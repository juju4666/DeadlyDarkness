"""art_tex.py - wall textures, floors and the sky, drawn procedurally (all tile seamlessly)."""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def pnoise(w, h, seed, n=6, maxf=5):
    """periodic noise in 0..1: a sum of sines whose frequencies are whole numbers, so it tiles"""
    rs = np.random.RandomState(seed)
    yy, xx = np.mgrid[0:h, 0:w]
    out = np.zeros((h, w))
    tot = 0
    for i in range(n):
        fx, fy = rs.randint(0, maxf + 1), rs.randint(0, maxf + 1)
        if fx == 0 and fy == 0:
            fx = 1
        a = 1.0 / (1 + 0.5 * (fx + fy))
        out += a * np.sin(2 * math.pi * (fx * xx / w + fy * yy / h) + rs.uniform(0, 6.28))
        tot += a
    return 0.5 + 0.5 * out / tot


def grain(w, h, seed, amp=0.08):
    rs = np.random.RandomState(seed)
    return 1.0 + (rs.rand(h, w) - 0.5) * 2 * amp


def to_img(arr):
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def bevel(mask_h, light=0.35):
    """shade by the height field's slope: light from the top left"""
    gy, gx = np.gradient(mask_h)
    return 1.0 + light * (-(gx + gy))


def bricks(w, h, rows, cols, base, mortar, var=0.14, seed=1, scale=1.0, mott=0.2, bev=0.5, offset=True):
    """a brick wall: `rows` courses of `cols` bricks (the odd courses shifted half a brick)"""
    rs = np.random.RandomState(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    bh = h / rows
    bw = w / cols
    row = np.floor(yy / bh).astype(int)
    shift = np.where(row % 2 == 1, bw / 2 if offset else 0, 0)
    xs = (xx + shift) % w
    col = np.floor(xs / bw).astype(int)
    fx = (xs - col * bw) / bw
    fy = (yy - row * bh) / bh
    cid = (row * 977 + col * 131 + seed * 7) % 1000003
    tint = np.array([((cid * k) % 997) / 997.0 for k in (13, 29, 47)])
    r1 = ((cid * 7919) % 1000) / 1000.0 - 0.5
    mg = max(1.2, 0.07 * min(bh, bw))
    ex = np.minimum(fx * bw, (1 - fx) * bw)
    ey = np.minimum(fy * bh, (1 - fy) * bh)
    e = np.minimum(ex, ey)
    inside = e > mg * 0.55
    height = np.clip((e - mg * 0.55) / (mg * 2.5), 0, 1)
    n = pnoise(w, h, seed + 3, 7, 9)
    lum = 1.0 + r1 * var * 2 + (n - 0.5) * mott * 2
    col3 = np.zeros((h, w, 3))
    for c in range(3):
        col3[..., c] = base[c] * lum * (1 + (tint[c % 3] - 0.5) * 0.12)
    sh = bevel(height * 3, bev * 0.9)
    col3 *= np.clip(sh, 0.6, 1.4)[..., None]
    mort = np.array(mortar, float)[None, None, :] * (0.85 + 0.3 * pnoise(w, h, seed + 9, 5, 12))[..., None]
    col3 = np.where(inside[..., None], col3, mort)
    col3 *= grain(w, h, seed + 5, 0.05)[..., None]
    return to_img(col3)


def blocks(w, h, rows, base, mortar, seed=1, jitter=0.25):
    """big stone blocks with irregular joints"""
    rs = np.random.RandomState(seed)
    img = np.zeros((h, w, 3))
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    bh = h / rows
    n = pnoise(w, h, seed + 2, 8, 6)
    mort = np.zeros((h, w), bool)
    lumv = np.ones((h, w))
    height = np.zeros((h, w))
    for r in range(rows):
        y0, y1 = int(round(r * bh)), int(round((r + 1) * bh))
        nb = rs.randint(2, 4)
        cuts = sorted(rs.uniform(0.1, 0.9, nb - 1) * w)
        cuts = [0] + cuts + [w]
        for i in range(len(cuts) - 1):
            x0, x1 = int(cuts[i]), int(cuts[i + 1])
            lumv[y0:y1, x0:x1] = 1.0 + rs.uniform(-0.13, 0.13)
            ey = np.minimum(yy[y0:y1, x0:x1] - y0, y1 - 1 - yy[y0:y1, x0:x1])
            ex = np.minimum(xx[y0:y1, x0:x1] - x0, x1 - 1 - xx[y0:y1, x0:x1])
            e = np.minimum(ex, ey)
            height[y0:y1, x0:x1] = np.clip((e - 1.2) / 5.0, 0, 1)
    mort = height <= 0
    for c in range(3):
        img[..., c] = base[c] * lumv * (0.78 + 0.44 * n)
    img *= np.clip(bevel(height * 3, 0.5), 0.6, 1.4)[..., None]
    img = np.where(mort[..., None], np.array(mortar, float)[None, None, :], img)
    img *= grain(w, h, seed + 1, 0.07)[..., None]
    return to_img(img)


def plates(w, h, nx, ny, base, edge, seed=1, rivets=True, scratches=0.5, stripe=None):
    """riveted metal panels"""
    rs = np.random.RandomState(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    pw, ph = w / nx, h / ny
    fx = (xx % pw) / pw
    fy = (yy % ph) / ph
    ex = np.minimum(xx % pw, pw - 1 - xx % pw)
    ey = np.minimum(yy % ph, ph - 1 - yy % ph)
    e = np.minimum(ex, ey)
    height = np.clip(e / max(2.0, 0.06 * min(pw, ph)), 0, 1)
    pid = (np.floor(yy / ph) * nx + np.floor(xx / pw)).astype(int)
    lum = 1.0 + (((pid * 7919 + seed) % 100) / 100.0 - 0.5) * 0.22
    n = pnoise(w, h, seed + 1, 6, 10)
    col = np.zeros((h, w, 3))
    for c in range(3):
        col[..., c] = base[c] * lum * (0.9 + 0.2 * n)
    # brushed streaks
    rs2 = np.random.RandomState(seed + 4)
    streak = np.repeat(rs2.rand(h, 1), w, axis=1)
    col *= (1 + (streak - 0.5) * 0.06 * scratches)[..., None]
    col *= np.clip(bevel(height * 3, 0.7), 0.55, 1.45)[..., None]
    col = np.where((e < 1.0)[..., None], np.array(edge, float)[None, None, :], col)
    if rivets:
        d = ImageDraw.Draw(Image.new('L', (1, 1)))
        out = to_img(col)
        dr = ImageDraw.Draw(out)
        rr = max(1.2, 0.035 * min(pw, ph))
        for iy in range(ny):
            for ix in range(nx):
                for (ox, oy) in ((0.14, 0.16), (0.86, 0.16), (0.14, 0.84), (0.86, 0.84)):
                    cx, cy = ix * pw + ox * pw, iy * ph + oy * ph
                    dr.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=tuple(int(c * 0.55) for c in base) + (255,))
                    dr.ellipse([cx - rr * 0.8 - 0.5, cy - rr * 0.8 - 0.5, cx + rr * 0.5, cy + rr * 0.5], fill=tuple(min(255, int(c * 1.4)) for c in base) + (255,))
        col = np.asarray(out.convert('RGB'), float)
    if stripe:
        pass
    return to_img(col)


def hazard(w, h, c1, c2, seed=1, bands=8):
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    t = ((xx + yy) / (w / bands)) % 2
    m = (t < 1)[..., None]
    n = pnoise(w, h, seed, 6, 9)
    col = np.where(m, np.array(c1, float), np.array(c2, float)) * (0.85 + 0.3 * n)[..., None]
    e = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy))
    col *= np.clip(0.6 + 0.4 * np.clip(e / 3.0, 0, 1), 0, 1)[..., None]
    return to_img(col * grain(w, h, seed + 1, 0.06)[..., None])


def planks(w, h, n, base, seed=1):
    rs = np.random.RandomState(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    pw = w / n
    pid = np.floor(xx / pw).astype(int)
    fx = (xx % pw) / pw
    lum = np.array([rs.uniform(0.78, 1.12) for _ in range(n)])[pid]
    g = pnoise(w, h, seed + 2, 9, 3)
    ring = 0.5 + 0.5 * np.sin((fx * 14 + g * 9 + pid * 3.1) * 2.2)
    col = np.zeros((h, w, 3))
    for c in range(3):
        col[..., c] = base[c] * lum * (0.82 + 0.3 * ring)
    e = np.minimum(xx % pw, pw - 1 - xx % pw)
    col *= np.clip(0.4 + e / 3.0, 0, 1.15)[..., None]
    # nail heads
    out = to_img(col)
    dr = ImageDraw.Draw(out)
    for i in range(n):
        for y in (0.07, 0.5, 0.93):
            cx, cy = i * pw + pw / 2, y * h
            dr.ellipse([cx - 1.4, cy - 1.4, cx + 1.4, cy + 1.4], fill=(60, 56, 54, 255))
    return out


def tiles(w, h, n, base, grout, seed=1, checker=0.0):
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    tw, th = w / n, h / n
    ex = np.minimum(xx % tw, tw - 1 - xx % tw)
    ey = np.minimum(yy % th, th - 1 - yy % th)
    e = np.minimum(ex, ey)
    height = np.clip((e - 0.8) / 3.0, 0, 1)
    tid = (np.floor(yy / th) * n + np.floor(xx / tw)).astype(int)
    lum = 1.0 + (((tid * 7919 + seed * 31) % 100) / 100.0 - 0.5) * 0.18
    if checker:
        lum = lum * (1 + checker * (((np.floor(xx / tw) + np.floor(yy / th)) % 2) - 0.5))
    n1 = pnoise(w, h, seed, 7, 8)
    col = np.zeros((h, w, 3))
    for c in range(3):
        col[..., c] = base[c] * lum * (0.84 + 0.32 * n1)
    col *= np.clip(bevel(height * 3, 0.5), 0.65, 1.35)[..., None]
    col = np.where((height <= 0)[..., None], np.array(grout, float)[None, None, :] * (0.9 + 0.2 * n1)[..., None], col)
    return to_img(col * grain(w, h, seed + 3, 0.05)[..., None])


def liquid(w, h, c0, c1, seed=1, waves=6, sparkle=True):
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    rs = np.random.RandomState(seed)
    v = np.zeros((h, w))
    for i in range(waves):
        fx, fy = rs.randint(1, 5), rs.randint(1, 5)
        v += np.sin(2 * math.pi * (fx * xx / w + fy * yy / h) + rs.uniform(0, 6.28)) * np.sin(2 * math.pi * (rs.randint(1, 4) * xx / w - rs.randint(1, 4) * yy / h) + rs.uniform(0, 6.28))
    v = (v / waves + 1) / 2
    v = np.clip(v, 0, 1)
    col = np.array(c0, float)[None, None, :] * (1 - v[..., None]) + np.array(c1, float)[None, None, :] * v[..., None]
    hl = np.clip((v - 0.72) / 0.28, 0, 1)
    col = col + hl[..., None] * np.array([70, 70, 70])
    return to_img(col)


def rock_floor(w, h, base, seed=1):
    n = pnoise(w, h, seed, 9, 10)
    n2 = pnoise(w, h, seed + 5, 6, 3)
    col = np.zeros((h, w, 3))
    for c in range(3):
        col[..., c] = base[c] * (0.7 + 0.5 * n) * (0.9 + 0.2 * n2)
    return to_img(col * grain(w, h, seed + 2, 0.07)[..., None])


def door(w, h, seed=1):
    img = plates(w, h, 1, 4, (110, 112, 122), (46, 48, 56), seed, True, 0.8)
    d = ImageDraw.Draw(img)
    d.rectangle([w * 0.45, 0, w * 0.55, h], fill=(190, 150, 40, 255))
    for k in range(0, h, 8):
        d.line([w * 0.45, k, w * 0.55, k + 4], fill=(40, 40, 40, 255), width=max(1, w // 64))
    d.rectangle([0, 0, w - 1, h - 1], outline=(40, 42, 50, 255))
    return img


def switch(w, h, on, label_col, seed=1):
    img = plates(w, h, 1, 1, (88, 92, 104), (36, 38, 46), seed, True, 0.5)
    d = ImageDraw.Draw(img)
    # a recessed housing with a lever and a lamp
    d.rounded_rectangle([w * 0.28, h * 0.30, w * 0.72, h * 0.70], radius=w * 0.05, fill=(34, 36, 42, 255), outline=(150, 154, 168, 255), width=max(1, w // 40))
    ly = h * 0.58 if on else h * 0.42
    d.rectangle([w * 0.46, h * 0.36, w * 0.54, h * 0.64], fill=(20, 22, 26, 255))
    d.ellipse([w * 0.39, ly - h * 0.05, w * 0.61, ly + h * 0.05], fill=(220, 224, 232, 255), outline=(40, 40, 46, 255))
    lamp = tuple(label_col) if on else tuple(int(c * 0.28) for c in label_col)
    d.ellipse([w * 0.40, h * 0.12, w * 0.60, h * 0.12 + w * 0.2], fill=lamp + (255,), outline=(20, 20, 24, 255))
    return img


def wall_label(img, text, size_px):
    """a small enamel plaque with the map number (so a map can be told by its walls)"""
    d = ImageDraw.Draw(img)
    w, h = img.size
    pw, ph = int(w * 0.46), int(h * 0.2)
    x0, y0 = (w - pw) // 2, int(h * 0.38)
    d.rounded_rectangle([x0, y0, x0 + pw, y0 + ph], radius=ph // 5, fill=(24, 26, 32, 255), outline=(220, 200, 120, 255), width=max(1, w // 64))
    try:
        fnt = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', int(ph * 0.62))
    except Exception:
        fnt = ImageFont.load_default()
    bb = d.textbbox((0, 0), text, font=fnt)
    d.text((x0 + (pw - (bb[2] - bb[0])) // 2 - bb[0], y0 + (ph - (bb[3] - bb[1])) // 2 - bb[1]), text, fill=(236, 220, 150, 255), font=fnt)
    return img


def sky(w, h, seed=3):
    """a dusk sky: a gradient, soft cloud bands, a moon and a ragged line of far mountains; tiles sideways"""
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    t = yy / h
    top, mid, bot = np.array([18, 22, 64]), np.array([96, 70, 130]), np.array([232, 150, 110])
    col = np.where((t < 0.55)[..., None], top + (mid - top) * (t / 0.55)[..., None], mid + (bot - mid) * ((t - 0.55) / 0.45)[..., None])
    n = pnoise(w, h, seed, 8, 5)
    n2 = pnoise(w, h, seed + 1, 6, 12)
    cloud = np.clip((n * 0.7 + n2 * 0.3 - 0.52) * 3.2, 0, 1) * np.clip(1.3 - t * 1.4, 0, 1)
    col = col * (1 - cloud[..., None] * 0.6) + np.array([220, 170, 190])[None, None, :] * cloud[..., None] * 0.4
    rs = np.random.RandomState(seed)
    for _ in range(int(w * h / 900)):
        x, y = rs.randint(0, w), rs.randint(0, int(h * 0.45))
        col[y, x] = col[y, x] * 0.4 + 255 * 0.6 * (1 - y / (h * 0.5))
    # moon
    mx, my, mr = w * 0.3, h * 0.24, h * 0.07
    d2 = np.sqrt((xx - mx) ** 2 + (yy - my) ** 2)
    col = np.where((d2 < mr)[..., None], np.array([248, 242, 220]) * (0.9 + 0.1 * pnoise(w, h, 9, 5, 9))[..., None], col)
    col = col + np.exp(-((d2 - mr) / (mr * 1.6)) ** 2)[..., None] * np.array([40, 36, 30]) * (d2 > mr)[..., None]
    # mountains: two layers
    for layer, (base_y, amp, c) in enumerate(((0.74, 0.10, (70, 54, 100)), (0.86, 0.09, (34, 28, 56)))):
        prof = np.zeros(w)
        for f, a in ((2, 1.0), (5, 0.5), (11, 0.25), (23, 0.12)):
            ph = np.random.RandomState(seed + layer * 10 + f).uniform(0, 6.28)
            prof += a * np.sin(2 * math.pi * f * np.arange(w) / w + ph)
        prof = base_y * h + prof / 1.9 * amp * h
        mask = yy > prof[None, :]
        col = np.where(mask[..., None], np.array(c, float)[None, None, :] * (0.9 + 0.2 * pnoise(w, h, seed + layer, 6, 14))[..., None], col)
    return to_img(col)
