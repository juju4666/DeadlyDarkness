"""art_hud.py - status bar, faces, key icons, title and backdrop screens, drawn.  apply(new) overwrites those graphics (same names and sizes)."""
import math, os, io
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from art_render import Fig, mat, render
from art_monsters import M
from art_monsters2 import GLOW
import art_tex as T

FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'


def font(sz):
    try:
        return ImageFont.truetype(FONT, sz)
    except Exception:
        return ImageFont.load_default()


def metal(w, h, seed=1):
    return T.plates(w, h, 1, 1, (74, 78, 90), (30, 32, 38), seed, rivets=False, scratches=0.6)


def inset(img, box, fill=(10, 10, 14), light=(120, 124, 140), dark=(18, 18, 22)):
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = box
    d.rectangle([x0, y0, x1, y1], fill=fill + (255,))
    d.line([x0 - 1, y1 + 1, x1 + 1, y1 + 1], fill=light + (255,))
    d.line([x1 + 1, y0 - 1, x1 + 1, y1 + 1], fill=light + (255,))
    d.line([x0 - 1, y0 - 1, x1 + 1, y0 - 1], fill=dark + (255,))
    d.line([x0 - 1, y0 - 1, x0 - 1, y1 + 1], fill=dark + (255,))


def stbar():
    img = metal(320, 32)
    for box in ((2, 3, 100, 28), (106, 3, 139, 28), (145, 2, 175, 29), (181, 3, 231, 28), (236, 3, 268, 28), (272, 3, 317, 28)):
        inset(img, box)
    d = ImageDraw.Draw(img)
    for x in (4, 316, 103, 142, 178, 233, 270):
        d.ellipse([x - 1, 1, x + 1, 3], fill=(150, 154, 168, 255))
        d.ellipse([x - 1, 28, x + 1, 30], fill=(150, 154, 168, 255))
    return img


def starms():
    img = Image.new('RGBA', (38, 32), (0, 0, 0, 0))
    base = metal(38, 32, 2)
    img.paste(base, (0, 0))
    inset(img, (2, 2, 35, 29), fill=(14, 14, 20))
    d = ImageDraw.Draw(img)
    d.line([19, 3, 19, 28], fill=(60, 62, 76, 255))
    for y in (11, 20):
        d.line([3, y, 34, y], fill=(60, 62, 76, 255))
    return img


# ---- faces: a 3D head drawn small
# the three characters (all women): skin, iris, lips, hair
CHARS = {
    'S': dict(skin=(98, 62, 42), nose=(86, 53, 36), iris=(56, 34, 22), lips=(150, 56, 66), hair=(22, 18, 18), brow=(26, 18, 16), eye_h=1.0),
    'W': dict(skin=(244, 214, 192), nose=(236, 198, 176), iris=(70, 130, 200), lips=(200, 66, 82), hair=(232, 198, 108), brow=(190, 150, 80), eye_h=1.05),
    'X': dict(skin=(222, 182, 148), nose=(212, 168, 134), iris=(26, 18, 16), lips=(176, 70, 78), hair=(20, 18, 24), brow=(16, 14, 18), eye_h=0.72),
}


def head_fig(health, look, expr, ch='S'):
    """health 0 (best) .. 4 (worst); look -1/0/1; expr: normal, ouch, evil, kill, god, dead; ch: S soldier, W witch, X swordsman"""
    c = CHARS[ch]
    f = Fig()
    skin = M(c['skin'], noise=0.04) if expr != 'god' else M((255, 214, 90), spec=0.6, glow=0.3, noise=0.03)
    if expr == 'dead':
        skin = M(tuple(int(v * 0.8) for v in c['skin']), noise=0.06)
    hair = M(c['hair'], spec=0.25, shin=18, noise=0.1)
    # hair behind and around the face, then the face (a softer, narrower jaw)
    if ch == 'W':
        f.ell((0, -2.0, -2.5), (8.6, 6.0, 11.5), hair)                       # long hair falling past the shoulders
    elif ch == 'X':
        f.ell((0, -2.0, -1.0), (8.0, 6.0, 10.0), hair)                       # straight black hair to the jaw
    else:
        f.ell((0, -0.8, 1.0), (7.6, 6.6, 8.0), hair)                         # short curls
    f.ell((0, 0, 0), (6.7, 7.2, 8.4), skin)
    f.ell((0, 0.8, -4.4), (5.0, 5.6, 4.2), skin)                              # chin: narrower than the cheeks
    if ch == 'S':
        f.ell((0, 0.5, 5.0), (7.0, 6.6, 3.2), hair)                          # curls over the forehead
        f.box((0, 6.2, 4.0), (6.8, 0.8, 0.8), M((56, 130, 148), spec=0.35, noise=0.04))   # a teal helmet band
    elif ch == 'W':
        f.ell((0, 1.0, 5.2), (7.4, 6.4, 2.6), hair)                          # fringe
        f.ell((0, 1.5, 7.4), (11.0, 10.0, 1.1), M((54, 34, 76), noise=0.08))  # hat brim
        f.ell((0, 1.5, 9.2), (6.4, 6.0, 2.6), M((54, 34, 76), noise=0.08))
    else:
        f.ell((0, 1.2, 5.4), (6.9, 6.2, 2.4), hair)                          # straight fringe
        f.box((0, 6.3, 3.4), (6.8, 0.8, 0.7), M((200, 40, 40), noise=0.02))   # red headband
    f.ell((0, 6.0, -0.4), (1.1, 1.5, 1.7), M(c['nose'], noise=0.04))        # nose
    ex = look * 0.7
    eh = c['eye_h']
    if expr == 'dead':
        for s in (-1, 1):
            f.box((s * 2.9, 6.9, 1.5), (1.4, 0.4, 0.3), M((20, 14, 14)), rot=(0, 0, 40 * s))
            f.box((s * 2.9, 6.9, 1.5), (1.4, 0.4, 0.3), M((20, 14, 14)), rot=(0, 0, -40 * s))
    else:
        for s in (-1, 1):
            tilt = -s * (8 if ch == 'X' else 0)
            f.ell((s * 3.0, 6.3, 1.7), (1.5, 0.7, (1.1 if expr != 'ouch' else 0.5) * eh), M((244, 244, 240), noise=0), rot=(0, 0, tilt))
            f.sph((s * 3.0 + ex, 6.9, 1.7), 0.62 if ch != 'X' else 0.5, GLOW(230, 40, 30) if expr in ('evil', 'god') else M(c['iris'], noise=0))
            f.box((s * 3.0, 6.9, 2.0 + 0.8 * eh), (1.7, 0.3, 0.22), M((16, 12, 14), noise=0), rot=(0, 0, tilt))     # lashes / lid
            f.box((s * 3.0, 6.6, 3.5 + (0.2 if expr == 'evil' else 0)), (1.6, 0.3, 0.25), M(c['brow'], noise=0),
                  rot=(0, 0, -s * (14 if expr in ('kill', 'evil') else 0) + s * (-8 if expr == 'ouch' else 0) + (tilt * 0.5)))
    lips = M(c['lips'], spec=0.3, noise=0.03)
    mouth_w = 2.5 if expr != 'ouch' else 1.4
    if expr in ('evil', 'kill'):
        f.ell((0, 6.4, -3.2), (2.9, 0.7, 1.0), M((240, 236, 226)))
        f.ell((0, 6.5, -4.0), (3.0, 0.6, 0.55), lips)
    elif expr == 'ouch':
        f.ell((0, 6.4, -3.4), (1.5, 0.7, 1.7), M((70, 14, 20)))
        f.ell((0, 6.5, -3.4), (1.9, 0.5, 2.0), lips)
    elif expr == 'dead':
        f.ell((0, 6.4, -3.4), (2.4, 0.6, 0.6), M((60, 20, 20)))
    else:
        f.ell((0, 6.4, -3.3), (mouth_w, 0.6, 0.7), lips)               # fuller lips
        f.ell((0, 6.55, -3.25), (mouth_w * 0.8, 0.5, 0.18), M(tuple(int(v * 0.55) for v in c['lips'])))
    blood = M((190, 20, 24), spec=0.3, noise=0.05)
    spots = [(3.5, 6.0, 4.0, 1.3), (-4.0, 5.6, -1.0, 1.5), (1.0, 6.9, -5.0, 1.2), (-2.0, 6.8, 5.4, 1.0), (4.6, 5.2, -3.4, 1.4)]
    for k in range(min(health, 4) + (2 if expr in ('dead',) else 0)):
        x, y, z, r = spots[k % len(spots)]
        f.ell((x, y, z), (r * (1 + 0.3 * k), 0.5, r * 0.9 * (1 + 0.2 * k)), blood)
    return f


def face_img(health, look, expr, w=35, h=32, bg=None, ch='S'):
    f = head_fig(health, look, expr, ch)
    yaw = {'tl': 38, 'tr': -38}.get(expr, 0)
    img, ox, oy = render(f, yaw if expr in ('tl', 'tr') else (look * -14), ppu=3.1, ss=4, elev=0, outline=1.2)
    img = img.resize((max(1, int(img.width * 1.0)), max(1, int(img.height * 1.0))), Image.LANCZOS)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    s = min((w - 2) / img.width, (h - 1) / img.height, 1.2)
    im2 = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.LANCZOS)
    a = np.asarray(im2).copy()
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    im2 = Image.fromarray(a, 'RGBA')
    out.paste(im2, ((w - im2.width) // 2, h - im2.height), im2)
    if bg is not None:
        base = Image.new('RGBA', (w, h), bg + (255,))
        base.paste(out, (0, 0), out)
        return base
    return out


FACE_BG = (30, 32, 44)


def all_faces(gr, ch='S', prefix='STF'):
    """name -> image for every status-face lump of one character"""
    out = {}
    P = prefix
    for lvl in range(5):
        for k, look in enumerate((-1, 0, 1)):
            out['%sST%d%d' % (P, lvl, k)] = face_img(lvl, look, 'normal', bg=FACE_BG, ch=ch)
        out['%sTR%d0' % (P, lvl)] = face_img(lvl, 0, 'tr', bg=FACE_BG, ch=ch)
        out['%sTL%d0' % (P, lvl)] = face_img(lvl, 0, 'tl', bg=FACE_BG, ch=ch)
        out['%sOUCH%d' % (P, lvl)] = face_img(lvl, 0, 'ouch', bg=FACE_BG, ch=ch)
        out['%sEVL%d' % (P, lvl)] = face_img(lvl, 0, 'evil', bg=FACE_BG, ch=ch)
        out['%sKILL%d' % (P, lvl)] = face_img(lvl, 0, 'kill', bg=FACE_BG, ch=ch)
    out['%sGOD0' % P] = face_img(0, 0, 'god', bg=(60, 50, 10), ch=ch)
    out['%sDEAD0' % P] = face_img(4, 0, 'dead', bg=FACE_BG, ch=ch)
    return out


CLASS_FACES = {'S': 'SLD', 'W': 'WCH', 'X': 'SWD'}      # BGCLASS "face <prefix>" for soldier, witch, swordsman


def player_bg(rgb):
    img = Image.new('RGBA', (35, 33), rgb + (255,))
    a = np.asarray(img).astype(float)
    yy = np.linspace(1.0, 0.55, 33)[:, None, None]
    a[..., :3] *= yy
    img = Image.fromarray(a.astype(np.uint8), 'RGBA')
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 34, 32], outline=tuple(int(c * 0.5) for c in rgb) + (255,))
    return img


def key_icon(rgb, size=8):
    S = 8
    img = Image.new('RGBA', (size * S, size * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    col = rgb + (255,)
    dark = tuple(int(c * 0.45) for c in rgb) + (255,)
    n = size * S
    d.ellipse([n * 0.10, n * 0.06, n * 0.55, n * 0.51], fill=col, outline=dark, width=S // 2)
    d.ellipse([n * 0.25, n * 0.2, n * 0.40, n * 0.35], fill=(0, 0, 0, 0))
    d.line([n * 0.45, n * 0.45, n * 0.9, n * 0.9], fill=dark, width=int(n * 0.2))
    d.line([n * 0.45, n * 0.45, n * 0.9, n * 0.9], fill=col, width=int(n * 0.12))
    d.line([n * 0.72, n * 0.72, n * 0.9, n * 0.54], fill=col, width=int(n * 0.1))
    return img.resize((size, size), Image.LANCZOS)


def skull_icon(w, h, frame):
    f = Fig()
    bone = M((232, 220, 196), noise=0.08)
    f.ell((0, 0, 0), (6, 6.2, 6.4), bone)
    f.ell((0, 3, -4.8), (3.8, 3, 2.6), bone)
    f.sph((-2.3, 5.0, 0.8), 1.5, M((20, 8, 8)))
    f.sph((2.3, 5.0, 0.8), 1.5, M((20, 8, 8)))
    f.sph((-2.3, 5.8, 0.8), 0.7, GLOW(255, 60 + 120 * frame, 40))
    f.sph((2.3, 5.8, 0.8), 0.7, GLOW(255, 60 + 120 * frame, 40))
    img, _, _ = render(f, 0, ppu=3, ss=4, elev=0, outline=1.2)
    s = min(w / img.width, h / img.height)
    im2 = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.LANCZOS)
    a = np.asarray(im2).copy()
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    im2 = Image.fromarray(a, 'RGBA')
    out.paste(im2, ((w - im2.width) // 2, (h - im2.height) // 2), im2)
    return out


# ---- full-screen pictures
def gradient(w, h, top, bottom):
    t = np.linspace(0, 1, h)[:, None, None]
    return np.broadcast_to(np.array(top, float)[None, None, :] * (1 - t) + np.array(bottom, float)[None, None, :] * t, (h, w, 3)).copy()


def title_text(img, text, y, size, fill_top=(255, 236, 150), fill_bot=(200, 120, 30), outline=(30, 10, 10), sub=None):
    w, h = img.size
    f = font(size)
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    bb = d.textbbox((0, 0), text, font=f)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    x = (w - tw) // 2 - bb[0]
    for dx in range(-2, 3):
        for dy in range(-2, 3):
            if dx or dy:
                d.text((x + dx, y + dy), text, font=f, fill=outline + (255,))
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((x, y), text, font=f, fill=255)
    g = Image.fromarray(gradient(w, h, fill_top, fill_bot).astype(np.uint8), 'RGB')
    layer.paste(g, (0, 0), mask)
    img.alpha_composite(layer)
    return img


def city(w=320, h=200, seed=1, night=True):
    rs = np.random.RandomState(seed)
    arr = gradient(w, h, (12, 14, 40), (150, 70, 90)) if night else gradient(w, h, (60, 90, 160), (240, 190, 130))
    img = Image.fromarray(arr.astype(np.uint8), 'RGB').convert('RGBA')
    d = ImageDraw.Draw(img)
    for _ in range(90):
        x, y = rs.randint(0, w), rs.randint(0, int(h * 0.5))
        c = int(150 + rs.rand() * 100)
        d.point((x, y), fill=(c, c, c, 255))
    mx, my = int(w * 0.72), int(h * 0.27)
    for r, a in ((26, 40), (20, 80), (15, 255)):
        d.ellipse([mx - r, my - r, mx + r, my + r], fill=(250, 244, 220, a) if r == 15 else (240, 230, 200, a))
    for layer, (base, col, wmin, wmax, hmin, hmax, lit) in enumerate(((0.62, (40, 30, 70), 12, 26, 30, 70, 0.0), (0.74, (24, 18, 44), 16, 34, 40, 100, 0.25), (0.9, (10, 8, 20), 20, 44, 50, 120, 0.45))):
        x = -rs.randint(0, 20)
        while x < w:
            bw = rs.randint(wmin, wmax)
            bh = rs.randint(hmin, hmax)
            top = int(h * base) - bh + int(h * 0.12)
            d.rectangle([x, top, x + bw, h], fill=col + (255,))
            if rs.rand() < 0.4:
                d.polygon([(x, top), (x + bw / 2, top - rs.randint(8, 20)), (x + bw, top)], fill=col + (255,))
            for wy in range(top + 5, h - 5, 7):
                for wx in range(x + 3, x + bw - 3, 6):
                    if rs.rand() < lit:
                        d.rectangle([wx, wy, wx + 2, wy + 3], fill=(250, 200 + rs.randint(0, 40), 90, 255))
            x += bw + rs.randint(0, 3)
    return img


def parchment(w=320, h=200, seed=1, dark=False):
    n = T.pnoise(w, h, seed, 9, 7)
    n2 = T.pnoise(w, h, seed + 3, 6, 3)
    base = np.array([214, 190, 140] if not dark else [90, 70, 52], float)
    arr = base[None, None, :] * (0.78 + 0.3 * n[..., None]) * (0.92 + 0.12 * n2[..., None])
    yy, xx = np.mgrid[0:h, 0:w]
    e = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)) / 40.0
    arr *= np.clip(0.55 + 0.45 * np.clip(e, 0, 1), 0, 1)[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')


def starfield(w=320, h=200, seed=2):
    rs = np.random.RandomState(seed)
    arr = gradient(w, h, (6, 6, 24), (40, 20, 60))
    img = Image.fromarray(arr.astype(np.uint8), 'RGB').convert('RGBA')
    d = ImageDraw.Draw(img)
    for _ in range(260):
        x, y = rs.randint(0, w), rs.randint(0, h)
        c = int(120 + rs.rand() * 135)
        d.point((x, y), fill=(c, c, min(255, c + 20), 255))
    d.ellipse([200, 90, 380, 270], fill=(60, 80, 130, 255), outline=(120, 150, 210, 255), width=2)
    d.ellipse([220, 100, 330, 200], fill=(80, 110, 160, 255))
    return img


def title_screens():
    out = {}
    t = city(seed=3)
    t = title_text(t, 'BEGOTTEN', 38, 40)
    ImageDraw.Draw(t).text((160 - 55, 92), 'PLACEHOLDER ART', font=font(10), fill=(230, 200, 200, 255))
    out['TITLEPIC'] = t
    c = parchment(seed=4)
    c = title_text(c, 'CREDITS', 14, 24, (90, 50, 20), (50, 20, 10), (230, 210, 160))
    d = ImageDraw.Draw(c)
    for i, line in enumerate(('A GAME ON A MODIFIED DSDA-DOOM ENGINE', 'ART, SOUND AND MUSIC: PLACEHOLDER', 'REPLACE THIS SCREEN WITH THE REAL CREDITS')):
        d.text((24, 60 + i * 20), line, font=font(10), fill=(60, 36, 20, 255))
    out['CREDIT'] = c
    hp = parchment(seed=5)
    hp = title_text(hp, 'HOW TO PLAY', 10, 22, (90, 50, 20), (50, 20, 10), (230, 210, 160))
    d = ImageDraw.Draw(hp)
    for i, line in enumerate(('MOVE: WASD / ARROWS     FIRE: CTRL / MOUSE 1', 'ALT FIRE: MOUSE 2      PARRY: SEE CONTROLS', 'USE: SPACE / E        JUMP: SEE CONTROLS', 'PLACEHOLDER HELP SCREEN')):
        d.text((20, 56 + i * 20), line, font=font(10), fill=(60, 36, 20, 255))
    out['HELP'] = hp
    ip = parchment(seed=6, dark=True)
    d = ImageDraw.Draw(ip)
    for x in range(0, 320, 20):
        d.line([x, 0, x, 199], fill=(60, 46, 34, 255))
    for y in range(0, 200, 20):
        d.line([0, y, 319, y], fill=(60, 46, 34, 255))
    d.text((90, 90), 'LEVEL MAP', font=font(20), fill=(210, 180, 120, 255))
    out['INTERPIC'] = ip
    out['BOSSBACK'] = starfield()
    return out


def menu_logo(w, h):
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    f = font(int(h * 0.62))
    for sz in range(int(h * 0.62), 6, -1):
        f = font(sz)
        bb = ImageDraw.Draw(img).textbbox((0, 0), 'BEGOTTEN', font=f)
        if bb[2] - bb[0] <= w - 4:
            break
    layer = title_text(Image.new('RGBA', (w, h), (0, 0, 0, 0)), 'BEGOTTEN', (h - (bb[3] - bb[1])) // 2 - bb[1], sz)
    a = np.asarray(layer).copy()
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    return Image.fromarray(a, 'RGBA')


KEYRGB = [(40, 90, 255), (245, 220, 40), (240, 40, 40), (50, 210, 70), (50, 225, 235), (230, 60, 220), (50, 50, 60), (150, 152, 162), (246, 246, 246)]


def apply(new, save):
    """save(path, img) writes a PNG; returns how many graphics were drawn"""
    g = os.path.join(new, 'graphics')

    def w(name, img):
        p = os.path.join(g, name + '.png')
        if os.path.exists(p):
            save(p, img)
            return 1
        return 0
    n = w('STBAR', stbar()) + w('STARMS', starms())
    for name, img in all_faces(None).items():
        n += w(name, img)
    for ch, pre in CLASS_FACES.items():                       # one face set per character
        for name, img in all_faces(None, ch, pre).items():
            p = os.path.join(g, name + '.png')
            save(p, img)
            n += 1
    for i, rgb in enumerate(((60, 200, 70), (90, 90, 230), (170, 110, 60), (220, 50, 50))):
        n += w('STPB%d' % i, player_bg(rgb))
        p = os.path.join(g, 'STFB%d.png' % i)
        if os.path.exists(p):
            n += w('STFB%d' % i, player_bg(rgb).resize(Image.open(p).size, Image.LANCZOS))
    for i, rgb in enumerate(KEYRGB):
        n += w('BGKEY%d' % i, key_icon(rgb, 8))
    for i, nm in enumerate(('STKEYS0', 'STKEYS1', 'STKEYS2', 'STKEYS3', 'STKEYS4', 'STKEYS5')):
        p = os.path.join(g, nm + '.png')
        if os.path.exists(p):
            sz = Image.open(p).size
            n += w(nm, key_icon(KEYRGB[[0, 1, 2, 0, 1, 2][i]], 7).resize(sz, Image.LANCZOS))
    for nm in ('M_SKULL1', 'M_SKULL2'):
        n += w(nm, skull_icon(20, 19, int(nm[-1]) - 1))
    for name, img in title_screens().items():
        n += w(name, img)
    p = os.path.join(g, 'M_DOOM.png')
    if os.path.exists(p):
        sz = Image.open(p).size
        n += w('M_DOOM', menu_logo(*sz))
    return n
