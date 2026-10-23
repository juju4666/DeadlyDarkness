"""art_fx.py - projectiles, explosions, fogs and glows: 2D procedural pictures (plus 3D rockets/bolts for the ones that turn)"""
import math
import numpy as np
from PIL import Image
from art_render import Fig, mat, render
from art_monsters import M
from art_monsters2 import GLOW

PAL = {
    'fire':   [(0, (255, 255, 235)), (0.30, (255, 226, 90)), (0.60, (244, 120, 24)), (0.85, (170, 44, 14)), (1, (70, 20, 12))],
    'plasma': [(0, (255, 255, 255)), (0.30, (170, 240, 255)), (0.60, (60, 160, 255)), (0.85, (30, 70, 200)), (1, (20, 30, 110))],
    'green':  [(0, (240, 255, 220)), (0.30, (150, 255, 110)), (0.60, (50, 200, 60)), (0.85, (24, 110, 40)), (1, (12, 50, 24))],
    'acid':   [(0, (250, 255, 190)), (0.30, (200, 255, 70)), (0.60, (110, 200, 30)), (0.85, (60, 120, 20)), (1, (30, 60, 14))],
    'magic':  [(0, (255, 240, 255)), (0.30, (255, 160, 255)), (0.60, (190, 70, 230)), (0.85, (100, 36, 160)), (1, (50, 20, 90))],
    'drain':  [(0, (255, 200, 210)), (0.30, (230, 60, 90)), (0.60, (150, 24, 60)), (0.85, (80, 14, 44)), (1, (36, 8, 30))],
    'soul':   [(0, (255, 255, 255)), (0.30, (150, 200, 255)), (0.60, (60, 110, 240)), (0.85, (30, 50, 170)), (1, (16, 24, 90))],
    'mega':   [(0, (255, 255, 255)), (0.30, (190, 255, 230)), (0.60, (70, 220, 160)), (0.85, (30, 120, 150)), (1, (16, 50, 90))],
    'inv':    [(0, (255, 255, 230)), (0.30, (255, 240, 120)), (0.60, (200, 220, 50)), (0.85, (90, 150, 30)), (1, (40, 70, 20))],
    'dust':   [(0, (230, 220, 200)), (0.30, (190, 176, 150)), (0.60, (140, 124, 104)), (0.85, (96, 84, 72)), (1, (60, 54, 48))],
    'smoke':  [(0, (200, 200, 200)), (0.40, (150, 150, 150)), (0.80, (100, 100, 104)), (1, (60, 60, 66))],
    'blood':  [(0, (230, 60, 60)), (0.4, (190, 30, 34)), (0.8, (130, 14, 20)), (1, (80, 8, 12))],
    'fog':    [(0, (255, 255, 255)), (0.4, (190, 230, 255)), (0.8, (110, 160, 230)), (1, (60, 90, 180))],
    'gold':   [(0, (255, 255, 230)), (0.3, (255, 235, 130)), (0.6, (255, 180, 50)), (0.85, (200, 110, 20)), (1, (110, 60, 14))],
}


def ramp(name, t):
    stops = PAL[name]
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,))
    for c in range(3):
        out[..., c] = np.interp(t, [s[0] for s in stops], [s[1][c] for s in stops])
    return out


def fbm(w, h, seed, base=4, octaves=4):
    rs = np.random.RandomState(seed)
    out = np.zeros((h, w))
    amp = 1.0
    tot = 0
    for o in range(octaves):
        n = base * (2 ** o)
        g = rs.rand(max(2, int(n * h / max(w, h)) + 2), max(2, int(n * w / max(w, h)) + 2))
        im = Image.fromarray((g * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        out += np.asarray(im, float) / 255.0 * amp
        tot += amp
        amp *= 0.5
    return out / tot


def blob(w, h, pal, cx=None, cy=None, rx=None, ry=None, seed=1, rough=0.55, core=1.0, glow_edge=False):
    """a ragged glowing blob with a bright core: the base of most effect pictures"""
    cx = w / 2 if cx is None else cx
    cy = h / 2 if cy is None else cy
    rx = (w / 2 - 1) if rx is None else rx
    ry = (h / 2 - 1) if ry is None else ry
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.sqrt(((xx + 0.5 - cx) / max(rx, 1)) ** 2 + ((yy + 0.5 - cy) / max(ry, 1)) ** 2)
    n = fbm(w, h, seed)
    d2 = d + (n - 0.5) * rough * 1.6
    alpha = d2 < 1.0
    col = ramp(pal, np.clip(d2 / max(core, 0.2), 0, 1) ** 0.9)
    img = np.zeros((h, w, 4), np.uint8)
    img[..., :3] = np.clip(col, 0, 255).astype(np.uint8)
    img[..., 3] = np.where(alpha, 255, 0)
    return Image.fromarray(img, 'RGBA')


def orb(w, h, pal, fi, seed=1):
    wob = 1.0 + 0.06 * (fi % 2)
    im = blob(w, h, pal, rx=(w / 2 - 1) * 0.9 * wob, ry=(h / 2 - 1) * 0.9 * wob, seed=seed + fi * 7, rough=0.5, core=0.9)
    return im


def boom(w, h, pal, p, seed=1, smoke=True):
    """p in 0..1: how far the explosion has gone"""
    grow = 0.55 + 0.45 * math.sin(min(1.0, p * 1.2) * math.pi / 2)
    rx, ry = (w / 2 - 1) * grow, (h / 2 - 1) * grow
    pal2 = pal
    im = blob(w, h, pal2, rx=rx, ry=ry, seed=seed + int(p * 10), rough=0.7 + 0.5 * p, core=1.0 - 0.45 * p + 0.0)
    if p > 0.55:
        # the fire has gone: what is left is the dark rim, thinning out
        a = np.asarray(im).copy()
        n = fbm(w, h, seed + 40 + int(p * 9), base=7)
        a[..., 3] = np.where(n < 0.55 - (p - 0.55) * 0.9, a[..., 3], 0)
        im = Image.fromarray(a, 'RGBA')
    return im


def sparkle_column(w, h, pal, fi, n, seed=2):
    """teleport fog: a glowing column, brightest in the middle, shimmering"""
    p = fi / max(1, n - 1)
    amp = math.sin(math.pi * min(1, p * 1.05)) ** 0.8
    rx = (w / 2 - 1) * (0.35 + 0.65 * amp)
    ry = (h / 2 - 1) * (0.3 + 0.7 * amp)
    im = blob(w, h, pal, rx=rx, ry=ry, seed=seed + fi * 3, rough=0.9, core=0.8 + 0.3 * amp)
    rs = np.random.RandomState(seed + fi)
    a = np.asarray(im).copy()
    for _ in range(14):
        x, y = int(rs.uniform(0, w)), int(rs.uniform(0, h))
        a[max(0, y - 1):y + 2, max(0, x - 1):x + 2] = (255, 255, 255, 255)
    return Image.fromarray(a, 'RGBA')


def flame_column(w, h, pal, fi, seed=3):
    """a tall tongue of fire (arch-vile style): wide at the bottom, licking up"""
    yy, xx = np.mgrid[0:h, 0:w]
    u = 1.0 - (yy + 0.5) / h
    n = fbm(w, h, seed + fi * 5, base=5)
    width = (0.5 * (1 - u ** 1.3) * 0.95 + 0.04) * w / 2
    wob = (fbm(w, h, seed + fi * 11 + 3, base=3) - 0.5) * w * 0.35
    d = np.abs((xx + 0.5) - (w / 2 + wob * u)) / np.maximum(width, 1)
    d2 = d + (n - 0.5) * 0.7 + u * 0.35
    alpha = (d2 < 1.0) & (u < 0.98)
    col = ramp(pal, np.clip(d2 * 0.9, 0, 1))
    img = np.zeros((h, w, 4), np.uint8)
    img[..., :3] = col.astype(np.uint8)
    img[..., 3] = np.where(alpha, 255, 0)
    return Image.fromarray(img, 'RGBA')


def puff(w, h, fi, pal='smoke', seed=5):
    grow = 0.5 + 0.17 * fi
    return blob(w, h, pal, rx=(w / 2 - 1) * min(1, grow), ry=(h / 2 - 1) * min(1, grow), seed=seed + fi, rough=0.6)


def splat(w, h, fi, seed=7):
    grow = 0.55 + 0.22 * fi
    im = blob(w, h, 'blood', rx=(w / 2 - 1) * min(1, grow), ry=(h / 2 - 1) * min(1, grow) * 0.9, seed=seed + fi, rough=1.0)
    return im


def flash(w, h, pal, fi, seed=11):
    """a muzzle flash: a bright star with spikes"""
    yy, xx = np.mgrid[0:h, 0:w]
    cx, cy = w / 2, h / 2
    dx, dy = (xx + 0.5 - cx) / (w / 2), (yy + 0.5 - cy) / (h / 2)
    r = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    spikes = 0.55 + 0.45 * np.abs(np.cos(ang * (3 + fi % 2) + fi))
    n = fbm(w, h, seed + fi)
    d = r / np.maximum(spikes * (0.95 - 0.1 * fi % 2), 0.2) + (n - 0.5) * 0.4
    col = ramp(pal, np.clip(d, 0, 1) ** 1.1)
    img = np.zeros((h, w, 4), np.uint8)
    img[..., :3] = col.astype(np.uint8)
    img[..., 3] = np.where(d < 1, 255, 0)
    return Image.fromarray(img, 'RGBA')


# ---------------------------------------------------------------------------------------------- 3D projectiles
def rocket_fig(body=(150, 154, 150), nose=(226, 70, 40), flame_len=10, flame_pal=(255, 190, 70), scale=1.0):
    f = Fig()
    s = scale
    f.cone((0, -6 * s, 0), (0, 9 * s, 0), 2.0 * s, 2.0 * s, M(body, spec=0.5, noise=0.05))
    f.cone((0, 9 * s, 0), (0, 15 * s, 0), 2.0 * s, 0.2 * s, M(nose, spec=0.4))
    for k in range(4):
        a = k * math.pi / 2
        f.box((math.cos(a) * 2.5 * s, -5 * s, math.sin(a) * 2.5 * s), (0.25 * s, 2.6 * s, 1.5 * s) if k % 2 == 0 else (1.5 * s, 2.6 * s, 0.25 * s), M((90, 94, 100), spec=0.4))
    f.cone((0, -6 * s, 0), (0, -6 * s - flame_len, 0), 1.9 * s, 0.1, M(flame_pal, glow=1.0, noise=0, flat=True))
    f.cone((0, -6 * s, 0), (0, -6 * s - flame_len * 0.6, 0), 1.0 * s, 0.1, M((255, 250, 220), glow=1.0, noise=0, flat=True))
    return f


def bolt_fig(rgb, length=14, r=1.8, tail=10):
    f = Fig()
    core = M(tuple(min(255, int(c * 0.4 + 150)) for c in rgb), glow=1.0, noise=0, flat=True)
    out = M(rgb, glow=1.0, noise=0, flat=True)
    f.cone((0, -length / 2, 0), (0, length / 2, 0), r * 1.7, r * 1.7, out)
    f.sph((0, length / 2, 0), r * 1.7, out)
    f.cone((0, length / 2 - 1, 0), (0, length / 2, 0), r, r, core)
    f.cone((0, -length / 2, 0), (0, -length / 2 - tail, 0), r * 1.5, 0.1, out)
    f.cone((0, -length / 2, 0), (0, -length / 2 - tail * 0.6, 0), r * 0.8, 0.1, core)
    return f


def missile_fig(rgb, fi, body=(90, 96, 104)):
    f = Fig()
    f.cone((0, -4, 0), (0, 6, 0), 1.3, 1.3, M(body, spec=0.5, noise=0.05))
    f.cone((0, 6, 0), (0, 9, 0), 1.3, 0.15, M(rgb, glow=0.6, noise=0))
    for k in range(3):
        a = k * 2 * math.pi / 3
        f.box((math.cos(a) * 1.8, -3, math.sin(a) * 1.8), (0.2, 1.6, 0.9), M((70, 74, 80)))
    f.cone((0, -4, 0), (0, -4 - 7 - 3 * (fi % 2), 0), 1.2, 0.1, M(rgb, glow=1.0, noise=0, flat=True))
    return f


def rock_fig(fi):
    f = Fig()
    rs = np.random.RandomState(4 + fi)
    for k in range(6):
        p = rs.uniform(-1.5, 1.5, 3)
        f.ell(p, rs.uniform(2.6, 3.8, 3), M((120, 108, 94), noise=0.3))
    return f.transformed(__import__('art_render').rot_matrix(30 * fi, 50 * fi, 20 * fi))


# ---------------------------------------------------------------------------------------------- the player's own projectiles
def swirl(w, h, fi, seed=21):
    """tornado projectile: a sphere of wind, spiral bands turning from frame to frame"""
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = (xx + 0.5 - w / 2) / (w / 2), (yy + 0.5 - h / 2) / (h / 2)
    r = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    band = 0.5 + 0.5 * np.sin(3 * ang + r * 7.0 - fi * math.pi / 2)
    n = fbm(w, h, seed + fi, base=5)
    d = r + (n - 0.5) * 0.25
    t = np.clip(0.55 - 0.45 * band + r * 0.55, 0, 1)
    col = ramp('fog', t)
    img = np.zeros((h, w, 4), np.uint8)
    img[..., :3] = col.astype(np.uint8)
    img[..., 3] = np.where(d < 0.96, 255, 0)
    return Image.fromarray(img, 'RGBA')


def funnel(w, h, fi, seed=31):
    """a tornado: a funnel, narrow at the bottom, wide at the top, with bands of dust and wind turning round it"""
    yy, xx = np.mgrid[0:h, 0:w]
    u = 1.0 - (yy + 0.5) / h                         # 0 at the bottom, 1 at the top
    half = (0.10 + 0.9 * u ** 1.25) * (w / 2 - 1)
    sway = np.sin(u * 5 + fi * 1.3) * w * 0.05 * u
    dx = ((xx + 0.5) - w / 2 - sway) / np.maximum(half, 1)
    n = fbm(w, h, seed + fi * 3, base=6)
    inside = (np.abs(dx) < 1.0 + (n - 0.5) * 0.25) & (u < 0.99)
    # bands that wrap the funnel, shifting sideways frame by frame
    bands = 0.5 + 0.5 * np.sin(u * 28 + dx * 2.2 * (1 + u) - fi * math.pi / 2)
    t = np.clip(0.2 + 0.55 * (1 - bands) + 0.22 * np.abs(dx) + (n - 0.5) * 0.3, 0, 1)
    col = ramp('fog', t)
    col = col * 0.55 + ramp('dust', np.clip(0.3 + 0.5 * bands, 0, 1)) * 0.45
    img = np.zeros((h, w, 4), np.uint8)
    img[..., :3] = np.clip(col, 0, 255).astype(np.uint8)
    img[..., 3] = np.where(inside, 255, 0)
    return Image.fromarray(img, 'RGBA')


def gust(w, h, p, seed=41):
    """the tornado dying down: a thinning ring of wind"""
    im = blob(w, h, 'fog', rx=(w / 2 - 1) * (0.7 + 0.3 * p), ry=(h / 2 - 1) * (0.7 + 0.3 * p), seed=seed + int(p * 5), rough=0.8, core=1.0)
    a = np.asarray(im).copy()
    n = fbm(w, h, seed + 7 + int(p * 9), base=6)
    a[..., 3] = np.where(n < 0.6 - 0.35 * p, a[..., 3], 0)
    return Image.fromarray(a, 'RGBA')


def zap(w, h, fi, seed=51):
    """an arc lightning bolt: a short jagged line, a different one on each frame"""
    from PIL import ImageDraw
    rs = np.random.RandomState(seed + fi * 13)
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pts = [(w * 0.1, h * 0.9)]
    n = 6
    for k in range(1, n):
        pts.append((w * (0.1 + 0.8 * k / n) + rs.uniform(-0.18, 0.18) * w, h * (0.9 - 0.8 * k / n) + rs.uniform(-0.18, 0.18) * h))
    pts.append((w * 0.9, h * 0.1))
    lw = max(2, int(min(w, h) * 0.2))
    d.line(pts, fill=(120, 205, 255, 255), width=lw, joint='curve')
    d.line(pts, fill=(250, 254, 255, 255), width=max(1, lw // 3), joint='curve')
    return img


def shock_ring(w, h, p):
    """the thunderclap's shock ring, p 0..1 growing and thinning"""
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = (xx + 0.5 - w / 2) / (w / 2), (yy + 0.5 - h / 2) / (h / 2)
    r = np.sqrt(dx * dx + dy * dy)
    rad = 0.35 + 0.62 * p
    thick = 0.16 - 0.09 * p
    n = fbm(w, h, 61 + int(p * 10), base=8)
    d = np.abs(r - rad) / thick + (n - 0.5) * 0.5
    inner = (r < rad) & (p < 0.5)
    col = ramp('plasma', np.clip(d * 0.9, 0, 1))
    img = np.zeros((h, w, 4), np.uint8)
    img[..., :3] = col.astype(np.uint8)
    a = (d < 1.0)
    img[..., 3] = np.where(a, 255, 0)
    # a flash in the middle of the first frames
    core = (r < 0.5 * (1 - p)) & (p < 0.45)
    img[core, :3] = (235, 245, 255)
    img[core, 3] = 255
    return Image.fromarray(img, 'RGBA')
