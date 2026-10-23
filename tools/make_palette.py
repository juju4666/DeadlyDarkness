#!/usr/bin/env python3
"""Begotten: build a structured palette family (PLAYPAL, COLORMAP, TRANMAP, sector colour maps) from themes.

Why "structured": the 256 colours are laid out as 15 ramps of 16 shades plus 1 ramp of 15 fullbright ("glow") colours:

    index  0-15    ramp  0   neutral        (index 0 is the lightest shade, 15 the darkest)
    index 16-31    ramp  1   red
    ...            ...       (see RAMP_NAMES)
    index 224-239  ramp 14   flesh / skin
    index 240-254  glow      fullbright: these colours map to themselves at every light level ("shine in the dark")
    index 255      spare     never used by art (the engine treats it as the transparent colour); a dark green in
                             every theme, used for the automap background

The COLORMAP only ever moves a colour along its own ramp (towards the dark end), so it does NOT depend on the colours
themselves: every theme of the family shares the same COLORMAP and the same art.  Picture lumps are matched to the base
palette when packed; a theme is a different set of 256 RGB values for the same indices, which the engine can switch
per map (BGPALETTE lump) to recolour everything at once - weapons, HUD, monsters, walls.  Art that should change more
between themes (monsters) simply uses ramps the themes bend further.

  make_palette.py TREE [--base base] [--variant 1=crow] [--variant 2=...] [--preview preview.png] [--list]

writes   TREE/palette/PLAYPAL.lmp (the base theme, 14 tinted palettes), PLAYPAL1..9.lmp (variants), COLORMAP.lmp,
         TREE/data/TRANMAP.lmp (50% blend, base theme) and TREE/colormaps/*.lmp (tinted sector colour maps).
Needs Python 3.8+, numpy and Pillow.  Themes are plain dictionaries below: copy one and change the numbers.
"""
import argparse
import colorsys
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

RAMP_NAMES = ['neutral', 'red', 'orange', 'yellow', 'lime', 'green', 'teal', 'cyan', 'azure', 'blue', 'violet',
              'magenta', 'rose', 'earth', 'flesh']
NRAMPS = len(RAMP_NAMES)       # 15 ramps of 16 shades
GLOW0 = 240                    # first glow index
SHADES = 16

# ---------------------------------------------------------------------------------------------------- themes
# Every ramp: hue (degrees), sat = (lightest, middle, darkest) saturation, val = (lightest, darkest) value.
# Highlights drift towards `warm` hue, shadows towards `shadow` hue; strength 0..1.

BASE_HUES = [0, 0, 24, 52, 84, 125, 165, 190, 215, 240, 272, 305, 340, 28, 18]
BASE_SAT = [(0.0, 0.0, 0.0), (0.22, 0.75, 0.6), (0.2, 0.8, 0.65), (0.2, 0.8, 0.7), (0.2, 0.75, 0.65), (0.2, 0.7, 0.6),
            (0.2, 0.7, 0.6), (0.2, 0.7, 0.6), (0.2, 0.7, 0.6), (0.2, 0.7, 0.6), (0.2, 0.65, 0.55), (0.2, 0.65, 0.55),
            (0.2, 0.6, 0.5), (0.18, 0.5, 0.45), (0.16, 0.5, 0.45)]
BASE_VAL = [(1.0, 0.07)] * NRAMPS


def theme_base():
    return dict(name='base', hues=list(BASE_HUES), sat=[tuple(s) for s in BASE_SAT], val=list(BASE_VAL),
                shadow_hue=None, shadow_strength=0.0, warm_hue=None, warm_strength=0.0, gamma=1.0,
                glow=[(0, 0.0, 1.0), (50, 0.25, 1.0), (55, 0.6, 1.0), (45, 0.9, 1.0), (30, 1.0, 1.0), (15, 1.0, 1.0),
                      (0, 1.0, 0.95), (330, 0.8, 1.0), (285, 0.7, 1.0), (240, 0.7, 1.0), (195, 0.8, 1.0),
                      (165, 0.8, 0.95), (125, 0.8, 0.95), (85, 0.8, 0.95), (0, 0.0, 0.8)])


def remap_hue(h, pts):
    """piecewise-linear hue remap through (from, to) control points (hues on a circle)"""
    pts = sorted(pts)
    xs = [p[0] for p in pts] + [pts[0][0] + 360]
    ys = [p[1] for p in pts]
    ys.append(ys[0] + 360)
    # keep the target monotone around the circle
    for i in range(1, len(ys)):
        while ys[i] < ys[i - 1]:
            ys[i] += 360
    h = h % 360
    if h < xs[0]:
        h += 360
    for i in range(len(xs) - 1):
        if xs[i] <= h <= xs[i + 1]:
            t = (h - xs[i]) / max(1e-9, xs[i + 1] - xs[i])
            return (ys[i] + (ys[i + 1] - ys[i]) * t) % 360
    return h


def theme_crow():
    """Inspired by the look of 'The Crow' (1994): rain-soaked night, midnight blues and bruised purples, cold moonlight
    greys, crimson blood, and warm oranges where fire is - the orange stands out because everything around it is cold.
    (Colour reasoning only, not taken from any frame of the film.)"""
    t = theme_base()
    t['name'] = 'crow'
    # each ramp keeps its slot but is re-coloured: cold steels and blues in the middle, crimson and fire at the warm end
    t['hues'] = [228, 354, 22, 40, 200, 175, 195, 210, 222, 236, 258, 280, 305, 20, 18]
    sat_mid = [0.22, 0.85, 0.95, 0.85, 0.35, 0.45, 0.55, 0.5, 0.6, 0.78, 0.68, 0.62, 0.5, 0.35, 0.28]
    t['sat'] = [(0.06 if i else 0.04, m, m * 0.85) for i, m in enumerate(sat_mid)]
    t['sat'][1] = (0.2, 0.85, 0.75)
    t['sat'][2] = (0.2, 0.95, 0.8)
    t['sat'][3] = (0.25, 0.85, 0.7)
    t['val'] = [(0.92, 0.06)] * NRAMPS
    t['val'][1] = (0.95, 0.06)
    t['val'][2] = (1.0, 0.06)
    t['val'][3] = (1.0, 0.06)
    t['val'][14] = (0.95, 0.06)
    t['gamma'] = 1.15
    t['shadow_hue'] = 252
    t['shadow_strength'] = 0.5
    t['warm_hue'] = 30
    t['warm_strength'] = 0.0
    # glow: fire first (white-hot -> yellow -> orange -> red), then a few cold lights (moonlight, blue, violet)
    t['glow'] = [(40, 0.12, 1.0), (45, 0.45, 1.0), (40, 0.75, 1.0), (32, 0.9, 1.0), (26, 1.0, 1.0), (20, 1.0, 0.95),
                 (12, 1.0, 0.85), (355, 0.95, 0.75), (300, 0.55, 0.9), (262, 0.6, 0.95), (235, 0.45, 1.0),
                 (215, 0.3, 1.0), (205, 0.12, 1.0), (180, 0.45, 0.9), (0, 0.0, 0.85)]
    return t


def theme_dusk():
    """a second, warmer family member: sun-bleached ochres and dusty teal, for variety between episodes"""
    t = theme_base()
    t['name'] = 'dusk'
    pts = [(0, 6), (24, 28), (52, 46), (84, 62), (125, 90), (165, 168), (190, 184), (215, 200), (240, 218), (272, 285),
           (305, 322), (340, 350)]
    t['hues'] = [remap_hue(h, pts) for h in BASE_HUES]
    t['hues'][0] = 38
    t['sat'] = [(a * 0.85, b * 0.85, c * 0.8) for a, b, c in BASE_SAT]
    t['sat'][0] = (0.06, 0.16, 0.3)
    t['gamma'] = 1.1
    t['shadow_hue'] = 285
    t['shadow_strength'] = 0.35
    return t


def theme_golden():
    """Episode 2: medieval villages at golden hour. Warm, saturated and lively: low sun on everything (highlights lean
    amber), long violet-blue shadows, warm greys, sky blues kept clear so the colours stay cheerful."""
    t = theme_base()
    t['name'] = 'golden'
    t['hues'] = [40, 2, 22, 46, 78, 112, 160, 192, 212, 232, 268, 318, 346, 26, 20]
    t['sat'] = [(0.0, 0.14, 0.22)] + [(a * 0.9, min(1.0, b * 1.12), c * 1.05) for a, b, c in BASE_SAT[1:]]
    t['val'] = [(1.0, 0.09)] * NRAMPS
    t['gamma'] = 0.95
    t['warm_hue'] = 42
    t['warm_strength'] = 0.45
    t['shadow_hue'] = 275
    t['shadow_strength'] = 0.35
    t['glow'] = [(48, 0.18, 1.0), (46, 0.5, 1.0), (42, 0.85, 1.0), (34, 1.0, 1.0), (24, 1.0, 1.0), (12, 1.0, 0.95),
                 (355, 0.9, 0.9), (330, 0.7, 1.0), (290, 0.6, 1.0), (240, 0.55, 1.0), (200, 0.7, 1.0),
                 (170, 0.8, 0.95), (125, 0.8, 0.95), (85, 0.85, 0.95), (45, 0.1, 0.85)]
    return t


def theme_alien():
    """Episode 3: a spaceship and an alien planet. Things are not quite right: blues slide to violet, yellows to
    acid green, greens to teal, reds to crimson-magenta, oranges to a sick yellow-green. Muted, with deep violet shadows,
    but lighter than the night of episode 1."""
    t = theme_base()
    t['name'] = 'alien'
    pts = [(0, 344), (24, 78), (52, 112), (84, 150), (125, 178), (165, 205), (190, 240), (215, 268), (240, 288),
           (272, 318), (305, 340), (340, 352)]
    t['hues'] = [remap_hue(h, pts) for h in BASE_HUES]
    t['hues'][0] = 268
    t['sat'] = [(a * 0.8, b * 0.78, c * 0.75) for a, b, c in BASE_SAT]
    t['sat'][0] = (0.05, 0.14, 0.3)
    t['val'] = [(0.9, 0.06)] * NRAMPS
    t['gamma'] = 1.2
    t['shadow_hue'] = 282
    t['shadow_strength'] = 0.5
    t['warm_hue'] = 95
    t['warm_strength'] = 0.15
    t['glow'] = [(remap_hue(h, pts) if sat_ > 0.05 else h, sat_, v) for h, sat_, v in theme_base()['glow']]
    return t


THEMES = {'base': theme_base, 'crow': theme_crow, 'dusk': theme_dusk, 'golden': theme_golden, 'alien': theme_alien}


# ---------------------------------------------------------------------------------------------------- palette
def hue_mix(h1, h2, t):
    """blend two hues along the shorter way round"""
    d = ((h2 - h1 + 180) % 360) - 180
    return (h1 + d * t) % 360


def make_ramp(theme, r):
    cols = []
    hue = theme['hues'][r]
    s_hi, s_mid, s_lo = theme['sat'][r]
    v_hi, v_lo = theme['val'][r]
    gamma = theme['gamma']
    for s in range(SHADES):
        t = s / (SHADES - 1.0)                              # 0 lightest .. 1 darkest
        v = v_lo + (v_hi - v_lo) * (1.0 - t) ** gamma
        if t < 0.5:
            sat = s_hi + (s_mid - s_hi) * (t / 0.5)
        else:
            sat = s_mid + (s_lo - s_mid) * ((t - 0.5) / 0.5)
        h = hue
        if theme['warm_hue'] is not None and theme['warm_strength'] > 0:
            h = hue_mix(h, theme['warm_hue'], theme['warm_strength'] * (1 - t) ** 2)
        if theme['shadow_hue'] is not None and theme['shadow_strength'] > 0:
            k = theme['shadow_strength'] * t ** 1.5
            h = hue_mix(h, theme['shadow_hue'], k)
            sat = sat + (0.5 - sat) * 0.0
        if r == 0 and theme['sat'][0][0] == 0.0:
            sat = 0.0
        rr, gg, bb = colorsys.hsv_to_rgb(h / 360.0, max(0.0, min(1.0, sat)), max(0.0, min(1.0, v)))
        cols.append((int(round(rr * 255)), int(round(gg * 255)), int(round(bb * 255))))
    return cols


def make_palette(theme):
    pal = []
    for r in range(NRAMPS):
        pal += make_ramp(theme, r)
    pal[SHADES - 1] = (0, 0, 0)         # the end of the neutral ramp is black
    # the neutral ramp is stored the other way round (index 0 = black .. 15 = white): the engine treats index 0 as black
    # (screen fills, borders, automap, wipes), so it must be. The colour maps know about this (see ramp_index).
    pal[:SHADES] = pal[:SHADES][::-1]
    for h, s, v in theme['glow']:
        rr, gg, bb = colorsys.hsv_to_rgb(h / 360.0, s, v)
        pal.append((int(round(rr * 255)), int(round(gg * 255)), int(round(bb * 255))))
    pal.append((12, 40, 22))           # 255: spare (art never uses it; transparent in patches). A dark green in every theme: the automap background
    assert len(pal) == 256
    return pal


def tinted(pal, target, t):
    return [tuple(int(round(c[k] * (1 - t) + target[k] * t)) for k in range(3)) for c in pal]


def playpal_bytes(pal):
    """26 palettes: normal, 8 pain (red), 4 pickup (gold), 1 radiation suit (green), 12 tints (blue, yellow, purple)"""
    out = [pal]
    for i in range(1, 9):
        out.append(tinted(pal, (255, 0, 0), 0.75 * i / 9.0))
    for i in range(1, 5):
        out.append(tinted(pal, (215, 186, 69), 0.06 * i))
    out.append(tinted(pal, (0, 255, 0), 0.125))
    # 14..25 (Begotten): blue (health, immortality), yellow (shield, boots), purple (adrenaline); four strengths each
    for target in ((50, 100, 255), (255, 215, 40), (170, 60, 255)):
        for pct in (0.05, 0.10, 0.17, 0.26):
            out.append(tinted(pal, target, pct))
    return b''.join(bytes(c) for p in out for c in p)


# ---------------------------------------------------------------------------------------------------- colormap
def nearest(pal_np, rgb):
    """index of the palette colour nearest to every row of rgb (n,3), searching all but the spare 255"""
    d = ((rgb[:, None, :].astype(np.int32) - pal_np[None, :255, :].astype(np.int32)) ** 2).sum(axis=2)
    return d.argmin(axis=1)


def ramp_index(r, s):
    """palette index of shade s (0 lightest .. 15 darkest) in ramp r; the neutral ramp is stored darkest first"""
    return SHADES - 1 - s if r == 0 else r * SHADES + s


def make_colormap(pal):
    """34 maps: 0..31 from full light to dark (each colour slides along its own ramp), 32 invulnerability, 33 black"""
    maps = []
    for lvl in range(32):
        f = lvl / 31.0
        m = bytearray(256)
        for i in range(256):
            if i >= GLOW0:
                m[i] = i                                    # glow and spare: unchanged at every level
            else:
                r, s = divmod(i, SHADES)
                if r == 0:
                    s = SHADES - 1 - s
                m[i] = ramp_index(r, int(round(s + (SHADES - 1 - s) * f)))
        maps.append(bytes(m))
    pal_np = np.array(pal, dtype=np.int32)
    lum = (0.299 * pal_np[:, 0] + 0.587 * pal_np[:, 1] + 0.114 * pal_np[:, 2]) / 255.0
    inv = bytearray(256)
    for i in range(256):
        inv_l = 1.0 - lum[i]                               # the neutral ramp, lightest..darkest, picked by inverted lightness
        best = min(range(SHADES), key=lambda s: abs(lum[ramp_index(0, s)] - inv_l))
        inv[i] = ramp_index(0, best)
    maps.append(bytes(inv))
    maps.append(bytes([0]) * 256)                           # black
    return b''.join(maps)


def make_tranmap(pal):
    """TRANMAP[fg * 256 + bg]: the palette colour nearest to a 50% blend (Boom translucency)"""
    pal_np = np.array(pal, dtype=np.int32)
    out = np.zeros(65536, dtype=np.uint8)
    for fg in range(256):
        blend = (pal_np[fg][None, :] + pal_np) // 2
        out[fg * 256:(fg + 1) * 256] = nearest(pal_np, blend)
    return out.tobytes()


SECTOR_MAPS = {'RADMAP': (60, 255, 80), 'HELLMAP': (255, 40, 30), 'REDSMAP': (255, 80, 80), 'MLREDMAP': (200, 40, 40),
               'BRONMAP': (255, 190, 110)}


def tint_colormap(pal, cmap, mul):
    """a sector colour map: the normal maps with every colour multiplied by `mul`, re-matched to the palette"""
    pal_np = np.array(pal, dtype=np.int32)
    out = bytearray()
    for lvl in range(34):
        row = np.frombuffer(cmap[lvl * 256:(lvl + 1) * 256], dtype=np.uint8)
        rgb = pal_np[row]
        rgb = (rgb * np.array(mul, dtype=np.float64)[None, :] / 255.0).astype(np.int32)
        out += bytes(nearest(pal_np, rgb).astype(np.uint8))
    return bytes(out)


# ---------------------------------------------------------------------------------------------------- preview
def preview(themes, path):
    cell = 14
    pad = 12
    names = [t['name'] for t in themes]
    W = pad * 2 + 16 * cell + 4 * 6 * 0 + 40
    rows = len(themes)
    block_h = 16 * cell + 90
    img = Image.new('RGB', (pad * 2 + 16 * cell * rows + pad * (rows - 1), pad * 2 + block_h + 40), (24, 24, 28))
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None
    cmap = None
    for k, theme in enumerate(themes):
        pal = make_palette(theme)
        if cmap is None:
            cmap = make_colormap(pal)
        x0 = pad + k * (16 * cell + pad)
        d.text((x0, 4), theme['name'], fill=(255, 255, 255), font=font)
        for i in range(256):
            r, s = divmod(i, 16)
            x, y = x0 + s * cell, pad + 8 + r * cell
            d.rectangle([x, y, x + cell - 1, y + cell - 1], fill=pal[i])
        # lighting: each ramp's middle colour at light levels 0, 8, 16, 24, 31
        y = pad + 8 + 16 * cell + 8
        for r in range(NRAMPS):
            base = r * SHADES + 3
            for j, lvl in enumerate((0, 8, 16, 24, 31)):
                idx = cmap[lvl * 256 + base]
                x = x0 + r * 17 + 0
                d.rectangle([x, y + j * 12, x + 15, y + j * 12 + 11], fill=pal[idx])
        # tint palettes: the middle of the red ramp and the neutral ramp under pain / pickup / radiation suit
        pp = playpal_bytes(pal)
        y2 = y + 5 * 12 + 6
        for p in range(14):
            for r, idx in enumerate((2 * 16 + 3, 8 * 16 + 4, 0 * 16 + 4)):
                c = tuple(pp[p * 768 + idx * 3:p * 768 + idx * 3 + 3])
                d.rectangle([x0 + p * 17, y2 + r * 10, x0 + p * 17 + 15, y2 + r * 10 + 9], fill=c)
    img.save(path)


# ---------------------------------------------------------------------------------------------------- main
def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tree', nargs='?', help='content tree (the folder pack_wad.py packs)')
    ap.add_argument('--base', default='base', help='theme of PLAYPAL (default base)')
    ap.add_argument('--variant', action='append', default=[], metavar='N=THEME',
                    help='write PLAYPAL<N> (N 1..9) from a theme; repeat for more')
    ap.add_argument('--preview', metavar='PNG', help='write a swatch sheet of the themes used')
    ap.add_argument('--list', action='store_true', help='list the themes')
    a = ap.parse_args()
    if a.list:
        for n in THEMES:
            print(n)
        return
    if not a.tree:
        ap.error('give the content tree folder')
    base = THEMES[a.base]()
    pal = make_palette(base)
    cmap = make_colormap(pal)
    write(os.path.join(a.tree, 'palette', 'PLAYPAL.lmp'), playpal_bytes(pal))
    write(os.path.join(a.tree, 'palette', 'COLORMAP.lmp'), cmap)
    write(os.path.join(a.tree, 'data', 'TRANMAP.lmp'), make_tranmap(pal))
    for nm, mul in SECTOR_MAPS.items():
        write(os.path.join(a.tree, 'colormaps', nm + '.lmp'), tint_colormap(pal, cmap, mul))
    used = [base]
    for v in a.variant:
        n, th = v.split('=')
        n = int(n)
        if not 1 <= n <= 9:
            ap.error('variant number must be 1..9')
        t = THEMES[th]()
        used.append(t)
        write(os.path.join(a.tree, 'palette', 'PLAYPAL%d.lmp' % n), playpal_bytes(make_palette(t)))
        print('PLAYPAL%d = %s' % (n, th))
    print('PLAYPAL = %s, COLORMAP, TRANMAP, %d sector colour maps' % (base['name'], len(SECTOR_MAPS)))
    if a.preview:
        preview(used, a.preview)
        print('preview:', a.preview)


if __name__ == '__main__':
    main()
