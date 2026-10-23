#!/usr/bin/env python3
"""Begotten: build a complete PLACEHOLDER content tree (no third-party art, sound, music or maps).

usage: make_placeholder_content.py OLD_TREE NEW_TREE

OLD_TREE is an unpacked begotten.wad (pack_wad.py unpack).  NEW_TREE is created from it:

  sprites/    every sprite lump again, SAME NAME, SAME SIZE, SAME HOT SPOT, drawn as a labelled coloured
              block (name, frame letter and rotation number on it) - the engine's frames and hit boxes keep working
  graphics/   every menu / HUD / intermission / title graphic again, same name and size.  Fonts and number
              graphics are real (plain) glyphs, so text stays readable; the rest are labelled blocks
  textures/   a small set of labelled wall textures (GREY1 ... DOOR1, SKY1 and MAP01 ... MAP32)
  flats/      a small set of labelled floors / ceilings (F_SKY1 included)
  sounds/     every sound name again, a very short silence (replace these)
  music/      every music name again, a silent MIDI (replace these)
  maps/       MAP01 ... MAP32: one square room with player starts and an exit switch
  data/       UMAPINFO / SWITCHES rewritten; DEHACKED, BGCLASS and the engine's own lumps are kept
  palette/    PLAYPAL, COLORMAP, GENMIDI ... are kept (they are tables, not art); ENDOOM is replaced

Nothing from the old tree's pictures, sounds, music or maps is copied.  Also writes SOUND_MAP.md.
"""
import math
import colorsys, glob, hashlib, io, os, re, shutil, struct, sys, wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFont
from pack_wad import png_bytes, png_offsets

FONT_PATHS = ['/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf',
              '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf']
_fonts = {}


def font(size):
    if size not in _fonts:
        for p in FONT_PATHS:
            if os.path.exists(p):
                _fonts[size] = ImageFont.truetype(p, size)
                break
        else:
            _fonts[size] = ImageFont.load_default()
    return _fonts[size]


def hue_of(s):
    return int(hashlib.md5(s.encode()).hexdigest()[:6], 16) % 360


def hsv(h, s, v):
    r, g, b = colorsys.hsv_to_rgb((h % 360) / 360.0, max(0, min(1, s)), max(0, min(1, v)))
    return (int(r * 255), int(g * 255), int(b * 255), 255)


def text_mask(text, w, h, max_size=40, min_size=5):
    """largest bold text that fits w x h, as a hard 1-bit mask image of exactly w x h (text centred)"""
    for size in range(max_size, min_size - 1, -1):
        f = font(size)
        m = Image.new('L', (w * 4, h * 4), 0)
        d = ImageDraw.Draw(m)
        bb = d.textbbox((0, 0), text, font=f)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        if tw <= w and th <= h:
            out = Image.new('L', (w, h), 0)
            od = ImageDraw.Draw(out)
            od.text(((w - tw) // 2 - bb[0], (h - th) // 2 - bb[1]), text, fill=255, font=f)
            return out.point(lambda v: 255 if v >= 110 else 0), size
    return None, 0


RECORD_TEXT = False     # while the 1x sprites are made: remember the labels instead of drawing them (redrawn sharp at hi-res)
TEXTS = {}              # saved sprite path -> [(text, box, colour, max size, min size)]


def put_text(img, text, box, color, max_size=40, min_size=5):
    if RECORD_TEXT:
        if getattr(img, '_texts', None) is None:
            img._texts = []
        img._texts.append((text, tuple(box), color, max_size, min_size))
        return True
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    if w < 4 or h < 4:
        return False
    m, size = text_mask(text, w, h, max_size, min_size)
    if m is None:
        return False
    img.paste(Image.new('RGBA', (w, h), color), (x0, y0), m)
    return True


def save(path, img, off=None):
    if getattr(img, '_texts', None):
        TEXTS[os.path.abspath(path)] = img._texts
    os.makedirs(os.path.dirname(path), exist_ok=True)
    lo, to = off if off else (None, None)
    open(path, 'wb').write(png_bytes(img, lo, to) if off is not None else png_bytes(img, 0, 0))


# --------------------------------------------------------------------------------------------- sprites

HINTS = {'PUNG': 'FIST / KNIFE', 'PISG': 'PISTOL', 'PISF': 'PISTOL FLASH', 'SHTG': 'SHOTGUN', 'SHTF': 'SHOTGUN FLASH',
         'SHT2': 'SUPER SHOTGUN', 'CHGG': 'CHAINGUN', 'CHGF': 'CHAINGUN FLASH', 'MISG': 'ROCKET LAUNCHER',
         'MISF': 'ROCKET FLASH', 'SAWG': 'CHAINSAW', 'PLSG': 'PLASMA GUN', 'PLSF': 'PLASMA FLASH', 'BFGG': 'BFG',
         'BFGF': 'BFG FLASH'}


def sprite_label(stem):
    """SRUNA1 -> ('SRUN', 'A', '1');  TROOA2A8 -> ('TROO', 'A', '2/8')"""
    name = stem[:4]
    frame = stem[4:5]
    rot = stem[5:6]
    if len(stem) >= 8:
        rot = stem[5:6] + '/' + stem[7:8]
    return name, frame, rot


def draw_dial(img, rotation):
    """a small dial in the top right corner: a ring with a sphere on it that sits where the sprite's owner is looking.
    Rotation 1 = facing the viewer (sphere at the bottom), 3 = facing the viewer's left, 5 = facing away (top),
    7 = facing the viewer's right; the others are in between."""
    w, h = img.size
    r = min(10, w // 4, h // 4)
    if r < 3:
        return
    d = ImageDraw.Draw(img)
    cx, cy = w - r - 2, r + 2
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(0, 0, 0, 255), outline=(255, 255, 255, 255))
    theta = math.radians(180 + (rotation - 1) * 45)         # clockwise from straight up
    dx, dy = math.sin(theta), -math.cos(theta)
    sr = max(1, r // 3)
    sx, sy = cx + dx * (r - sr - 1), cy + dy * (r - sr - 1)
    d.line([cx, cy, sx, sy], fill=(255, 255, 255, 255))
    d.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], fill=(255, 220, 40, 255), outline=(0, 0, 0, 255))


def make_sprite(stem, w, h):
    img = make_sprite_base(stem, w, h)
    rot = stem[5:6]
    if len(stem) == 6 and rot in tuple('12345678') and w >= 12 and h >= 12:
        draw_dial(img, int(rot))
    return img


def make_sprite_base(stem, w, h):
    name, frame, rot = sprite_label(stem)
    hue = hue_of(name)
    fi = (ord(frame) - 65) if frame else 0
    body = hsv(hue, 0.55, 0.45 + 0.025 * (fi % 12))
    edge = hsv(hue, 0.25, 0.95)
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if w <= 2 or h <= 2:
        d.rectangle([0, 0, w - 1, h - 1], fill=body)
        return img
    big = w * h > 3500
    if big:
        # see-through block (first-person weapons): dithered body, thick outline
        px = img.load()
        for y in range(h):
            for x in range(w):
                if (x + y) % 2 == 0:
                    px[x, y] = body
        t = 2 if min(w, h) > 12 else 1
        for i in range(t):
            d.rectangle([i, i, w - 1 - i, h - 1 - i], outline=edge)
        d.line([0, 0, w - 1, h - 1], fill=edge)
        d.line([0, h - 1, w - 1, 0], fill=edge)
    else:
        d.rectangle([0, 0, w - 1, h - 1], fill=body)
        d.rectangle([0, 0, w - 1, h - 1], outline=edge)
    # label
    label1 = name
    label2 = (frame + (' ' + rot if rot else '')).strip()
    plate = (0, 0, 0, 255)
    if w >= 28 and h >= 20:
        ph = max(9, h // 4) if name in HINTS and h >= 30 else max(9, h // 3)
        centers = [h // 3, 2 * h // 3] if name in HINTS and h >= 30 else [h // 2]
        for k, cy in enumerate(centers):
            y0, y1 = max(1, cy - ph // 2), min(h - 2, cy + ph // 2)
            d.rectangle([1, y0, w - 2, y1], fill=plate)
            if k == 0:
                put_text(img, label1 + ' ' + label2, (2, y0 + 1, w - 2, y1), (255, 255, 255, 255), 14, 5)
            else:
                put_text(img, HINTS[name], (2, y0 + 1, w - 2, y1), (255, 230, 120, 255), 14, 5)
    elif w >= 9 and h >= 9:
        put_text(img, (frame + rot[:1]) if rot else frame, (1, 1, w - 1, h - 1), (255, 255, 255, 255), 14, 5)
    elif w >= 5 and h >= 5:
        put_text(img, frame or '?', (1, 1, w - 1, h - 1), (255, 255, 255, 255), 10, 4)
    return img


WEAPON_BASE = {'PUNG': 'PUNG', 'PISG': 'PISG', 'SHTG': 'SHTG', 'SHT2': 'SHT2', 'CHGG': 'CHGG', 'MISG': 'MISG',
               'SAWG': 'SAWG', 'PLSG': 'PLSG', 'BFGG': 'BFGG',
               'PISF': 'PISG', 'SHTF': 'SHTG', 'CHGF': 'CHGG', 'MISF': 'MISG', 'PLSF': 'PLSG', 'BFGF': 'BFGG'}


def weapon_shifts(old):
    """per weapon: how far (in pixels) to move every one of its frames so that its first frame sits in the middle of the screen
    (flashes and sub-sprites move with their weapon, so they stay lined up)"""
    first = {}
    for p in glob.glob(os.path.join(old, 'sprites', '**', '*.png'), recursive=True):
        stem = os.path.splitext(os.path.basename(p))[0]
        if stem[:4] in WEAPON_BASE and WEAPON_BASE[stem[:4]] == stem[:4] and stem[4:5] == 'A':
            first[stem[:4]] = p
    out = {}
    for base, p in first.items():
        raw = open(p, 'rb').read()
        lo = (png_offsets(raw) or (0, 0))[0]
        w = Image.open(io.BytesIO(raw)).size[0]
        out[base] = 160 - (1 - lo + w / 2.0)
    return out


def do_sprites(old, new):
    global RECORD_TEXT
    RECORD_TEXT = True
    try:
        return _do_sprites(old, new)
    finally:
        RECORD_TEXT = False


def _do_sprites(old, new):
    n = 0
    shifts = weapon_shifts(old)
    for p in glob.glob(os.path.join(old, 'sprites', '**', '*.png'), recursive=True):
        rel = os.path.relpath(p, os.path.join(old, 'sprites'))
        raw = open(p, 'rb').read()
        off = png_offsets(raw) or (0, 0)
        w, h = Image.open(io.BytesIO(raw)).size
        stem = os.path.splitext(os.path.basename(p))[0]
        if stem[:4] in WEAPON_BASE and WEAPON_BASE[stem[:4]] in shifts:
            off = (int(round(off[0] - shifts[WEAPON_BASE[stem[:4]]])), off[1])
        if len(stem) == 8 and '%' not in stem:
            # a lump shared by two rotations (TROOA2A8 = angle 2, and angle 8 drawn mirrored): split it into two lumps so
            # that every one of the eight directions has its own picture, nothing is mirrored, and each shows its own
            # direction dial. The second one is the first flipped, so its left offset is mirrored too.
            d = os.path.dirname(rel)
            save(os.path.join(new, 'sprites', d, stem[:6] + '.png'), make_sprite(stem[:6], w, h), off)
            save(os.path.join(new, 'sprites', d, stem[:4] + stem[6:8] + '.png'), make_sprite(stem[:4] + stem[6:8], w, h), (w - off[0], off[1]))
            n += 2
            continue
        save(os.path.join(new, 'sprites', rel), make_sprite(stem, w, h), off)
        n += 1
    n += do_blade_frames(new)
    n += do_clap_frames(new)
    n += do_class_weapons(new)
    n += do_class_bodies(new)
    n += do_class_poses(new)
    n += do_player_shots(new)
    n += do_pickups(old, new)
    return n


# Pickups: eight looping frames (A .. H, no rotations) for each weapon pickup and each of the nine keys.  The placeholder is
# a coloured block with the frame letter and a bright band that climbs the block frame by frame, so the loop is visible.
PICKUP_WEAPONS = {'SHOT': 'SHOTGUN', 'MGUN': 'CHAINGUN', 'LAUN': 'LAUNCHER', 'PLAS': 'PLASMA', 'BFUG': 'BFG', 'SGN2': 'SUPER SG'}
KEYS = [('BKEY', 'blue', (40, 80, 255)), ('YKEY', 'yellow', (240, 220, 40)), ('RKEY', 'red', (230, 30, 30)),
        ('GKEY', 'green', (40, 200, 60)), ('CKEY', 'cyan', (40, 220, 230)), ('MKEY', 'magenta', (220, 50, 210)),
        ('KKEY', 'black', (28, 28, 34)), ('AKEY', 'grey', (140, 140, 150)), ('WKEY', 'white', (245, 245, 245))]


def pickup_frame(name, fi, w, h, rgb, label):
    img = Image.new('RGBA', (w, h), rgb + (255,))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 1, h - 1], outline=(255, 255, 255, 255) if sum(rgb) < 600 else (0, 0, 0, 255))
    # the climbing band
    n = max(1, h - 2)
    y = 1 + (fi * n) // 8
    band = (255, 255, 255, 255) if sum(rgb) < 600 else (60, 60, 60, 255)
    d.rectangle([1, y, w - 2, min(h - 2, y + max(0, h // 8))], fill=band)
    txt = (0, 0, 0, 255) if sum(rgb) > 300 else (255, 255, 255, 255)
    if w >= 24 and h >= 10:
        put_text(img, name + ' ' + 'ABCDEFGH'[fi] + ' ' + label, (2, 1, w - 2, h - 1), txt, 12, 5)
    elif w >= 9 and h >= 9:
        put_text(img, 'ABCDEFGH'[fi], (1, 1, w - 1, h - 1), txt, 14, 5)
    return img


def do_pickups(old, new):
    n = 0
    sizes = {}
    for name in list(PICKUP_WEAPONS) + ['BKEY']:
        p = os.path.join(old, 'sprites', name, name + 'A0.png')
        raw = open(p, 'rb').read()
        sizes[name] = (Image.open(io.BytesIO(raw)).size, png_offsets(raw) or (0, 0))
    for name, label in PICKUP_WEAPONS.items():
        (w, h), off = sizes[name]
        hue = hsv(hue_of(name), 0.55, 0.6)
        rgb = tuple(hue[:3]) if len(hue) >= 3 else (150, 150, 150)
        for fi in range(8):
            save(os.path.join(new, 'sprites', name, '%s%s0.png' % (name, 'ABCDEFGH'[fi])), pickup_frame(name, fi, w, h, rgb, label), off)
            n += 1
    (w, h), off = sizes['BKEY']
    for name, label, rgb in KEYS:
        for fi in range(8):
            save(os.path.join(new, 'sprites', name, '%s%s0.png' % (name, 'ABCDEFGH'[fi])), pickup_frame(name, fi, w, h, rgb, label), off)
            n += 1
    # HUD icons, one per colour: BGKEY0 .. BGKEY8 in the engine's order (blue yellow red green cyan magenta black grey white)
    for c, (name, label, rgb) in enumerate(KEYS):
        img = Image.new('RGBA', (8, 8), rgb + (255,))
        ImageDraw.Draw(img).rectangle([0, 0, 7, 7], outline=(255, 255, 255, 255) if sum(rgb) < 600 else (0, 0, 0, 255))
        save(os.path.join(new, 'graphics', 'BGKEY%d.png' % c), img, (0, 0))
        n += 1
    return n


def do_clap_frames(new):
    """extra first-person frames: the thunderclap has nine (clasped, six of the hands drawing apart, the clap, after) and the
    fireball six (ready, three of the orb growing, the cast, after); the stock BFG has three and the missile launcher two, so the
    rest are stubs the art pass draws over"""
    n = 0
    for fam, last, frames in (('BFGG', 'C', 'DEFGHI'), ('MISG', 'B', 'CDEF')):
        d = os.path.join(new, 'sprites', fam)
        cpath = os.path.join(d, '%s%s0.png' % (fam, last))
        if not os.path.exists(cpath):
            continue
        raw = open(cpath, 'rb').read()
        off = png_offsets(raw) or (0, 0)
        w, h = Image.open(io.BytesIO(raw)).size
        for fr in frames:
            save(os.path.join(d, '%s%s0.png' % (fam, fr)), make_sprite('%s%s0' % (fam, fr), w, h), off)
            n += 1
    return n


def do_blade_frames(new):
    """PUNG E-G: the charged stab and riposte frames the swordsman's blade needs (the engine's states use them).
    E = sword drawn back, only the tip showing in the lower right corner; F = the thrust (same place as D);
    G = riposte wind-up, drawn back further to the lower left."""
    d = os.path.join(new, 'sprites', 'PUNG')
    dpath = os.path.join(d, 'PUNGD0.png')
    if not os.path.exists(dpath):
        return 0
    raw = open(dpath, 'rb').read()
    doff = png_offsets(raw) or (0, 0)
    dw, dh = Image.open(io.BytesIO(raw)).size
    # sprite top lands at screen row 32 - topoffset, left edge at column 1 - leftoffset (320x200 space)
    frames = {'E': (56, 56, (-250, -90)), 'F': (dw, dh, doff), 'G': (80, 72, (-60, -104))}
    for fr in 'HIJKLMNOP':      # the parry: mirrored guard, slow return, bat-away swing and its mirror (art_fp.py draws them)
        frames[fr] = (dw, dh, doff)
    for fr, (w, h, off) in frames.items():
        save(os.path.join(d, 'PUNG%s0.png' % fr), make_sprite('PUNG%s0' % fr, w, h), off)
    return len(frames)


# First-person weapons per class: the guns are the same but the hands holding them are not.  Soldier: one hand (dark
# skin, a black model); witch: both hands (light skin), and she handles guns more loosely.  The engine draws a class'
# own lumps wherever its BGCLASS has "sprite <stock name> <class name>" (data/lumps/bgclass.lmp); a frame the class has
# no lump for falls back to the stock one.  Muzzle flashes are shared.
SOLDIER_SKIN = (96, 62, 40, 255)
WITCH_SKIN = (240, 203, 176, 255)
CLASS_WEAPONS = {  # stock sprite -> (soldier name, witch name)
    'PUNG': ('SKNF', 'WPUN'), 'SNPG': ('SSNP', 'WSNP'), 'HLFG': ('SHLG', 'WHLG'), 'ARCG': ('SARC', 'WARC'), 'TRNG': ('STRN', 'WTRN'), 'SAWG': ('SSAW', 'WSAW'), 'PISG': ('SPIS', 'WPIS'), 'SHTG': ('SSHG', 'WSHG'),
    'SHT2': ('SSH2', 'WSH2'), 'CHGG': ('SCHG', 'WCHG'), 'MISG': ('SMSG', 'WMSG'), 'PLSG': ('SPLG', 'WPLG'),
    'BFGG': ('SBFG', 'WBFG')}


def class_hands(img, who, fi):
    """paint the hands (placeholder blocks in the class' skin tone) on the lower part of a weapon block"""
    w, h = img.size
    d = ImageDraw.Draw(img)
    outline = (0, 0, 0, 255)
    if who == 0:      # soldier: one hand, steady, on the right
        boxes = [(0.58, 0.60, 0.86, 0.96)]
        skin = SOLDIER_SKIN
    else:             # witch: two hands, a little loose: they wander from frame to frame
        wob = ((fi * 7) % 5 - 2) / 100.0
        boxes = [(0.12 + wob, 0.62, 0.38 + wob, 0.97), (0.60 - wob, 0.58, 0.88 - wob, 0.96)]
        skin = WITCH_SKIN
    for x0, y0, x1, y1 in boxes:
        r = [int(w * x0), int(h * y0), int(w * x1) - 1, int(h * y1) - 1]
        d.rectangle(r, fill=skin, outline=outline)
    tag = 'SOLDIER' if who == 0 else 'WITCH'
    if w >= 40 and h >= 30:
        d.rectangle([2, 2, w - 3, max(10, h // 6)], fill=(0, 0, 0, 255))
        put_text(img, tag, (3, 3, w - 3, max(10, h // 6)), (255, 255, 255, 255), 12, 5)


def do_player_shots(new):
    """stand-in lumps for the player's own projectile sprites (begotten.c: fireball, devil's breath, tornado, arc lightning,
    hellfire, thunderclap ring); art_fx_providers.player_shots draws them at their real size.  Sizes are map units at 1x."""
    sizes = {'FBAL': [22, 22, 34, 40, 36], 'DBRT': [12, 12, 16, 16], 'TRNP': [18, 18, 18, 18, 30, 30], 'TRNF': [40, 40, 40, 40],
             'ARCB': [14, 14, 18, 18], 'HLFR': [None, 40, 52, 44], 'CLAP': [30, 70, 110, 150, 190]}
    tall = {'DBRT': (12, 18), 'TRNF': (40, 64)}
    n = 0
    for fam, szs in sizes.items():
        for i, sz in enumerate(szs):
            frame = chr(65 + i)
            w, h = (sz, sz) if sz else (20, 12)
            if fam in tall and i < 4 and not (fam == 'DBRT' and i >= 2):
                w, h = tall[fam]
            rots = '12345678' if (fam == 'HLFR' and i == 0) else '0'
            for r in rots:
                stem = '%s%s%s' % (fam, frame, r)
                save(os.path.join(new, 'sprites', fam, stem + '.png'), make_sprite(stem, w, h), (w // 2, h // 2))
                n += 1
    return n


def do_class_bodies(new):
    """the witch and the swordsman have bodies of their own (WPLY, XPLY: the PLAY layout, so standing, pain and death are theirs too)"""
    n = 0
    for p in sorted(glob.glob(os.path.join(new, 'sprites', 'PLAY', 'PLAY*.png'))):
        stem = os.path.basename(p)[:-4]
        for nm in ('WPLY', 'XPLY'):
            os.makedirs(os.path.join(new, 'sprites', nm), exist_ok=True)
            shutil.copyfile(p, os.path.join(new, 'sprites', nm, nm + stem[4:] + '.png'))
            n += 1
    return n


def do_class_poses(new):
    """stand / shoot sprites for every weapon of the soldier and the witch (art_class_weapons.py), and the swordsman's stand:
    stubs that the art pass draws over (one set of eight rotations per frame, copied from the SSHT stubs)"""
    import art_class_weapons as CW
    n = 0
    srcs = {'stand': ('SSHT', 1), 'shoot': ('SSHT', 3), 'run': ('SRUN', 6), 'jump': ('SJMP', 3),
            'cstand': ('SSHT', 1), 'crun': ('SRUN', 6), 'cjump': ('SJMP', 3), 'lunge': ('SSHT', 1), 'lungeair': ('SSHT', 1)}
    for fam, (cls, code, kind) in sorted(CW.FAMILIES.items()):
        sfam, nfr = srcs[kind]
        if kind == 'shoot' and CW.WEAPON_OF[code] in CW.LEAD_POSE:
            nfr = CW.LEAD_POSE[CW.WEAPON_OF[code]][0]       # lead-up, cast and after
        for fr in range(nfr):
            for rot in '12345678':
                src = os.path.join(new, 'sprites', sfam, '%s%s%s.png' % (sfam, 'ABCDEF'[min(fr, 2) if kind == 'shoot' else (fr if kind != 'stand' else 0)], rot))
                if not os.path.exists(src):
                    continue
                os.makedirs(os.path.join(new, 'sprites', fam), exist_ok=True)
                shutil.copyfile(src, os.path.join(new, 'sprites', fam, '%s%s%s.png' % (fam, 'ABCDEF'[fr], rot)))
                n += 1
    return n


def do_class_weapons(new):
    n = 0
    # hellfire (HLFG, flash HLFF) and arc lightning (ARCG) have sprites of their own too
    for fam, src_fam, frames in (('HLFG', 'MISG', 'AB'), ('HLFF', 'MISF', 'AB'), ('ARCG', 'PLSG', 'ABB'), ('TRNG', 'PLSG', 'ABBB')):
        for i, fr in enumerate(frames):
            src = os.path.join(new, 'sprites', src_fam, '%s%s0.png' % (src_fam, fr))
            if os.path.exists(src):
                os.makedirs(os.path.join(new, 'sprites', fam), exist_ok=True)
                shutil.copyfile(src, os.path.join(new, 'sprites', fam, '%s%s0.png' % (fam, 'ABCD'[i])))
    # the snap has a sprite of its own (SNPG, frames A-D): a stock set for classes without their own hands
    for fr in 'ABCD':
        src = os.path.join(new, 'sprites', 'PUNG', 'PUNG%s0.png' % fr)
        if os.path.exists(src):
            os.makedirs(os.path.join(new, 'sprites', 'SNPG'), exist_ok=True)
            shutil.copyfile(src, os.path.join(new, 'sprites', 'SNPG', 'SNPG%s0.png' % fr))
    for stock, names in CLASS_WEAPONS.items():
        for p in sorted(glob.glob(os.path.join(new, 'sprites', stock, stock + '?0.png'))):
            frame = os.path.basename(p)[4]
            if stock == 'SNPG' and frame not in 'ABCD':
                continue
            if stock == 'PUNG' and frame in 'HIJKLMNOP':
                continue
            raw = open(p, 'rb').read()
            off = png_offsets(raw) or (0, 0)
            w, h = Image.open(io.BytesIO(raw)).size
            for who, nm in enumerate(names):
                img = make_sprite_base(stock + frame + '0', w, h)
                if w >= 12 and h >= 12:
                    class_hands(img, who, ord(frame) - 65)
                save(os.path.join(new, 'sprites', nm, nm + frame + '0.png'), img, off)
                n += 1
    return n


# --------------------------------------------------------------------------------------------- graphics

def old_colors(img):
    cols = {}
    for px in img.getdata():
        if px[3] >= 128:
            cols[px[:3]] = cols.get(px[:3], 0) + 1
    return cols


def luma(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


MENU_TEXT = {
    'M_NGAME': 'NEW GAME', 'M_OPTION': 'OPTIONS', 'M_LOADG': 'LOAD GAME', 'M_SAVEG': 'SAVE GAME',
    'M_RDTHIS': 'READ THIS', 'M_QUITG': 'QUIT GAME', 'M_NEWG': 'NEW GAME', 'M_SKILL': 'SKILL',
    'M_EPISOD': 'EPISODE', 'M_EPI1': 'EPISODE 1', 'M_JKILL': 'EASY', 'M_ROUGH': 'NORMAL', 'M_HURT': 'HARD',
    'M_ULTRA': 'HARDER', 'M_NMARE': 'NIGHTMARE', 'M_ENDGAM': 'END GAME', 'M_MESSG': 'MESSAGES',
    'M_DETAIL': 'DETAIL', 'M_SCRNSZ': 'SCREEN SIZE', 'M_MSENS': 'MOUSE SENS', 'M_SVOL': 'SOUND VOLUME',
    'M_SFXVOL': 'SFX VOLUME', 'M_MUSVOL': 'MUSIC VOLUME', 'M_DISP': 'DISPLAY', 'M_PAUSE': 'PAUSED',
    'M_GOING': 'GOING', 'M_COMING': 'COMING', 'M_MULTI': 'MULTIPLAYER', 'M_OPTTTL': 'OPTIONS',
    'M_LGTTL': 'LOAD GAME', 'M_SGTTL': 'SAVE GAME', 'M_LOKSEN': 'LOOK SENS', 'M_HORSEN': 'HORIZ SENS',
    'M_SOUND': 'SOUND', 'M_ABOUT': 'ABOUT', 'M_AUTO': 'AUTOMAP', 'M_CHAT': 'CHAT', 'M_COMPAT': 'COMPAT',
    'M_DEMOS': 'DEMOS', 'M_ENEM': 'ENEMIES', 'M_GENERL': 'GENERAL', 'M_KEYBND': 'KEY BINDS',
    'M_MESS': 'MESSAGES', 'M_MOUSE': 'MOUSE', 'M_STAT': 'STATUS', 'M_VIDEO': 'VIDEO', 'M_WEAP': 'WEAPONS',
    'M_MSGON': 'ON', 'M_MSGOFF': 'OFF', 'M_GDHIGH': 'HIGH', 'M_GDLOW': 'LOW',
}


def label_from_name(stem):
    return MENU_TEXT.get(stem, re.sub(r'^(M|WI|ST)_?', '', stem) or stem)


def glyph_for(stem):
    """character a font / number lump stands for, or None"""
    m = re.match(r'^STCFN(\d{3})$', stem)
    if m:
        return chr(int(m.group(1)))
    m = re.match(r'^(STTNUM|STYSNUM|STGNUM|WINUM|AMMNUM)(\d)$', stem)
    if m:
        return m.group(2)
    return {'STTMINUS': '-', 'STTPRCNT': '%', 'WIMINUS': '-', 'WICOLON': ':'}.get(stem)


FONT_GROUPS = ('STCFN', 'STTNUM', 'STYSNUM', 'STGNUM', 'WINUM', 'AMMNUM')


def font_group(stem):
    for g in FONT_GROUPS:
        if stem.startswith(g):
            return g
    return None


def glyph_img(ch, w, h, ramp, shadow=None):
    """a plain glyph, as large as fits, in the palette colours the original used (so text colouring still works)"""
    bright = ramp[-1]
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    best = None
    for size in range(40, 3, -1):
        f = font(size)
        m = Image.new('L', (w * 4, h * 4), 0)
        bb = ImageDraw.Draw(m).textbbox((0, 0), ch, font=f)
        if bb[2] - bb[0] <= w - (1 if shadow else 0) and bb[3] - bb[1] <= h - (1 if shadow else 0):
            best = (size, bb)
            break
    if best is None:
        return img
    size, bb = best
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text(((w - (bb[2] - bb[0])) // 2 - bb[0], (h - (bb[3] - bb[1])) // 2 - bb[1]), ch, fill=255, font=font(size))
    mask = mask.point(lambda v: 255 if v >= 110 else 0)
    if shadow:
        # a dark drop shadow one pixel down and right: it also gives the engine's text-colouring code (which measures
        # the lightest and darkest pixel of the letter A to scale its colour tables) a range to work with. A font made of one
        # flat colour has no range, and every colour table then maps it to the DARK end of the ramp.
        sh = Image.new('L', (w, h), 0)
        sh.paste(mask, (1, 1))
        img.paste(Image.new('RGBA', (w, h), shadow + (255,)), (0, 0), sh)
    img.paste(Image.new('RGBA', (w, h), bright + (255,)), (0, 0), mask)
    return img


def make_graphic(stem, w, h, old_img, ramp_all):
    cols = old_colors(old_img)
    ordered = sorted(cols, key=luma) or [(200, 200, 200)]
    if cols:
        top = max(cols.values())
        main = max((c for c in cols if cols[c] >= 0.2 * top), key=luma)
    else:
        main = (160, 160, 160)
    hue = hue_of(stem)
    ch = glyph_for(stem)
    if ch is not None:
        # every glyph of a font gets the SAME colour (the font's main fill), otherwise letters whose original
        # art happened to use other shades come out in odd colours once the engine tints the text
        col = FONT_COLOR.get(font_group(stem)) or (sorted(cols, key=luma)[-1] if cols else (255, 255, 255))
        if font_group(stem) == 'STCFN':
            return glyph_img(ch, w, h, [(255, 255, 255)], shadow=(70, 70, 70))
        return glyph_img(ch, w, h, [col])
    # sprite-like small graphics keep a transparent background when the original had one
    transparent = any(px[3] < 128 for px in old_img.getdata())
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    text = label_from_name(stem)
    if stem.startswith('M_') and (w > 20 and h > 6) and stem not in ('M_SKULL1', 'M_SKULL2', 'M_THERML', 'M_THERMM',
                                                                         'M_THERMR', 'M_THERMO', 'M_BLACK'):
        # menu text picture: just the words, in the colour the original used
        put_text(img, text, (0, 0, w, h), main + (255,), 30, 5)
        return img
    if stem in ('M_SKULL1', 'M_SKULL2'):
        c = hsv(0, 0.8, 0.9) if stem.endswith('1') else hsv(40, 0.8, 0.95)
        d.polygon([(0, 0), (w - 1, h // 2), (0, h - 1)], fill=c)
        return img
    if stem.startswith('M_THERM'):
        c = hsv(120, 0.5, 0.7)
        d.rectangle([0, 0, w - 1, h - 1], fill=c if stem == 'M_THERMO' else hsv(0, 0, 0.45))
        return img
    if stem == 'M_BLACK':
        return Image.new('RGBA', (w, h), (0, 0, 0, 255))
    if stem.startswith('CWILV'):
        n = int(stem[5:]) + 1
        put_text(img, 'MAP %02d' % n, (0, 0, w, h), (230, 230, 230, 255), 30, 5)
        return img
    if stem.startswith('BRDR_') or stem.startswith('STBAR') or stem in ('STARMS',):
        base = hsv(hue, 0.3, 0.30)
        d.rectangle([0, 0, w - 1, h - 1], fill=base, outline=hsv(hue, 0.2, 0.6))
        if w >= 40 and h >= 10:
            put_text(img, stem, (4, 2, w - 4, h - 2), (210, 210, 210, 255), 12, 5)
        return img
    if w >= 160 and h >= 100:
        # full-screen pictures: dark gradient with the lump name and the word PLACEHOLDER
        for y in range(h):
            c = hsv(hue, 0.35, 0.18 + 0.20 * y / h)
            d.line([0, y, w - 1, y], fill=c)
        d.rectangle([0, 0, w - 1, h - 1], outline=hsv(hue, 0.3, 0.7))
        put_text(img, 'BEGOTTEN', (w // 8, h // 6, w - w // 8, h // 6 + h // 5), (230, 230, 230, 255), 40, 8)
        put_text(img, stem + '  (PLACEHOLDER)', (w // 8, h // 2, w - w // 8, h // 2 + h // 10), (190, 190, 190, 255), 20, 6)
        return img
    # everything else (faces, keys, intermission bits, ...): a labelled block
    body = hsv(hue, 0.5, 0.5)
    if transparent and w * h < 200:
        d.rectangle([0, 0, w - 1, h - 1], fill=body)
        return img
    d.rectangle([0, 0, w - 1, h - 1], fill=body, outline=hsv(hue, 0.25, 0.95))
    lab = re.sub(r'^(ST|WI)', '', stem)
    if w >= 8 and h >= 7:
        put_text(img, lab, (1, 1, w - 1, h - 1), (255, 255, 255, 255), 14, 4)
    return img


FONT_COLOR = {}


def do_graphics(old, new):
    n = 0
    # the colour each font group draws most of its pixels in (brightest of the common ones)
    counts = {}
    for p in glob.glob(os.path.join(old, 'graphics', '*.png')):
        stem = os.path.splitext(os.path.basename(p))[0]
        g = font_group(stem)
        if g and glyph_for(stem) is not None:
            for c, k in old_colors(Image.open(p).convert('RGBA')).items():
                counts.setdefault(g, {})[c] = counts.get(g, {}).get(c, 0) + k
    for g, cs in counts.items():
        top = max(cs.values())
        FONT_COLOR[g] = max((c for c in cs if cs[c] >= 0.25 * top), key=luma)
    ramp_all = [(255, 255, 255)]
    for p in sorted(glob.glob(os.path.join(old, 'graphics', '*.png'))):
        stem = os.path.splitext(os.path.basename(p))[0]
        raw = open(p, 'rb').read()
        off = png_offsets(raw) or (0, 0)
        im = Image.open(io.BytesIO(raw)).convert('RGBA')
        w, h = im.size
        out = make_graphic(stem, w, h, im, ramp_all)
        save(os.path.join(new, 'graphics', stem + '.png'), out, off)
        n += 1
    return n


# --------------------------------------------------------------------------------------------- textures / flats

def block(w, h, hue, label, sat=0.45, val=0.5, grid=16, sub=None, k=1.0):
    """a coloured, gridded block with a label; k = pixels per map unit (sizes and grid come in pixels already)"""
    img = Image.new('RGBA', (w, h), hsv(hue, sat, val))
    d = ImageDraw.Draw(img)
    for x in range(0, w, grid):
        d.line([x, 0, x, h - 1], fill=hsv(hue, sat, val * 0.78))
    for y in range(0, h, grid):
        d.line([0, y, w - 1, y], fill=hsv(hue, sat, val * 0.78))
    d.rectangle([0, 0, w - 1, h - 1], outline=hsv(hue, 0.2, 0.95))
    big = int(min(14, h // 4 / k) * k)
    put_text(img, label, (4, h // 2 - big, w - 4, h // 2 + big), (255, 255, 255, 255), int(28 * k), 6)
    if sub:
        put_text(img, sub, (4, 3, w - 4, min(int(14 * k), h // 4)), (230, 230, 230, 255), int(12 * k), 5)
    return img


def do_textures_flats(new, nmaps, k=1.0):
    """wall textures, switches, the sky and the flats, drawn (see art_tex.py); every size is in pixels = map units * k"""
    import art_tex as T

    def px(v):
        return int(round(v * k))

    def tex(name, img):
        save(os.path.join(new, 'textures', name + '.png'), img, (0, 0))

    tex('GREY1', T.blocks(px(64), px(128), 4, (150, 150, 152), (70, 70, 74), seed=1))
    tex('RED1', T.bricks(px(64), px(128), 12, 3, (150, 58, 46), (80, 70, 66), seed=2))
    tex('BLUE1', T.plates(px(64), px(128), 1, 3, (84, 106, 160), (30, 38, 66), seed=3))
    tex('GREEN1', T.blocks(px(64), px(128), 4, (88, 120, 84), (50, 62, 50), seed=4))
    tex('YELLOW1', T.hazard(px(64), px(128), (236, 196, 40), (36, 34, 30), seed=5, bands=6))
    tex('BROWN1', T.planks(px(64), px(128), 4, (136, 94, 58), seed=6))
    tex('DOOR1', T.door(px(128), px(72), seed=7))
    tex('DOORTRAK', T.plates(px(16), px(128), 1, 8, (96, 98, 108), (40, 42, 50), seed=8, rivets=False))
    lift = T.plates(px(64), px(128), 1, 1, (120, 124, 134), (50, 52, 60), seed=9)
    ld = ImageDraw.Draw(lift)
    for i in range(6):
        y = px(128) * (0.12 + 0.15 * i)
        ld.line([px(10), y + px(6), px(32), y, px(54), y + px(6)], fill=(230, 190, 50, 255), width=max(1, px(2)))
    tex('LIFT1', lift)
    step = T.plates(px(256), px(16), 8, 1, (112, 114, 124), (48, 50, 58), seed=10, rivets=False)
    sd = ImageDraw.Draw(step)
    sd.rectangle([0, px(13), px(256), px(16)], fill=(230, 190, 50, 255))
    tex('STEP1', step)
    for name, on, col in (('SWX1', False, (60, 230, 90)), ('SWX2', True, (60, 230, 90)), ('SWS1', False, (190, 90, 255)), ('SWS2', True, (190, 90, 255))):
        tex(name, T.switch(px(64), px(128), on, col, seed=11))
    # one wall per map, in the material of its episode, with a plaque so a map can be told by its walls
    for n in range(1, nmaps + 1):
        if n <= 10:      # episode 1: a night city: brick and concrete
            im = T.bricks(px(128), px(128), 16, 4, [(120, 62, 52), (96, 98, 108), (92, 78, 70)][n % 3], (60, 58, 60), seed=20 + n)
        elif n <= 20:    # episode 2: villages and keeps: warm stone blocks
            im = T.blocks(px(128), px(128), 5, [(196, 170, 120), (176, 150, 110), (210, 186, 140)][n % 3], (92, 76, 58), seed=40 + n)
        else:            # episode 3: ship and alien world: panels
            im = T.plates(px(128), px(128), 2, 2, [(70, 110, 130), (96, 80, 140), (70, 130, 110)][n % 3], (24, 30, 44), seed=60 + n)
        tex('WALL%02d' % n, T.wall_label(im, '%d' % n, px(128)))
    tex('SKY1', T.sky(max(256, px(256) // 2 * 2) if False else 256, 128))

    fs = FLAT_SIZE if k != 1 else 64

    def flat(name, img):
        save(os.path.join(new, 'flats', name + '.png'), img)

    flat('FLOOR1', T.tiles(fs, fs, 4, (150, 148, 142), (60, 58, 56), seed=1))
    flat('FLOOR2', T.tiles(fs, fs, 2, (88, 112, 160), (30, 38, 64), seed=2, checker=0.25))
    flat('FLOOR3', T.rock_floor(fs, fs, (126, 96, 70), seed=3))
    flat('CEIL1', T.tiles(fs, fs, 2, (126, 128, 134), (56, 58, 64), seed=4))
    flat('CEIL2', T.plates(fs, fs, 2, 2, (84, 120, 96), (28, 44, 34), seed=5))
    flat('LIQUID1', T.liquid(fs, fs, (30, 110, 30), (130, 235, 70), seed=6))
    flat('LIQUID2', T.liquid(fs, fs, (24, 70, 150), (110, 190, 255), seed=7))
    flat('LAVA1', T.liquid(fs, fs, (160, 28, 8), (255, 190, 50), seed=8))
    flat('F_SKY1', T.tiles(fs, fs, 1, (150, 190, 240), (150, 190, 240), seed=9))


# --------------------------------------------------------------------------------------------- sounds / music

def silent_wav(path, ms=60):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    w = wave.open(path, 'wb')
    w.setnchannels(1)
    w.setsampwidth(1)
    w.setframerate(11025)
    w.writeframes(bytes([128]) * (11025 * ms // 1000))
    w.close()


def weather_wavs(new):
    # stand-ins for the weather sounds: hiss for rain, a low rumble for thunder (replace with real recordings)
    import random
    rnd = random.Random(7)
    def put(name, secs, fn):
        path = os.path.join(new, 'sounds', name + '.wav')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(1); w.setframerate(11025)
        n = int(11025 * secs); prev = 0.0; out = bytearray()
        for i in range(n):
            prev = fn(i / 11025.0, secs, prev)
            out.append(max(0, min(255, int(128 + prev))))
        w.writeframes(bytes(out)); w.close()
    def rain(t, d, prev):
        edge = min(t, d - t, 0.1) / 0.1
        return (prev * 0.2 + rnd.uniform(-1, 1) * 40) * edge if edge < 1 else prev * 0.2 + rnd.uniform(-1, 1) * 40
    def thunder(t, d, prev):
        env = (1 - t / d) ** 2 * min(1.0, t / 0.15)
        return prev * 0.97 + rnd.uniform(-1, 1) * 30 * env + 0.0
    put('DSRAIN', 1.5, rain)
    put('DSTHUNDR', 2.5, thunder)


def silent_midi(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    trk = bytes([0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,          # tempo
                 0x00, 0xC0, 0x00,                                    # program change
                 0x83, 0x60, 0xFF, 0x2F, 0x00])                       # end of track after 1 beat
    open(path, 'wb').write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, 96) + b'MTrk' + struct.pack('>I', len(trk)) + trk)


def parse_sfx(src):
    """[(enum identifier, lump name)] from the engine's sounds.h / sounds.c"""
    h = open(os.path.join(src, 'sounds.h')).read()
    c = open(os.path.join(src, 'sounds.c')).read()
    m = re.search(r'sfx_pistol = BASE_NUMSFX,(.*?)NUMSFX', h, re.S)
    ids = ['sfx_pistol'] + re.findall(r'(sfx_\w+)\s*(?:=[^,]*)?,', m.group(1))
    t = c[c.index('doom_S_sfx[]'):]
    t = t[:t.index('};')]
    names = re.findall(r'\{\s*"(ds\w+)"', t)[1:]
    return list(zip(ids, names))


def parse_music(src):
    h = open(os.path.join(src, 'sounds.h')).read()
    c = open(os.path.join(src, 'sounds.c')).read()
    t = c[c.index('S_music[]'):]
    t = t[:t.index('};')]
    return re.findall(r'\{\s*"(\w+)"', t)


SFX_NOTES = {
    'pistol': 'pistol / revolver shot', 'shotgn': 'shotgun / rifle shot', 'sgcock': 'shotgun pump',
    'dshtgn': 'super shotgun / scattergun shot', 'dbopn': 'super shotgun: open', 'dbcls': 'super shotgun: close',
    'dbload': 'super shotgun: load shells', 'plasma': 'plasma rifle shot', 'bfg': 'BFG shot',
    'sawup': 'chainsaw: pick up / start', 'sawidl': 'chainsaw: idle', 'sawful': 'chainsaw: swing (full power)',
    'sawhit': 'chainsaw: hits something', 'rlaunc': 'rocket launcher shot', 'rxplod': 'rocket / barrel explosion',
    'firsht': 'fireball launched (imp, caco...)', 'firxpl': 'fireball explodes', 'pstart': 'platform / lift starts',
    'pstop': 'platform / lift stops', 'doropn': 'door opens', 'dorcls': 'door closes', 'stnmov': 'floor / ceiling moving',
    'swtchn': 'switch pressed', 'swtchx': 'switch released', 'plpain': 'player: pain', 'dmpain': 'player: pain (alt)',
    'popain': 'zombie pain', 'vipain': 'monster pain (arch-vile type)', 'mnpain': 'monster pain (mancubus type)',
    'pepain': 'pain elemental pain', 'slop': 'player: gibbed', 'itemup': 'item picked up', 'wpnup': 'weapon picked up',
    'oof': 'player: grunt on landing / hitting a wall', 'telept': 'teleport', 'posit1': 'zombie notices you (1)',
    'posit2': 'zombie notices you (2)', 'posit3': 'zombie notices you (3)', 'bgsit1': 'imp type notices you (1)',
    'bgsit2': 'imp type notices you (2)', 'sgtsit': 'demon type notices you', 'cacsit': 'cacodemon notices you',
    'brssit': 'baron notices you', 'cybsit': 'cyberdemon notices you', 'spisit': 'spider mastermind notices you',
    'bspsit': 'arachnotron notices you', 'kntsit': 'hell knight notices you', 'vilsit': 'arch-vile notices you',
    'mansit': 'mancubus notices you', 'pesit': 'pain elemental notices you', 'sklatk': 'lost soul attack',
    'sgtatk': 'demon bite', 'skepch': 'revenant punch', 'vilatk': 'arch-vile attack', 'claw': 'imp / knight claw',
    'skeswg': 'revenant swing', 'pldeth': 'player: death', 'pdiehi': 'player: death (hard hit)',
    'podth1': 'zombie death (1)', 'podth2': 'zombie death (2)', 'podth3': 'zombie death (3)',
    'bgdth1': 'imp death (1)', 'bgdth2': 'imp death (2)', 'sgtdth': 'demon death', 'cacdth': 'cacodemon death',
    'skldth': 'lost soul death', 'brsdth': 'baron death', 'cybdth': 'cyberdemon death', 'spidth': 'spider death',
    'bspdth': 'arachnotron death', 'vildth': 'arch-vile death', 'kntdth': 'hell knight death',
    'pedth': 'pain elemental death', 'skedth': 'revenant death', 'posact': 'zombie idle', 'bgact': 'imp idle',
    'dmact': 'demon idle', 'bspact': 'arachnotron idle', 'bspwlk': 'arachnotron footstep', 'vilact': 'arch-vile idle',
    'noway': 'player: "can\'t do that" (locked door etc.)', 'barexp': 'barrel explosion', 'punch': 'fist / knife hit',
    'hoof': 'cyberdemon footstep', 'metal': 'cyberdemon / spider footstep', 'chgun': 'chaingun (unused)',
    'tink': 'tink (small hit)', 'bdopn': 'blue door opens', 'bdcls': 'blue door closes', 'itmbk': 'item respawn',
    'flame': 'flame (fire attack)', 'flamst': 'flame start', 'getpow': 'powerup picked up', 'bospit': 'boss cube spit',
    'boscub': 'boss cube fly', 'bossit': 'boss brain wake', 'bospn': 'boss brain spawn', 'bosdth': 'boss brain death',
    'manatk': 'mancubus attack', 'mandth': 'mancubus death', 'sssit': 'SS soldier notices you', 'ssdth': 'SS soldier death',
    'keenpn': 'commander keen pain', 'keendt': 'commander keen death', 'skeact': 'revenant idle',
    'skesit': 'revenant notices you', 'skeatk': 'revenant attack', 'radio': 'radio chatter (map message)',
    'jump': 'player: jump', 'ptaunt': 'player: taunt', 'sgtatk': 'demon bite',
}


def write_sound_map(new, src, wavs, musics, root):
    sfx = parse_sfx(src)
    lines = ['# Begotten sound and music map', '',
             'Every sound below exists in begotten.wad as a very short silence. Replace a sound by putting a WAV (8-bit mono is exact;',
             '16-bit / stereo is converted) named exactly like the lump (e.g. `DSPISTOL.wav`) into the `sounds/` folder of the content tree,',
             'then pack. Music: put a MIDI / MUS / OGG named like the lump into `music/`.', '',
             'Class sounds (player pain / death / jump ...) are chosen per class by `BGCLASS` ("sound <slot> <sfx>"); the stock names are listed here.', '',
             '## Sounds', '', '| lump | engine id | used for |', '|---|---|---|']
    extra = []
    for ident, nm in sfx:
        short = ident[4:]
        note = SFX_NOTES.get(short, '')
        have = nm.upper() in wavs
        if have:
            lines.append('| `%s` | `%s` | %s |' % (nm.upper(), ident, note))
        else:
            extra.append(nm.upper())
    for nm in sorted(wavs):
        if nm not in {n.upper() for _, n in sfx}:
            lines.append('| `%s` | (custom) | %s |' % (nm, {'DSJUMP': 'player: jump (BGCLASS "sound jump")'}.get(nm, 'no engine reference found: leftover, safe to drop')))
    lines += ['', '## Other names the engine can play (not in the wad; add a WAV with the name to use one)', '',
              ', '.join('`%s`' % n for n in extra if not n.startswith('DSFRE')), '',
              '`DSFRE000` ... `DSFRE0xx`: spare slots for DEHACKED / MBF21 sounds.', '']
    lines += ['', '## Music', '', '| lump | where |', '|---|---|']
    where = {'runnin': 'MAP01', 'stalks': 'MAP02', 'countd': 'MAP03', 'betwee': 'MAP04', 'doom': 'MAP05',
             'the_da': 'MAP06', 'shawn': 'MAP07', 'ddtblu': 'MAP08', 'in_cit': 'MAP09', 'dead': 'MAP10',
             'stlks2': 'MAP11', 'theda2': 'MAP12', 'doom2': 'MAP13', 'ddtbl2': 'MAP14', 'runni2': 'MAP15',
             'dead2': 'MAP16', 'stlks3': 'MAP17', 'romero': 'MAP18', 'shawn2': 'MAP19', 'messag': 'MAP20',
             'count2': 'MAP21', 'ddtbl3': 'MAP22', 'ampie': 'MAP23', 'theda3': 'MAP24', 'adrian': 'MAP25',
             'messg2': 'MAP26', 'romer2': 'MAP27', 'tense': 'MAP28', 'shawn3': 'MAP29', 'openin': 'MAP30',
             'evil': 'MAP31', 'ultima': 'MAP32', 'read_m': 'text screens / finale', 'dm2ttl': 'title screen',
             'dm2int': 'intermission (level results)'}
    for m in musics:
        if 'D_' + m.upper() in {x for x in wavs} or True:
            lines.append('| `D_%s` | %s |' % (m.upper(), where.get(m, '')))
    open(os.path.join(root, 'SOUND_MAP.md'), 'w').write('\n'.join(lines) + '\n')


# --------------------------------------------------------------------------------------------- maps

def lump(name, data):
    return (name, data)


def build_map_wad(path, mapname, wall_tex, ceil_flat, floor_flat, exit_sw, secret):
    """one square room 512 x 512, split by a node at x = 256 into two convex sub-sectors"""
    # vertices
    V = [(0, 0), (0, 512), (128, 512), (192, 512), (320, 512), (384, 512), (512, 512), (512, 0), (256, 0), (256, 512)]
    vi = {v: i for i, v in enumerate(V)}
    # linedefs (clockwise, room on the right): (v1, v2, special, texture)
    L = [
        ((0, 0), (0, 512), 0, wall_tex),
        ((0, 512), (128, 512), 0, wall_tex),
        ((128, 512), (192, 512), 51 if secret else 0, 'SWS1' if secret else wall_tex),
        ((192, 512), (320, 512), 0, wall_tex),
        ((320, 512), (384, 512), 11, exit_sw),
        ((384, 512), (512, 512), 0, wall_tex),
        ((512, 512), (512, 0), 0, wall_tex),
        ((512, 0), (0, 0), 0, wall_tex),
    ]
    vert = b''.join(struct.pack('<hh', x, y) for x, y in V)
    ld = b''
    sd = b''
    for i, (a, b, sp, tx) in enumerate(L):
        ld += struct.pack('<hhhhhhh', vi[a], vi[b], 1, sp, 0, i, -1)
        sd += struct.pack('<hh8s8s8sh', 0, 0, b'-', b'-', tx.encode().ljust(8, b'\0'), 0)

    def ang(a, b):
        import math
        t = math.atan2(b[1] - a[1], b[0] - a[0])
        return int(round(t / (2 * math.pi) * 65536)) & 0xFFFF

    def seg(a, b, line):
        la = L[line][0]
        off = int(round(((a[0] - la[0]) ** 2 + (a[1] - la[1]) ** 2) ** 0.5))
        return struct.pack('<hhHhhh', vi[a], vi[b], ang(a, b), line, 0, off)

    west = [seg((0, 0), (0, 512), 0), seg((0, 512), (128, 512), 1), seg((128, 512), (192, 512), 2),
            seg((192, 512), (256, 512), 3), seg((256, 0), (0, 0), 7)]
    east = [seg((256, 512), (320, 512), 3), seg((320, 512), (384, 512), 4), seg((384, 512), (512, 512), 5),
            seg((512, 512), (512, 0), 6), seg((512, 0), (256, 0), 7)]
    segs = b''.join(west + east)
    ssec = struct.pack('<hh', len(west), 0) + struct.pack('<hh', len(east), len(west))
    # node: partition x = 256 running up; right side (east) = subsector 1, left (west) = subsector 0
    node = struct.pack('<hhhh', 256, 0, 0, 512)
    node += struct.pack('<hhhh', 512, 0, 256, 512)      # right bbox: top bottom left right
    node += struct.pack('<hhhh', 512, 0, 0, 256)        # left bbox
    node += struct.pack('<HH', 0x8001, 0x8000)
    sector = struct.pack('<hh8s8shhh', 0, 128, floor_flat.encode().ljust(8, b'\0'), ceil_flat.encode().ljust(8, b'\0'), 200, 0, 0)
    things = b''
    for i, (x, y) in enumerate([(128, 128), (384, 128), (128, 384), (384, 384)]):
        things += struct.pack('<hhhhh', x, y, 90, i + 1, 7)
    for i, (x, y) in enumerate([(64, 64), (448, 64), (64, 448), (448, 448)]):
        things += struct.pack('<hhhhh', x, y, 90, 11, 7)
    if mapname == 'MAP01':
        things += struct.pack('<hhhhh', 170, 230, 90, 2012, 7)     # a medikit ahead and to the right of the player start
        # every weapon, in a row across the middle of the room (for testing): shotgun, scattergun, chaingun, launcher, plasma, bfg
        for k, dt in enumerate((2001, 82, 2002, 2003, 2004, 2006)):
            things += struct.pack('<hhhhh', 90 + k * 66, 290, 90, dt, 7)
    lumps = [(mapname, b''), ('THINGS', things), ('LINEDEFS', ld), ('SIDEDEFS', sd), ('VERTEXES', vert),
             ('SEGS', segs), ('SSECTORS', ssec), ('NODES', node), ('SECTORS', sector), ('REJECT', b'\0'), ('BLOCKMAP', b'')]
    d = b'PWAD' + struct.pack('<ii', len(lumps), 12 + sum(len(x[1]) for x in lumps))
    pos = 12
    tbl = b''
    for n, b in lumps:
        d += b
        tbl += struct.pack('<ii8s', pos if b else 0, len(b), n.encode().ljust(8, b'\0'))
        pos += len(b)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(d + tbl)


UMAP_NEXT = {}


def do_maps(old, new, nmaps):
    um = open(os.path.join(old, 'data', 'UMAPINFO.txt')).read()
    secret_maps = set()
    for m in re.finditer(r'MAP\s+MAP(\d+)\s*\{(.*?)\}', um, re.S):
        if re.search(r'NextSecret', m.group(2), re.I):
            secret_maps.add(int(m.group(1)))
    secret_maps |= {15, 31}
    showcase = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'MAP01_showcase.wad')
    for n in range(1, nmaps + 1):
        if n == 1 and os.path.exists(showcase):     # the lighting showcase (tools/make_showcase_map.py)
            os.makedirs(os.path.join(new, 'maps'), exist_ok=True)
            shutil.copy(showcase, os.path.join(new, 'maps', 'MAP01.wad'))
            continue
        build_map_wad(os.path.join(new, 'maps', 'MAP%02d.wad' % n), 'MAP%02d' % n, 'WALL%02d' % n,
                      'F_SKY1' if n % 2 else 'CEIL1', ['FLOOR1', 'FLOOR2', 'FLOOR3'][n % 3],
                      'SWX1', n in secret_maps)
    return secret_maps


def write_umapinfo(old, new, nmaps):
    um = open(os.path.join(old, 'data', 'UMAPINFO.txt')).read()
    out = []
    seen = set()
    for m in re.finditer(r'MAP\s+MAP(\d+)\s*\{(.*?)\}', um, re.S):
        n = int(m.group(1))
        seen.add(n)
        body = m.group(2)
        lines = []
        for ln in body.strip().splitlines():
            s = ln.strip()
            if not s:
                continue
            key = s.split('=')[0].strip().lower()
            if key == 'levelname':
                s = 'LevelName = "%s"' % ('Gallery of Lights' if n == 1 else 'Placeholder %02d' % n)
            elif key == 'skytexture':
                s = 'SkyTexture = "SKY1"'
            elif key == 'music':
                mus = re.search(r'"(.*)"', s).group(1).upper()
                if not (mus.startswith('D_') and os.path.exists(os.path.join(new, 'music', mus + '.mid'))):
                    s = 'Music = "D_RUNNIN"'
            lines.append('    ' + s)
        out.append('MAP MAP%02d\n{\n%s\n}\n' % (n, '\n'.join(lines)))
    for n in range(1, nmaps + 1):
        if n in seen:
            continue
        body = ['    LevelName = "Placeholder %02d"' % n, '    SkyTexture = "SKY1"']
        out.append('MAP MAP%02d\n{\n%s\n}\n' % (n, '\n'.join(body)))
    open(os.path.join(new, 'data', 'UMAPINFO.txt'), 'w').write('\n'.join(out))


def write_switches(new):
    def ent(off, on):
        return off.encode().ljust(9, b'\0') + on.encode().ljust(9, b'\0') + struct.pack('<h', 3)
    d = ent('SWX1', 'SWX2') + ent('SWS1', 'SWS2') + (b'\0' * 18) + struct.pack('<h', 0)
    open(os.path.join(new, 'data', 'SWITCHES.lmp'), 'wb').write(d)


def write_endoom(new):
    rows = []
    msg = ['', '', '', '        B E G O T T E N', '', '        placeholder content - replace me', '']
    for r in range(25):
        t = msg[r] if r < len(msg) else ''
        t = t.ljust(80)
        rows.append(b''.join(bytes([ord(c), 0x07]) for c in t))
    open(os.path.join(new, 'palette', 'ENDOOM.lmp'), 'wb').write(b''.join(rows))


def write_palettes(new):
    """a structured palette family (tools/make_palette.py): PLAYPAL = base, PLAYPAL1 = crow (episode 1, night), PLAYPAL2 = golden (episode 2), PLAYPAL3 = alien (episode 3), and a BGPAL
    lump choosing one per map; the pictures are matched to PLAYPAL when packed"""
    import make_palette as mp
    base = mp.THEMES['base']()
    pal = mp.make_palette(base)
    cmap = mp.make_colormap(pal)
    pdir = os.path.join(new, 'palette')
    os.makedirs(pdir, exist_ok=True)
    open(os.path.join(pdir, 'PLAYPAL.lmp'), 'wb').write(mp.playpal_bytes(pal))
    open(os.path.join(pdir, 'COLORMAP.lmp'), 'wb').write(cmap)
    for n, th in ((1, 'crow'), (2, 'golden'), (3, 'alien')):
        open(os.path.join(pdir, 'PLAYPAL%d.lmp' % n), 'wb').write(mp.playpal_bytes(mp.make_palette(mp.THEMES[th]())))
    open(os.path.join(new, 'data', 'TRANMAP.lmp'), 'wb').write(mp.make_tranmap(pal))
    for nm, mul in mp.SECTOR_MAPS.items():
        os.makedirs(os.path.join(new, 'colormaps'), exist_ok=True)
        open(os.path.join(new, 'colormaps', nm + '.lmp'), 'wb').write(mp.tint_colormap(pal, cmap, mul))
    open(os.path.join(new, 'data', 'BGPAL.txt'), 'w').write(
        '# which palette each map uses: map <first>[-<last>] <n>   (0 = PLAYPAL, 1 = PLAYPAL1 ... 9 = PLAYPAL9)\n'
        '# the palettes come from tools/make_palette.py (0 = base, 1 = crow, 2 = golden, 3 = alien); the art is the same, only the colours change\n'
        'default 0\n'
        'map 1-10 1\n'
        'map 11-20 2\n'
        'map 21-32 3\n')


def write_class_weapon_map(new):
    """data/BGCLASS.txt: the soldier and the witch draw their own first-person weapons (see do_class_weapons)"""
    path = os.path.join(new, 'data', 'BGCLASS.txt')
    if not os.path.exists(path):
        return
    txt = open(path).read()
    if 'sprite PISG' in txt:
        return
    out = []
    for ln in txt.splitlines():
        out.append(ln)
        face = {'edit soldier': 'SLD', 'edit witch': 'WCH', 'edit swordsman': 'SWD'}.get(ln.strip())
        if face:
            out.append('  face %s' % face)          # HUD face set: SLDST00 ... (art_hud.CLASS_FACES)
        body = {'edit witch': 'WPLY', 'edit swordsman': 'XPLY'}.get(ln.strip())
        if body:
            out.append('  sprite PLAY %s' % body)   # the body (standing, pain, death): its own frames
        who = {'edit soldier': 0, 'edit witch': 1}.get(ln.strip())
        if who is not None:
            out.append('  # first-person weapons: the same guns, this class\' own hands')
            for stock, names in CLASS_WEAPONS.items():
                out.append('  sprite %s %s' % (stock, names[who]))
    # third person: every weapon has its own stand and shoot pictures (art_class_weapons.py); these replace the old shotgun-only poses
    import art_class_weapons as CW
    res, cur = [], None
    for ln in out:
        s = ln.strip()
        if s.startswith('edit '):
            cur = s[5:]
        if s.startswith(('pose stand SRFL', 'pose shoot SRFS')):
            continue
        res.append(ln)
        if s.startswith('pose parry' if cur == 'swordsman' else 'pose jump'):
            c = {'soldier': 'S', 'witch': 'W', 'swordsman': 'X'}[cur]
            if c == 'X':
                res.append('  pose stand X1FI 1 4')
                # the stab: sword drawn back (still, walking, in the air), the lunge (ground, air)
                res.append('  pose charge X5FI')
                res.append('  pose chargerun X6FI 6 3')
                res.append('  pose chargejump X7FI')
                res.append('  pose stab X8FI')
                res.append('  pose stabair X9FI')
                continue
            res.append('  pose stand %s1PI 1 4' % c)
            for w, code in CW.CODES:
                res.append('  pose stand %s1%s 1 4 weapon %s' % (c, code, w))
                if w in CW.LEAD_POSE:
                    res.append('  pose shoot %s2%s %d %d weapon %s' % ((c, code) + CW.LEAD_POSE[w] + (w,)))
                else:
                    res.append('  pose shoot %s2%s 3 3 weapon %s' % (c, code, w))
                res.append('  pose run   %s3%s 6 3 weapon %s' % (c, code, w))
                res.append('  pose jump  %s4%s 3 4 weapon %s' % (c, code, w))
    open(path, 'w').write('\n'.join(res) + '\n')


# --------------------------------------------------------------------------------------------- main

KEEP_DATA_SKIP = re.compile(r'^(DP\w+|DS\w+|DBIGFONT|DFH|BLOOD4|DRAMA|SETMAIN|FREEDOOM|ANIMDEFS|UMAPINFO|SWITCHES)\.(lmp|txt)$', re.I)


ART_SCALE = 2.5          # picture pixels per map unit for sprites, weapons and wall textures (BEGOTTEN_ART_SCALE overrides)
FLAT_SIZE = 128          # flats: 64 map units wide, so 128 pixels = 2 per unit (a power of two is needed)


def upscale_sprites(new, k):
    """every sprite and first-person weapon gets k times the pixels: the picture is enlarged (nearest neighbour, hot spots
    multiplied) and its labels are drawn again at the new size, so text stays sharp. data/BGSCALE.txt tells the engine."""
    count = 0
    for p in glob.glob(os.path.join(new, 'sprites', '**', '*.png'), recursive=True):
        raw = open(p, 'rb').read()
        off = png_offsets(raw)
        img = Image.open(io.BytesIO(raw)).convert('RGBA')
        w2, h2 = max(1, int(round(img.width * k))), max(1, int(round(img.height * k)))
        img = img.resize((w2, h2), Image.NEAREST)
        for text, box, color, mx, mn in TEXTS.get(os.path.abspath(p), []):
            hb = (int(round(box[0] * k)), int(round(box[1] * k)), int(round(box[2] * k)), int(round(box[3] * k)))
            put_text(img, text, hb, color, int(mx * k), max(5, int(mn * k)))
        save(p, img, (int(round(off[0] * k)), int(round(off[1] * k))) if off else None)
        count += 1
    return count



def detailed_medikit(w, h):
    """a hand-drawn style medikit (white case, red cross, steel latches and handle) at w x h pixels, drawn 4x too big and
    shrunk for smooth edges. Meant to show what a sprite looks like at the higher resolution, not as final art."""
    import numpy as np
    SS = 4
    W, H = w * SS, h * SS
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def gradient(box, top, bottom, left_light=0.12):
        x0, y0, x1, y1 = box
        gw, gh = x1 - x0, y1 - y0
        t = np.linspace(0, 1, gh)[:, None]
        xs = np.linspace(0, 1, gw)[None, :]
        arr = np.zeros((gh, gw, 4), dtype=np.float64)
        for c in range(3):
            arr[:, :, c] = top[c] * (1 - t) + bottom[c] * t
        arr[:, :, :3] *= (1.0 + left_light * (0.5 - xs))[:, :, None]
        arr[:, :, 3] = 255
        return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), 'RGBA')

    outline = (52, 38, 40, 255)
    # ground shadow
    d.ellipse([W * 0.04, H * 0.86, W * 0.96, H * 1.0], fill=(10, 10, 20, 110))
    # handle
    hx0, hx1 = W * 0.34, W * 0.66
    d.rounded_rectangle([hx0, H * 0.04, hx1, H * 0.30], radius=SS * 4, outline=outline, width=SS * 2, fill=None)
    d.rounded_rectangle([hx0 + SS * 2, H * 0.06, hx1 - SS * 2, H * 0.30], radius=SS * 3, outline=(120, 124, 136, 255), width=SS * 2)
    # case body
    bx0, by0, bx1, by1 = int(W * 0.04), int(H * 0.22), int(W * 0.96), int(H * 0.90)
    body = gradient((bx0, by0, bx1, by1), (250, 246, 232), (176, 168, 160))
    mask = Image.new('L', (bx1 - bx0, by1 - by0), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, bx1 - bx0 - 1, by1 - by0 - 1], radius=SS * 6, fill=255)
    img.paste(body, (bx0, by0), mask)
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=SS * 6, outline=outline, width=SS * 2)
    # lid seam and shadow under it
    sy = int(by0 + (by1 - by0) * 0.30)
    d.line([bx0 + SS * 2, sy, bx1 - SS * 2, sy], fill=outline, width=SS * 2)
    d.line([bx0 + SS * 3, sy + SS * 3, bx1 - SS * 3, sy + SS * 3], fill=(150, 140, 136, 255), width=SS * 2)
    d.line([bx0 + SS * 4, by0 + SS * 3, bx1 - SS * 4, by0 + SS * 3], fill=(255, 255, 250, 255), width=SS)
    # steel latches
    for cx in (W * 0.20, W * 0.80):
        lx0, lx1, ly0, ly1 = int(cx - W * 0.05), int(cx + W * 0.05), sy - SS * 5, sy + SS * 13
        latch = gradient((lx0, ly0, lx1, ly1), (214, 218, 228), (96, 100, 114), 0.2)
        lm = Image.new('L', (lx1 - lx0, ly1 - ly0), 0)
        ImageDraw.Draw(lm).rounded_rectangle([0, 0, lx1 - lx0 - 1, ly1 - ly0 - 1], radius=SS * 2, fill=255)
        img.paste(latch, (lx0, ly0), lm)
        d.rounded_rectangle([lx0, ly0, lx1, ly1], radius=SS * 2, outline=outline, width=SS)
        d.ellipse([cx - SS * 2, ly0 + SS * 4 - SS * 2, cx + SS * 2, ly0 + SS * 4 + SS * 2], fill=(60, 62, 72, 255))
    # a few scuffs
    rng = np.random.RandomState(7)
    for _ in range(26):
        x0_ = rng.randint(bx0 + SS * 6, bx1 - SS * 12)
        y0_ = rng.randint(sy + SS * 8, by1 - SS * 4)
        d.line([x0_, y0_, x0_ + rng.randint(SS * 2, SS * 8), y0_ + rng.randint(-SS, SS)], fill=(120, 108, 100, 150), width=SS)
    # red cross, with a dark edge and a highlight
    cx, cy = W * 0.5, by0 + (by1 - by0) * 0.64
    arm, thick = (by1 - by0) * 0.26, (by1 - by0) * 0.15
    for off, col in (((SS * 1.5, SS * 1.5), (120, 14, 18, 255)), ((0, 0), (208, 30, 34, 255))):
        ox, oy = off
        d.rectangle([cx - arm + ox, cy - thick / 2 + oy, cx + arm + ox, cy + thick / 2 + oy], fill=col)
        d.rectangle([cx - thick / 2 + ox, cy - arm + oy, cx + thick / 2 + ox, cy + arm + oy], fill=col)
    d.line([cx - arm + SS, cy - thick / 2 + SS, cx + arm - SS, cy - thick / 2 + SS], fill=(255, 130, 120, 255), width=SS)
    d.line([cx - thick / 2 + SS, cy - arm + SS, cx - thick / 2 + SS, cy + arm - SS], fill=(255, 130, 120, 255), width=SS)
    # rivets and a few scuffs
    for rx, ry in ((0.10, 0.34), (0.90, 0.34), (0.10, 0.84), (0.90, 0.84)):
        px_, py_ = bx0 + (bx1 - bx0) * (rx - 0.04) / 0.92, by0 + (by1 - by0) * (ry - 0.22) / 0.68
        d.ellipse([px_ - SS * 2, py_ - SS * 2, px_ + SS * 2, py_ + SS * 2], fill=(130, 132, 142, 255), outline=outline)
    return img.resize((w, h), Image.LANCZOS)


def write_detail_item(new, k):
    """MEDIA0: the medikit, drawn in detail at k pixels per map unit (see dsda/bgscale.h)"""
    path = os.path.join(new, 'sprites', 'MEDI', 'MEDIA0.png')
    raw = open(path, 'rb').read()
    off = png_offsets(raw) or (0, 0)
    w, h = Image.open(io.BytesIO(raw)).size
    save(path, detailed_medikit(w, h), (off[0], off[1]))


def main():
    old, new = sys.argv[1:3]
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'prboom2', 'src')
    if os.path.exists(new):
        shutil.rmtree(new)
    os.makedirs(new)
    # kept as they are: tables and the engine's own lumps
    for g in ('palette', 'colormaps'):
        shutil.copytree(os.path.join(old, g), os.path.join(new, g))
    os.makedirs(os.path.join(new, 'data'))
    for f in os.listdir(os.path.join(old, 'data')):
        if not KEEP_DATA_SKIP.match(f):
            shutil.copy(os.path.join(old, 'data', f), os.path.join(new, 'data', f))
    shutil.copy(os.path.join(old, 'wad.cfg'), os.path.join(new, 'wad.cfg'))

    print('sprites  ', do_sprites(old, new))
    print('graphics ', do_graphics(old, new))
    try:
        import art_hud
        print('hud art  ', art_hud.apply(new, save))
    except ImportError:
        pass
    nmaps = 32
    k = float(os.environ.get('BEGOTTEN_ART_SCALE', ART_SCALE))
    do_textures_flats(new, nmaps, k)
    # sounds: every name the engine table knows, plus every name the old wad had
    wavs = {os.path.splitext(f)[0] for f in os.listdir(os.path.join(old, 'sounds'))}
    sfx = parse_sfx(src)
    names = set(wavs)
    for ident, nm in sfx:
        if nm.lower() == 'dsdgsit':      # the engine's own extra sounds start here (they live in begotten-data.wad)
            break
        names.add(nm.upper())
    for nm in sorted(names):
        silent_wav(os.path.join(new, 'sounds', nm + '.wav'))
    weather_wavs(new)
    musics = parse_music(src)
    mnames = {'D_' + m.upper() for m in musics if not re.match(r'^(e\dm\d|inter|intro|bunny|victor|introa|credit|musinfo)$', m)}
    mnames |= {os.path.splitext(f)[0] for f in os.listdir(os.path.join(old, 'music'))}
    for nm in sorted(mnames):
        silent_midi(os.path.join(new, 'music', nm + '.mid'))
    do_maps(old, new, nmaps)
    if os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'MAP01_showcase.wad')):
        open(os.path.join(new, 'data', 'BGLIGHT.txt'), 'w').write(
            '# Begotten lighting additions for this wad (see LIGHTING.md).\n'
            '# MAP01 (the Gallery of Lights) has a thin cold haze: far walls fade into it, and lights glow in it.\n'
            'fog map MAP01 color 34 40 54 density 0.0018 scatter 0.6\n'
            '# and a low sun in the sun court: the angle is where the sun is (0 east, 90 north, 250 south-south-west)\n'
            '# per-sector fog: the Chamber of Colour (sector tag 5) is thick and violet, the Sun court (tag 6) is clear\n'
            'fog map MAP01 sector 5 color 56 34 74 density 0.0035 scatter 0.5 max 0.85\n'
            'fog map MAP01 sector 6 density 0\n'
            'color sunlight 255 236 200\n'
            'sun map MAP01 angle 250 elevation 50 color sunlight strength 0.9\n'
            '# rain falls in the sun court (the only open sky), with thunder about every 12 seconds. For snow: `weather map MAPxx snow`\n'
            'weather map MAP01 rain density 1 wind 3 thunder 12\n')
    write_umapinfo(old, new, nmaps)
    write_switches(new)
    write_endoom(new)
    write_class_weapon_map(new)
    write_palettes(new)
    if k != 1:
        print('hi-res sprites', upscale_sprites(new, k))
    try:
        import art_apply
        done = art_apply.apply(new, k, procs=int(os.environ.get('BEGOTTEN_ART_PROCS', '2')))
        print('drawn sprites', sum(done.values()), 'in', len(done), 'families')
    except ImportError:
        pass
    if k != 1:
        write_detail_item(new, k)
    open(os.path.join(new, 'data', 'BGSCALE.txt'), 'w').write(
        '# pixels per map unit of the art (see dsda/bgscale.h); flats need no line: a 128x128 flat is drawn over 64x64 units\n'
        'sprites %g\ntextures %g\nweapons %g\n' % (k, k, k))
    write_sound_map(new, src, names, [m for m in musics if 'D_' + m.upper() in mnames], new)
    print('done:', new)


main()
