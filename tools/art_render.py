"""
art_render.py - a tiny analytic ray-caster used to draw the placeholder art.

Everything is built out of spheres, ellipsoids, boxes and (tapered) cylinders, drawn orthographically from a camera that
looks slightly down.  A figure is described in its own frame (x = its left, y = forward, z = up, units = map units); the
renderer turns it to face any direction, so the eight rotations of a sprite come from one model instead of eight drawings.

The output is meant to be readable ("that is a shotgun, that is a werewolf"), not final: it is a stand-in for the real art.
"""
import math
import numpy as np
from PIL import Image

LIGHT = np.array([-0.55, -0.55, 0.80])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def rot_matrix(rx=0.0, ry=0.0, rz=0.0):
    """Rz * Ry * Rx, angles in degrees"""
    ax, ay, az = math.radians(rx), math.radians(ry), math.radians(rz)
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    mx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    my = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    mz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return mz @ my @ mx


def frame_along(a, b):
    """rotation whose local z axis points from a to b"""
    a, b = np.asarray(a, float), np.asarray(b, float)
    z = b - a
    n = np.linalg.norm(z)
    if n < 1e-9:
        return np.eye(3), 0.0
    z = z / n
    helper = np.array([1.0, 0, 0]) if abs(z[0]) < 0.9 else np.array([0, 1.0, 0])
    x = np.cross(helper, z)
    x /= np.linalg.norm(x)
    y = np.cross(z, x)
    return np.stack([x, y, z], axis=1), n


def mat(color, spec=0.0, shin=24, glow=0.0, noise=0.05, alpha=1.0, flat=False):
    return dict(color=np.array(color[:3], float), spec=spec, shin=shin, glow=glow, noise=noise, flat=flat)


def shade_color(m, k):
    return mat(np.clip(m['color'] * k, 0, 255), m['spec'], m['shin'], m['glow'], m['noise'], flat=m.get('flat', False))


class Fig:
    """a list of primitives; coordinates are in the figure's own frame"""

    def __init__(self):
        self.prims = []

    # -- primitives
    def ell(self, c, r, m, rot=(0, 0, 0), group=0):
        r = np.broadcast_to(np.asarray(r, float), (3,)).copy()
        self.prims.append(dict(kind='ell', c=np.asarray(c, float), s=r, R=rot_matrix(*rot) if not isinstance(rot, np.ndarray) else rot, m=m, g=group))

    def sph(self, c, r, m, group=0):
        self.ell(c, r, m, group=group)

    def box(self, c, half, m, rot=(0, 0, 0), group=0):
        half = np.broadcast_to(np.asarray(half, float), (3,)).copy()
        self.prims.append(dict(kind='box', c=np.asarray(c, float), s=half, R=rot_matrix(*rot) if not isinstance(rot, np.ndarray) else rot, m=m, g=group))

    def cone(self, a, b, r0, r1, m, caps=True, group=0):
        """a tapered cylinder from point a (radius r0) to point b (radius r1)"""
        R, n = frame_along(a, b)
        if n <= 0:
            return
        c = (np.asarray(a, float) + np.asarray(b, float)) / 2
        self.prims.append(dict(kind='cone', c=c, s=np.array([r0, r1, n / 2.0]), R=R, m=m, g=group, caps=caps))

    def cyl(self, c, r, h, m, rot=(0, 0, 0), group=0):
        """upright (before rot) cylinder, total height h"""
        R = rot_matrix(*rot)
        self.prims.append(dict(kind='cone', c=np.asarray(c, float), s=np.array([r, r, h / 2.0]), R=R, m=m, g=group, caps=True))

    def limb(self, a, b, r0, r1, m, joint=True, group=0):
        """a rounded tapered limb: a cone with a ball at each end"""
        self.cone(a, b, r0, r1, m, caps=False, group=group)
        if joint:
            self.sph(a, r0, m, group)
            self.sph(b, r1, m, group)

    def extend(self, other):
        self.prims.extend(other.prims)

    # -- whole-figure transforms
    def transformed(self, R, t=(0, 0, 0), pivot=(0, 0, 0)):
        out = Fig()
        R = np.asarray(R, float)
        t = np.asarray(t, float)
        pv = np.asarray(pivot, float)
        for p in self.prims:
            q = dict(p)
            q['c'] = R @ (p['c'] - pv) + pv + t
            q['R'] = R @ p['R']
            out.prims.append(q)
        return out

    def lowest(self):
        z = 1e9
        for p in self.prims:
            z = min(z, p['c'][2] - self._radius(p) * 0.85)
        return z

    @staticmethod
    def _radius(p):
        if p['kind'] == 'cone':
            return float(max(p['s'][0], p['s'][1]) + p['s'][2]) * 0.9
        return float(np.linalg.norm(p['s'])) if p['kind'] == 'box' else float(np.max(p['s']))


def _bound_radius(p):
    if p['kind'] == 'box':
        return float(np.linalg.norm(p['s']))
    if p['kind'] == 'cone':
        return float(math.hypot(max(p['s'][0], p['s'][1]), p['s'][2]))
    return float(np.max(p['s']))


def _intersect(p, O, D):
    """O: (N,3) ray origins, D: (3,) direction.  Returns t (N,), normal (N,3) in world frame; t = inf for misses"""
    R, c, s = p['R'], p['c'], p['s']
    ol = (O - c) @ R            # = R^T (O - c) per row
    dl = D @ R
    N = O.shape[0]
    inf = np.full(N, np.inf)
    if p['kind'] == 'ell':
        o = ol / s
        d = dl / s
        a = float(d @ d)
        b = o @ d
        cc = np.einsum('ij,ij->i', o, o) - 1.0
        disc = b * b - a * cc
        ok = disc > 0
        sq = np.sqrt(np.where(ok, disc, 0))
        t = (-b - sq) / a
        ok &= t > -1e5
        pt = o + t[:, None] * d
        n = (pt / s) @ R.T
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        return np.where(ok, t, inf), n
    if p['kind'] == 'box':
        with np.errstate(divide='ignore', invalid='ignore'):
            inv = 1.0 / np.where(np.abs(dl) < 1e-9, 1e-9, dl)
            t1 = (-s - ol) * inv
            t2 = (s - ol) * inv
        tn = np.minimum(t1, t2)
        tf = np.maximum(t1, t2)
        tmin = tn.max(axis=1)
        tmax = tf.min(axis=1)
        ok = tmin <= tmax
        axis = tn.argmax(axis=1)
        nl = np.zeros((N, 3))
        sign = -np.sign(dl)[axis]
        nl[np.arange(N), axis] = sign
        # a little bevel: bend the normal toward the neighbouring faces near the edges
        pt = ol + tmin[:, None] * dl
        q = pt / s
        bev = 0.35
        for ax in range(3):
            m_ = (axis != ax)
            nl[:, ax] += np.where(m_, np.clip((np.abs(q[:, ax]) - 0.78) / 0.22, 0, 1) * np.sign(q[:, ax]) * bev, 0)
        n = nl @ R.T
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        return np.where(ok, tmin, inf), n
    # cone / cylinder along local z, z in [-h, h], radius r(z) = r0 + (r1-r0)*(z+h)/(2h)
    r0, r1, h = s
    k = (r1 - r0) / (2 * h)
    rz0 = r0 + k * h              # radius at z = 0
    ox, oy, oz = ol[:, 0], ol[:, 1], ol[:, 2]
    dx, dy, dz = dl
    A = dx * dx + dy * dy - (k * dz) ** 2
    B = 2 * (ox * dx + oy * dy - (rz0 + k * oz) * k * dz)
    C = ox * ox + oy * oy - (rz0 + k * oz) ** 2
    best = inf.copy()
    nl = np.zeros((N, 3))
    if abs(A) > 1e-12:
        disc = B * B - 4 * A * C
        ok = disc > 0
        sq = np.sqrt(np.where(ok, disc, 0))
        for sgn in (-1, 1):
            t = (-B + sgn * sq) / (2 * A)
            z = oz + t * dz
            good = ok & (np.abs(z) <= h) & (t < best) & (t > -1e5)
            if good.any():
                x = ox + t * dx
                y = oy + t * dy
                rr = rz0 + k * z
                nn = np.stack([x, y, -k * rr], axis=1)
                best = np.where(good, t, best)
                nl = np.where(good[:, None], nn, nl)
    if p.get('caps', True) and abs(dz) > 1e-9:
        for zc, rc, sg in ((-h, r0, -1.0), (h, r1, 1.0)):
            t = (zc - oz) / dz
            x = ox + t * dx
            y = oy + t * dy
            good = (x * x + y * y <= rc * rc) & (t < best) & (t > -1e5)
            best = np.where(good, t, best)
            nl = np.where(good[:, None], np.array([0, 0, sg]), nl)
    n = nl @ R.T
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    return best, n


def _hash(P, seed=0):
    v = np.sin(P @ np.array([12.9898, 78.233, 37.719]) + seed) * 43758.5453
    return v - np.floor(v)


def _vnoise(P, freq):
    """cheap smooth value noise in [0,1]"""
    q = P * freq
    i = np.floor(q)
    f = q - i
    f = f * f * (3 - 2 * f)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                out = out + w * _hash(i + np.array([dx, dy, dz]))
    return out


def render(fig, facing=0.0, ppu=2.5, ss=3, elev=12.0, outline=1.4, pad=2, light=None, minsize=1):
    """draw fig turned to face `facing` degrees (0 = towards the viewer, 90 = towards the viewer's left, 180 = away).
    Returns (RGBA image, origin_x, origin_y): the pixel in the image where the figure's feet (0,0,0) are."""
    if not fig.prims:
        return Image.new('RGBA', (1, 1), (0, 0, 0, 0)), 0, 0
    f = math.radians(facing)
    fwd = np.array([-math.sin(f), -math.cos(f), 0.0])
    left = np.array([math.cos(f), -math.sin(f), 0.0])
    Rf = np.stack([left, fwd, np.array([0, 0, 1.0])], axis=1)      # local -> world
    e = math.radians(elev)
    D = np.array([0.0, math.cos(e), -math.sin(e)])
    U = np.array([0.0, math.sin(e), math.cos(e)])
    prims = []
    for p in fig.prims:
        q = dict(p)
        q['c'] = Rf @ p['c']
        q['R'] = Rf @ p['R']
        prims.append(q)
    # screen extents
    xs0, xs1, vs0, vs1 = 1e9, -1e9, 1e9, -1e9
    for q in prims:
        r = _bound_radius(q)
        cx, cv = q['c'][0], q['c'] @ U
        xs0, xs1 = min(xs0, cx - r), max(xs1, cx + r)
        vs0, vs1 = min(vs0, cv - r), max(vs1, cv + r)
    S = ppu * ss
    x0, v1 = xs0 - pad / ppu, vs1 + pad / ppu
    W = int(math.ceil((xs1 - xs0) * S + 2 * pad * ss))
    H = int(math.ceil((vs1 - vs0) * S + 2 * pad * ss))
    Tb = np.full((H, W), np.inf)
    Nb = np.zeros((H, W, 3))
    Mb = np.full((H, W), -1, dtype=np.int32)
    Pb = np.zeros((H, W, 3))
    mats = []
    gids = np.zeros((H, W), dtype=np.int32)
    for q in prims:
        r = _bound_radius(q) + 0.5
        cx, cv = q['c'][0], q['c'] @ U
        px0 = max(0, int((cx - r - x0) * S))
        px1 = min(W, int((cx + r - x0) * S) + 2)
        py0 = max(0, int((v1 - (cv + r)) * S))
        py1 = min(H, int((v1 - (cv - r)) * S) + 2)
        if px1 <= px0 or py1 <= py0:
            continue
        gx, gy = np.meshgrid(np.arange(px0, px1), np.arange(py0, py1))
        xw = x0 + (gx.ravel() + 0.5) / S
        vw = v1 - (gy.ravel() + 0.5) / S
        O = np.outer(xw, np.array([1.0, 0, 0])) + np.outer(vw, U) - 200.0 * D
        t, n = _intersect(q, O, D)
        hit = np.isfinite(t)
        if not hit.any():
            continue
        cur = Tb[py0:py1, px0:px1].ravel()
        better = hit & (t < cur)
        if not better.any():
            continue
        mi = len(mats)
        mats.append(q['m'])
        sl = (slice(py0, py1), slice(px0, px1))
        shape = (py1 - py0, px1 - px0)
        Tb[sl] = np.where(better, t, cur).reshape(shape)
        Nb[sl] = np.where(better[:, None], n, Nb[sl].reshape(-1, 3)).reshape(shape + (3,))
        Mb[sl] = np.where(better, mi, Mb[sl].ravel()).reshape(shape)
        Pb[sl] = np.where(better[:, None], O + np.where(hit, t, 0)[:, None] * D, Pb[sl].reshape(-1, 3)).reshape(shape + (3,))
        gids[sl] = np.where(better, q['g'], gids[sl].ravel()).reshape(shape)
    mask = Mb >= 0
    if not mask.any():
        return Image.new('RGBA', (minsize, minsize), (0, 0, 0, 0)), 0, 0
    # --- shading
    L = LIGHT if light is None else np.asarray(light, float) / np.linalg.norm(light)
    col = np.zeros((H, W, 3))
    ndl = np.clip(Nb @ L, 0, 1)
    up = Nb[:, :, 2]
    view = -D
    half = L + view
    half /= np.linalg.norm(half)
    ndh = np.clip(Nb @ half, 0, 1)
    rim = np.clip(1.0 - Nb @ view, 0, 1) ** 3
    for i, m in enumerate(mats):
        sel = Mb == i
        if not sel.any():
            continue
        base = m['color']
        if m.get('flat'):
            col[sel] = base
            continue
        amb = 0.40 + 0.10 * up[sel]
        lit = amb + 0.72 * ndl[sel]
        rgb = base[None, :] * lit[:, None]
        if m['noise'] > 0:
            nz = _vnoise(Pb[sel], 0.9) * 0.6 + _vnoise(Pb[sel], 3.1) * 0.4
            rgb = rgb * (1.0 + (nz[:, None] - 0.5) * 2 * m['noise'])
        if m['spec'] > 0:
            sp = (ndh[sel] ** m['shin']) * m['spec'] * 255
            rgb = rgb + sp[:, None]
        rgb = rgb + rim[sel][:, None] * 18
        if m['glow'] > 0:
            rgb = rgb * (1 - m['glow']) + base[None, :] * (1.0 + 0.25 * ndl[sel][:, None]) * m['glow'] * 1.1
        col[sel] = rgb
    col = np.clip(col, 0, 255)
    # --- outline: a pixel of the nearer surface that has a hole or a far-away neighbour next to it
    if outline > 0:
        edge = np.zeros((H, W), bool)
        thr = 3.2
        for dy_, dx_ in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            Tn = np.full((H, W), np.inf)
            ys = slice(max(0, dy_), H + min(0, dy_))
            yd = slice(max(0, -dy_), H + min(0, -dy_))
            xs = slice(max(0, dx_), W + min(0, dx_))
            xd = slice(max(0, -dx_), W + min(0, -dx_))
            Tn[yd, xd] = Tb[ys, xs]
            with np.errstate(invalid='ignore'):
                edge |= mask & ((Tn - Tb) > thr)
        w = int(round(outline * ss / 2.0))
        if w > 0:
            ed = edge.copy()
            for _ in range(w):
                e2 = ed.copy()
                e2[1:, :] |= ed[:-1, :]
                e2[:-1, :] |= ed[1:, :]
                e2[:, 1:] |= ed[:, :-1]
                e2[:, :-1] |= ed[:, 1:]
                ed = e2
            edge = ed & mask
        dark = col * 0.22 + np.array([6, 4, 10])
        col = np.where(edge[:, :, None], dark, col)
    # --- downsample (premultiplied)
    a = mask.astype(float)
    pm = col * a[:, :, None]
    hh, ww = H // ss, W // ss
    pm = pm[:hh * ss, :ww * ss].reshape(hh, ss, ww, ss, 3).sum(axis=(1, 3))
    aa = a[:hh * ss, :ww * ss].reshape(hh, ss, ww, ss).sum(axis=(1, 3))
    rgb = pm / np.maximum(aa[:, :, None], 1e-9)
    alpha = aa / (ss * ss)
    out = np.zeros((hh, ww, 4), np.uint8)
    out[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    out[:, :, 3] = np.where(alpha >= 0.5, 255, 0).astype(np.uint8)
    img = Image.fromarray(out, 'RGBA')
    # origin: world x = 0 and screen v = 0
    ox = (0 - x0) * ppu
    oy = (v1 - 0.0) * ppu
    bbox = img.getbbox()
    if bbox is None:
        return Image.new('RGBA', (minsize, minsize), (0, 0, 0, 0)), 0, 0
    img = img.crop(bbox)
    return img, int(round(ox - bbox[0])), int(round(oy - bbox[1]))
