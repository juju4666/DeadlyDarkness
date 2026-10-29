#!/usr/bin/env python3
"""
make_voxel_props.py - makes the placeholder voxel props (a car that explodes into a burning wreck) as real MagicaVoxel
.vox files, the BGVOXEL definition, and an editor icon.

    python make_voxel_props.py OUTDIR

writes   OUTDIR/data/voxels/VXCAR.vox  VXCARB.vox  VXCARC.vox  VXCARD.vox     (idle, explosion, two burning frames)
         OUTDIR/data/BGVOXEL.txt
         OUTDIR/sprites/props/VCARA0.png                                        (what the map editor shows)

One cell is one art pixel (0.4 map units at 2.5 pixels per unit): the car is 360 cells long, 146 wide, 100 high.
A .vox model holds at most 256 cells a side, so the car is written as two models placed side by side in the scene
graph, exactly like MagicaVoxel saves a larger object.

Palette slots 241..255 are emissive (they ignore the light and glow); everything else is lit.
"""
import os, struct, sys
import numpy as np

L, W, H = 360, 146, 112          # cells: length (x), width (y), height (z)
X0, Y0 = -L // 2, -W // 2        # world origin (0, 0) is the middle of the footprint; z = 0 is the ground

# palette slots (voxel colour index -> rgb)
PAL = {
    1: (35, 70, 160),     # body
    2: (22, 22, 24),      # tyre
    3: (130, 180, 210),   # glass
    4: (220, 222, 225),   # chrome
    5: (140, 140, 150),   # hubcap
    6: (36, 33, 31),      # char
    7: (95, 35, 25),      # rust red
    8: (20, 28, 36),      # dark glass
    9: (50, 52, 58),      # bumper
    10: (70, 24, 20),     # burnt body
    241: (255, 120, 20),  # emissive orange
    242: (255, 215, 70),  # emissive yellow
    243: (255, 40, 20),   # emissive red
    244: (255, 250, 220), # emissive white
}


def grid():
    return np.zeros((L, W, H), dtype=np.uint8)


def car(variant, seed=0):
    g = grid()
    rng = np.random.default_rng(seed)
    xs = np.arange(L)[:, None, None] + X0
    ys = np.arange(W)[None, :, None] + Y0
    zs = np.arange(H)[None, None, :]
    burnt = variant in ('C', 'D')
    # lower body, the hood and the trunk slope down
    top = 56 - np.clip(xs - 100, 0, None) * 0.15 - np.clip(-110 - xs, 0, None) * 0.12
    body = (abs(xs) <= 175) & (abs(ys) <= 70) & (zs >= 16) & (zs <= top)
    # rounded corners
    body &= ~((abs(xs) > 150) & (abs(ys) > 55) & ((abs(xs) - 150) + (abs(ys) - 55) > 22))
    # wheel arches
    for wx in (-110, 110):
        for sgn in (-1, 1):
            arch = ((xs - wx) ** 2 + (zs - 22) ** 2 <= 27 ** 2) & (ys * sgn >= 46)
            body &= ~arch
    # cabin: narrower and shorter towards the roof
    roof = 92 if burnt else 100
    hw = 62 - np.clip(zs - 56, 0, None) * 0.25
    front = 70 - np.clip(zs - 56, 0, None) * 0.5
    rear = -95 + np.clip(zs - 56, 0, None) * 0.4
    cabin = (zs > 56) & (zs <= roof) & (abs(ys) <= hw) & (xs <= front) & (xs >= rear)
    g[body] = 10 if burnt else 1
    g[cabin] = 10 if burnt else 1
    # windows: a band round the cabin
    band = cabin & (zs >= 66) & (zs <= roof - 8)
    pillars = ((abs(xs - (-10)) < 5) | (abs(xs - 65) < 4) | (abs(xs + 92) < 5))
    glass = band & ~pillars
    # only the outer shell shows the glass: keep it simple, colour every glass cell
    g[glass] = 8 if burnt else 3
    # wheels
    for wx in (-110, 110):
        for sgn in (-1, 1):
            ring = ((xs - wx) ** 2 + (zs - 22) ** 2 <= 22 ** 2) & (ys * sgn >= 52) & (ys * sgn <= 73)
            hub = ((xs - wx) ** 2 + (zs - 22) ** 2 <= 10 ** 2) & (ys * sgn >= 70) & (ys * sgn <= 73)
            g[ring] = 6 if burnt else 2
            g[hub] = 6 if burnt else 5
    # bumpers
    bump = ((xs >= 168) | (xs <= -168)) & (abs(xs) <= 178) & (abs(ys) <= 68) & (zs >= 18) & (zs <= 27)
    g[bump] = 6 if burnt else 9
    chrome = (xs >= 176) & (xs <= 179) & (abs(ys) <= 62) & (zs >= 20) & (zs <= 24)
    g[chrome] = 6 if burnt else 4
    if not burnt:
        for sgn in (-1, 1):
            head = (xs >= 172) & (xs <= 175) & (ys * sgn >= 38) & (ys * sgn <= 58) & (zs >= 38) & (zs <= 46)
            tail = (xs <= -172) & (xs >= -175) & (ys * sgn >= 42) & (ys * sgn <= 60) & (zs >= 40) & (zs <= 46)
            g[head] = 244
            g[tail] = 243
    if variant == 'B':
        # the blast: the body glows, a fireball on top
        solid = g > 0
        glow = solid & (rng.random(g.shape) < 0.55)
        g[glow] = 241
        g[solid & ~glow] = 6
        cx, cy, cz = 0, 0, 100
        ball = ((xs - cx) ** 2 + (ys - cy) ** 2 + (zs - cz) ** 2) <= 78 ** 2
        noise = rng.random(g.shape)
        g[ball & (noise < 0.8)] = 241
        g[ball & (noise < 0.35)] = 242
        g[ball & (noise < 0.08)] = 243
    if burnt:
        # flames over the engine and through the windows, each frame a different set
        for _ in range(60):
            if rng.random() < 0.6:
                fx = rng.integers(60, 165)
                fy = rng.integers(-55, 55)
                base = 52 - max(0, fx - 100) * 0.15
            else:
                fx = rng.integers(-85, 55)
                fy = rng.integers(-50, 50)
                base = roof - 2
            hgt = int(rng.integers(18, 62))
            wd = int(rng.integers(5, 11))
            for dz in range(hgt):
                t = dz / hgt
                r = max(1, int(round(wd * (1.0 - 0.8 * t) / 2)))
                z = int(base) + dz
                if z >= H:
                    break
                xi = np.arange(max(fx - r, X0), min(fx + r, X0 + L - 1) + 1)
                yi = np.arange(max(fy - r, Y0), min(fy + r, Y0 + W - 1) + 1)
                if xi.size == 0 or yi.size == 0:
                    continue
                col = 241 if t < 0.55 else 242
                if t > 0.85:
                    col = 243
                sub = g[xi[0] - X0:xi[-1] - X0 + 1, yi[0] - Y0:yi[-1] - Y0 + 1, z]
                sub[(sub == 0) | (rng.random(sub.shape) < 0.5)] = col
    return shell(g)


def shell(g):
    """Keeps only the cells that touch the outside (the inside of a solid object is never seen, so it only makes the
    file big). Hollow pockets that do not reach the outside are dropped too."""
    from scipy import ndimage
    empty = np.pad(g == 0, 1, constant_values=True)
    lab, _ = ndimage.label(empty)
    outside = lab == lab[0, 0, 0]
    keep = np.zeros(g.shape, dtype=bool)
    o = outside
    for ax in range(3):
        for sh in (1, -1):
            keep |= np.roll(o, sh, axis=ax)[1:-1, 1:-1, 1:-1] & (g > 0)
    out = np.where(keep, g, 0).astype(np.uint8)
    return out


# ---------------------------------------------------------------- writing .vox

def chunk(cid, content, children=b''):
    return cid + struct.pack('<ii', len(content), len(children)) + content + children


def sdict(d):
    out = struct.pack('<i', len(d))
    for k, v in d.items():
        kb, vb = k.encode(), v.encode()
        out += struct.pack('<i', len(kb)) + kb + struct.pack('<i', len(vb)) + vb
    return out


def write_vox(path, g, split=180):
    """g[x, y, z] = colour index (0 = empty), world coordinates start at (X0, Y0, 0)."""
    chunks = b''
    models = []
    for x in range(0, L, split):
        sub = g[x:x + split]
        pos = np.argwhere(sub > 0)
        if len(pos) == 0:
            continue
        mn = pos.min(axis=0)
        mx = pos.max(axis=0)
        size = (mx - mn + 1).tolist()
        vox = pos - mn
        cols = sub[pos[:, 0], pos[:, 1], pos[:, 2]]
        data = b''.join(struct.pack('<BBBB', int(a), int(b), int(c), int(v)) for (a, b, c), v in zip(vox, cols))
        chunks += chunk(b'SIZE', struct.pack('<iii', *size))
        chunks += chunk(b'XYZI', struct.pack('<i', len(vox)) + data)
        world_min = (int(mn[0]) + x + X0, int(mn[1]) + Y0, int(mn[2]))
        models.append((size, world_min))
    nid = 0
    nodes = b''
    # node 0: transform -> node 1: group of (transform -> shape) pairs
    kids = [2 + 2 * k for k in range(len(models))]
    nodes += chunk(b'nTRN', struct.pack('<i', 0) + sdict({}) + struct.pack('<iiii', 1, -1, 0, 1) + sdict({'_t': '0 0 0'}))
    nodes += chunk(b'nGRP', struct.pack('<i', 1) + sdict({}) + struct.pack('<i', len(kids)) + b''.join(struct.pack('<i', k) for k in kids))
    for k, (size, wmin) in enumerate(models):
        t = ' '.join(str(wmin[i] + size[i] // 2) for i in range(3))
        nodes += chunk(b'nTRN', struct.pack('<i', 2 + 2 * k) + sdict({}) + struct.pack('<iiii', 3 + 2 * k, -1, 0, 1) + sdict({'_t': t}))
        nodes += chunk(b'nSHP', struct.pack('<i', 3 + 2 * k) + sdict({}) + struct.pack('<i', 1) + struct.pack('<i', k) + sdict({}))
    rgba = bytearray(1024)
    for i in range(1, 256):
        r, gg, b = PAL.get(i, (128, 128, 128))
        rgba[(i - 1) * 4:(i - 1) * 4 + 4] = bytes((r, gg, b, 255))
    body = chunks + nodes + chunk(b'RGBA', bytes(rgba))
    main = chunk(b'MAIN', b'', body)
    with open(path, 'wb') as f:
        f.write(b'VOX ' + struct.pack('<i', 150) + main)
    return models


def icon(g, path):
    from PIL import Image
    # a side view: for every (x, z) the colour of the nearest cell seen from -y
    h, w = H, L
    img = np.zeros((h, w, 4), dtype=np.uint8)
    for xi in range(L):
        col = g[xi]                      # [y, z]
        for zi in range(H):
            ys = np.nonzero(col[:, zi])[0]
            if len(ys):
                c = int(col[ys[0], zi])
                r, gg, b = PAL.get(c, (128, 128, 128))
                img[h - 1 - zi, xi] = (r, gg, b, 255)
    im = Image.fromarray(img, 'RGBA').resize((w // 4, h // 4), Image.NEAREST)
    im.save(path)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'out'
    vdir = os.path.join(out, 'data', 'voxels')
    os.makedirs(vdir, exist_ok=True)
    os.makedirs(os.path.join(out, 'sprites', 'props'), exist_ok=True)
    frames = {'VXCAR': car('A'), 'VXCARB': car('B', 1), 'VXCARC': car('C', 2), 'VXCARD': car('D', 3)}
    for name, g in frames.items():
        m = write_vox(os.path.join(vdir, name + '.vox'), g)
        print(name, len(m), 'models', int((g > 0).sum()), 'cells')
    icon(frames['VXCAR'], os.path.join(out, 'sprites', 'props', 'VCARA0.png'))
    with open(os.path.join(out, 'data', 'BGVOXEL.txt'), 'w') as f:
        f.write("""# Voxel props (see BEGOTTEN_ENGINE.md 0.10.69).
#   prop  KEY EDNUM sprite SPRT [radius R] [height H] [health N] [mass M] [offset X Y Z]
#   model KEY FRAME LUMP [light RADIUS STRENGTH R G B] [yaw DEGREES]
# FRAME: A idle, B explosion, C and D burning wreck (D optional). One cell = one art pixel.
prop  car 21101 sprite VCAR radius 44 height 44 health 150 mass 1000
model car A VXCAR
model car B VXCARB light 360 1.0  1.0 0.55 0.2
model car C VXCARC light 300 0.85 1.0 0.5  0.15
model car D VXCARD light 300 0.8  1.0 0.55 0.2
""")


if __name__ == '__main__':
    main()
