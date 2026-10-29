#!/usr/bin/env python3
"""
obj_to_vox.py - turn a Blockbench (or any) OBJ / glTF / GLB model into a MagicaVoxel .vox file for Begotten voxel props.

  python obj_to_vox.py car.glb VXCAR.vox                      # 1 voxel = 0.4 map units (the size of the art pixels)
  python obj_to_vox.py car.obj VXCAR.vox --length 144         # scale so the model is 144 map units long
  python obj_to_vox.py car.obj VXCAR.vox --scale 16           # 1 model unit = 16 map units (Blockbench: 1 block = 16 px)
  python obj_to_vox.py lamp.glb VXLAMP.vox --emissive lamp    # materials whose name contains "lamp" glow

Needs: pip install trimesh numpy pillow scipy

Orientation: --up and --front name the model axes (x, y, z, optionally with a minus). Blockbench is Y up and its
front ("north") is -Z, which is the default. The output has the front on +x, left on +y, up on +z, the lowest point on
the floor and the middle of the footprint at the thing's position (see BEGOTTEN_ENGINE.md, 0.10.69).

Colours: from the texture (UV sampled), else the material colour, else the vertex colour. Transparent texels are
skipped. Materials with "emissive"/"glow"/"light" in the name (or --emissive WORD), or a glTF emissive factor, go to
the glowing palette slots 241-255 (up to 15 distinct glow colours); everything else is reduced to 240 colours. The game
then picks the closest colour of its own palette.

Models bigger than 256 voxels in an axis are split into several models in one .vox scene (the engine reads it).
"""
import argparse, os, struct, sys
import numpy as np

VOXEL_UNITS = 0.4           # map units per voxel (the art pixel size, BGSCALE 2.5)
TILE = 240                  # a .vox model is at most 256 cells per axis


def axis_vec(s):
    s = s.strip().lower()
    sign = -1.0 if s.startswith('-') else 1.0
    a = 'xyz'.index(s.lstrip('+-'))
    v = np.zeros(3)
    v[a] = sign
    return v


def load_triangles(path, emissive_words):
    """Returns triangles [n,3,3] (model units), per-triangle uv [n,3,2] (or None), and per-triangle material index; plus
    the list of materials (dict: image (PIL RGBA or None), colour (r,g,b), glow bool)."""
    import trimesh
    scene = trimesh.load(path, force='scene')
    tris, uvs, mats_of = [], [], []
    mats = []
    for name, geom in scene.geometry.items():
        node_tf = None
        # apply the scene graph transform of every instance of this geometry
        for nname in scene.graph.nodes_geometry:
            tf, gname = scene.graph[nname]
            if gname != name:
                continue
            g = geom.copy()
            g.apply_transform(tf)
            v = g.vertices[g.faces]
            vis = g.visual
            mat = getattr(vis, 'material', None)
            img, col, glow = None, (200, 200, 200), False
            mname = (getattr(mat, 'name', '') or name or '').lower()
            if mat is not None:
                im = getattr(mat, 'image', None) or getattr(mat, 'baseColorTexture', None)
                if im is not None:
                    img = im.convert('RGBA')
                bc = getattr(mat, 'baseColorFactor', None)
                if bc is None and getattr(mat, 'main_color', None) is not None:
                    bc = mat.main_color
                if bc is not None:
                    col = tuple(int(c) for c in list(bc)[:3])
                ef = getattr(mat, 'emissiveFactor', None)
                if ef is not None and float(np.max(ef)) > 0.0:
                    glow = True
            if any(w and w in mname for w in emissive_words) or any(k in mname for k in ('emissive', 'glow', 'light')):
                glow = True
            mats.append({'image': img, 'colour': col, 'glow': glow, 'name': mname})
            mi = len(mats) - 1
            uv = None
            if getattr(vis, 'uv', None) is not None and img is not None:
                uv = np.asarray(vis.uv)[g.faces]
            elif img is None and vis.kind == 'vertex':
                pass
            tris.append(v)
            uvs.append(uv if uv is not None else np.full((len(v), 3, 2), np.nan))
            mats_of.append(np.full(len(v), mi))
    if not tris:
        sys.exit('no geometry found in ' + path)
    return np.concatenate(tris), np.concatenate(uvs), np.concatenate(mats_of), mats


def sample_surface(tris, uvs, mi, step):
    """Points on every triangle, at most `step` apart, with their barycentric uv and material index."""
    P, U, M = [], [], []
    e = np.maximum.reduce([np.linalg.norm(tris[:, 1] - tris[:, 0], axis=1),
                           np.linalg.norm(tris[:, 2] - tris[:, 1], axis=1),
                           np.linalg.norm(tris[:, 0] - tris[:, 2], axis=1)])
    k = np.maximum(1, np.ceil(e / step).astype(int))
    for kk in np.unique(k):
        sel = np.nonzero(k == kk)[0]
        ij = np.array([(i, j) for i in range(kk + 1) for j in range(kk + 1 - i)], dtype=float) / kk
        b1, b2 = ij[:, 0], ij[:, 1]
        b0 = 1.0 - b1 - b2
        w = np.stack([b0, b1, b2], axis=1)                       # [s,3]
        P.append(np.einsum('sc,tcd->tsd', w, tris[sel]).reshape(-1, 3))
        U.append(np.einsum('sc,tcd->tsd', w, uvs[sel]).reshape(-1, 2))
        M.append(np.repeat(mi[sel], len(w)))
    return np.concatenate(P), np.concatenate(U), np.concatenate(M)


def colours_of(pts_uv, pts_m, mats):
    rgb = np.zeros((len(pts_m), 3), dtype=np.uint8)
    ok = np.ones(len(pts_m), dtype=bool)
    glow = np.zeros(len(pts_m), dtype=bool)
    for i, m in enumerate(mats):
        sel = np.nonzero(pts_m == i)[0]
        if not len(sel):
            continue
        glow[sel] = m['glow']
        uv = pts_uv[sel]
        if m['image'] is not None and not np.isnan(uv).any():
            im = np.asarray(m['image'])
            h, w = im.shape[:2]
            x = np.clip((uv[:, 0] % 1.0 * w).astype(int), 0, w - 1)
            y = np.clip(((1.0 - uv[:, 1] % 1.0) * h).astype(int), 0, h - 1)
            px = im[y, x]
            rgb[sel] = px[:, :3]
            ok[sel] = px[:, 3] >= 128
        else:
            rgb[sel] = m['colour']
    return rgb, ok, glow


def quantize(rgb, glow):
    """Palette indices 1..255 for the voxel colours: 1..240 ordinary, 241..255 glowing."""
    from PIL import Image
    idx = np.zeros(len(rgb), dtype=np.uint8)
    pal = {}
    for is_glow, lo, n in ((False, 1, 240), (True, 241, 15)):
        sel = np.nonzero(glow == is_glow)[0]
        if not len(sel):
            continue
        im = Image.fromarray(rgb[sel].reshape(-1, 1, 3), 'RGB')
        q = im.quantize(colors=n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        p = np.array(q.getpalette()[:n * 3]).reshape(-1, 3)
        used = np.asarray(q).reshape(-1)
        idx[sel] = used + lo
        for i in np.unique(used):
            pal[int(i) + lo] = tuple(int(c) for c in p[i])
    return idx, pal


def chunk(cid, content, children=b''):
    return cid + struct.pack('<ii', len(content), len(children)) + content + children


def sdict(d):
    out = struct.pack('<i', len(d))
    for k, v in d.items():
        kb, vb = k.encode(), v.encode()
        out += struct.pack('<i', len(kb)) + kb + struct.pack('<i', len(vb)) + vb
    return out


def write_vox(path, grid, pal, origin):
    """grid[x,y,z] = palette index (0 empty). origin = grid cell that is world (0,0,0)."""
    chunks, models = b'', []
    for x0 in range(0, grid.shape[0], TILE):
        for y0 in range(0, grid.shape[1], TILE):
            for z0 in range(0, grid.shape[2], TILE):
                sub = grid[x0:x0 + TILE, y0:y0 + TILE, z0:z0 + TILE]
                pos = np.argwhere(sub > 0)
                if not len(pos):
                    continue
                mn, mx = pos.min(axis=0), pos.max(axis=0)
                size = (mx - mn + 1).tolist()
                vox = pos - mn
                cols = sub[pos[:, 0], pos[:, 1], pos[:, 2]]
                arr = np.concatenate([vox.astype(np.uint8), cols[:, None]], axis=1).astype(np.uint8)
                chunks += chunk(b'SIZE', struct.pack('<iii', *size))
                chunks += chunk(b'XYZI', struct.pack('<i', len(vox)) + arr.tobytes())
                wmin = (int(mn[0]) + x0 - origin[0], int(mn[1]) + y0 - origin[1], int(mn[2]) + z0 - origin[2])
                models.append((size, wmin))
    kids = [2 + 2 * k for k in range(len(models))]
    nodes = chunk(b'nTRN', struct.pack('<i', 0) + sdict({}) + struct.pack('<iiii', 1, -1, 0, 1) + sdict({'_t': '0 0 0'}))
    nodes += chunk(b'nGRP', struct.pack('<i', 1) + sdict({}) + struct.pack('<i', len(kids)) + b''.join(struct.pack('<i', k) for k in kids))
    for k, (size, wmin) in enumerate(models):
        t = ' '.join(str(wmin[i] + size[i] // 2) for i in range(3))
        nodes += chunk(b'nTRN', struct.pack('<i', 2 + 2 * k) + sdict({}) + struct.pack('<iiii', 3 + 2 * k, -1, 0, 1) + sdict({'_t': t}))
        nodes += chunk(b'nSHP', struct.pack('<i', 3 + 2 * k) + sdict({}) + struct.pack('<i', 1) + struct.pack('<i', k) + sdict({}))
    rgba = bytearray(1024)
    for i in range(1, 256):
        r, g, b = pal.get(i, (128, 128, 128))
        rgba[(i - 1) * 4:(i - 1) * 4 + 4] = bytes((r, g, b, 255))
    main = chunk(b'MAIN', b'', chunks + nodes + chunk(b'RGBA', bytes(rgba)))
    with open(path, 'wb') as f:
        f.write(b'VOX ' + struct.pack('<i', 150) + main)
    return len(models)


def shell(g):
    """Drop cells that cannot be seen (all six neighbours filled)."""
    p = np.pad(g > 0, 1)
    inner = np.ones_like(p[1:-1, 1:-1, 1:-1])
    for ax in range(3):
        for sh in (1, -1):
            inner &= np.roll(p, sh, axis=ax)[1:-1, 1:-1, 1:-1]
    out = g.copy()
    out[inner] = 0
    return out


def convert(src, dst, up='y', front='-z', scale=None, length=None, voxel=VOXEL_UNITS, emissive=(), fill=False):
    tris, uvs, mi, mats = load_triangles(src, [w.lower() for w in emissive])
    upv, frv = axis_vec(up), axis_vec(front)
    if abs(float(np.dot(upv, frv))) > 1e-6:
        sys.exit('--up and --front must be different axes')
    left = np.cross(upv, frv)                       # right-handed: front x left = up
    R = np.stack([frv, left, upv])                  # model -> game (x forward, y left, z up)
    tris = tris @ R.T
    uvs_ok = uvs
    lo, hi = tris.reshape(-1, 3).min(axis=0), tris.reshape(-1, 3).max(axis=0)
    if length:
        scale = length / max(hi[0] - lo[0], 1e-9)
    elif scale is None:
        scale = 1.0
    tris = tris * scale                              # now in map units
    lo, hi = tris.reshape(-1, 3).min(axis=0), tris.reshape(-1, 3).max(axis=0)
    cx, cy = 0.5 * (lo[0] + hi[0]), 0.5 * (lo[1] + hi[1])
    tris = tris - np.array([cx, cy, lo[2]])          # footprint middle at 0,0 and the lowest point on the floor
    vox = tris / voxel
    pts, uv, pm = sample_surface(vox, uvs_ok, mi, 0.45)
    rgb, ok, glow = colours_of(uv, pm, mats)
    pts, rgb, glow = pts[ok], rgb[ok], glow[ok]
    cell = np.floor(pts).astype(int)
    mn = cell.min(axis=0)
    cell -= mn
    shape = cell.max(axis=0) + 1
    if int(np.prod(shape.astype(np.int64))) > 400_000_000:
        sys.exit('model is too big at this voxel size: %s cells' % (shape.tolist(),))
    idx, pal = quantize(rgb, glow)
    grid = np.zeros(shape, dtype=np.uint8)
    # when several samples fall in one cell the glowing one wins, then the last one
    order = np.argsort(glow.astype(np.uint8), kind='stable')
    grid[cell[order, 0], cell[order, 1], cell[order, 2]] = idx[order]
    if fill:
        from scipy import ndimage
        filled = ndimage.binary_fill_holes(grid > 0)
        grid[(grid == 0) & filled] = 1 if 1 in pal else (next(iter(pal)) if pal else 1)
    grid = shell(grid)
    origin = (-int(mn[0]), -int(mn[1]), -int(mn[2]))
    n = write_vox(dst, grid, pal, origin)
    cells = int((grid > 0).sum())
    size_units = [round(float(s) * voxel, 1) for s in shape]
    print('%s: %d x %d x %d voxels, %d visible cells, %d model(s) in the scene' % (dst, shape[0], shape[1], shape[2], cells, n))
    print('size in map units: %s long, %s wide, %s high (1 voxel = %s units)' % (size_units[0], size_units[1], size_units[2], voxel))
    return cells


def main():
    ap = argparse.ArgumentParser(description='OBJ / glTF / GLB to a Begotten voxel .vox')
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('--up', default='y', help='model up axis (default y, as in Blockbench)')
    ap.add_argument('--front', default='-z', help='model front axis (default -z, Blockbench north)')
    ap.add_argument('--scale', type=float, help='map units per model unit (default 1)')
    ap.add_argument('--length', type=float, help='scale so the model is this many map units long (front to back)')
    ap.add_argument('--voxel', type=float, default=VOXEL_UNITS, help='map units per voxel (default 0.4, the art pixel size)')
    ap.add_argument('--emissive', action='append', default=[], help='material names containing this word glow (repeatable)')
    ap.add_argument('--fill', action='store_true', help='close small holes in the surface before dropping hidden cells')
    a = ap.parse_args()
    convert(a.input, a.output, a.up, a.front, a.scale, a.length, a.voxel, a.emissive, a.fill)


if __name__ == '__main__':
    main()
