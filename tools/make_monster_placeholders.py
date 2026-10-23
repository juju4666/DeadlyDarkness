#!/usr/bin/env python3
"""Begotten: make PLACEHOLDER sprites for the monster roster (and their projectiles).

usage: make_monster_placeholders.py CONTENT_TREE

For every new 4-letter sprite name this copies all the lumps of a Doom sprite (same frames, same
rotations, same hot spots) into CONTENT_TREE/sprites/<NAME>/ with the colours rotated, then removes the Doom sprites the roster replaces (REPLACED), so the monsters
are playable and tell apart straight away.  Replace a folder with real art whenever you have it;
the engine only cares about the frame letters (the states are copies of the original monsters'
chains - see BEGOTTEN_ENGINE.md).  Existing folders are NOT overwritten (so your art is safe).
"""
import colorsys, os, re, struct, sys, zlib
from PIL import Image

# new name, original sprite, hue shift (degrees), saturation x, brightness x
SPRITES = [
    # Modern
    ("RTRP", "SPOS", 20, 1.2, 0.9), ("STRP", "SSWV", 200, 1.3, 1.1),
    # Medieval
    ("WWLF", "SARG", 30, 0.5, 0.7), ("FELM", "TROO", 15, 1.6, 1.3), ("MWIZ", "SKEL", 260, 1.4, 0.9),
    ("APPR", "BOS2", 280, 0.8, 1.1), ("MINI", "BOSS", 300, 0.9, 1.2), ("GLEM", "FATT", 40, 0.35, 0.9),
    # Alien
    ("MNDE", "VILE", 140, 1.2, 1.0), ("AEGS", "SKUL", 120, 1.3, 1.2), ("BORE", "HEAD", 170, 1.2, 0.9),
    ("CNJR", "PAIN", 90, 1.3, 1.1), ("HTRD", "SKUL", 330, 1.8, 1.0),
    # bosses
    ("MECH", "CYBR", 60, 0.6, 1.1), ("BEET", "SPID", 100, 1.3, 0.8), ("HART", "BBRN", 340, 1.6, 1.1),
    ("CORE", "BBRN", 10, 1.8, 0.8),
    # projectiles
    ("TRKT", "MISL", 30, 1.0, 1.0), ("SBLT", "BAL7", 180, 1.2, 1.2), ("EFBL", "BAL1", 20, 1.4, 1.2),
    ("WZBL", "BAL2", 270, 1.2, 1.1), ("WZSK", "FATB", 250, 1.2, 1.1), ("DRNB", "BAL7", 300, 1.2, 1.0),
    ("GRCK", "BAL1", 50, 0.3, 0.8), ("BRBL", "BAL2", 150, 1.4, 1.1), ("MCRK", "MISL", 60, 0.8, 1.0),
    ("MCSK", "FATB", 70, 1.0, 1.1), ("MCFL", "BAL1", 10, 1.8, 1.3), ("BTAC", "BAL2", 100, 1.6, 1.0),
    ("HFBL", "BAL1", 330, 1.6, 1.1),
]


# Doom's own sprites that the roster replaces (the monsters took over their slots): removed once the
# tinted copies exist, so the wad does not carry two sets of art for one monster.
REPLACED = ["SARG", "TROO", "SKEL", "BOS2", "BOSS", "FATT", "VILE", "HEAD", "PAIN", "SKUL", "CYBR", "SPID", "BBRN"]


def grab_chunk(data):
    pos = 8
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        if typ == b'grAb':
            return data[pos:pos + 12 + n]
        pos += 12 + n
    return None


def with_grab(png, chunk):
    if chunk is None:
        return png
    ihdr_end = 8 + 12 + struct.unpack('>I', png[8:12])[0]
    return png[:ihdr_end] + chunk + png[ihdr_end:]


def shift(img, hue, sat, val):
    img = img.convert('RGBA')
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if not a:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            h = (h + hue / 360.0) % 1.0
            s = min(1.0, s * sat)
            v = min(1.0, v * val)
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            px[x, y] = (int(r * 255), int(g * 255), int(b * 255), a)
    return img


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    spr = os.path.join(sys.argv[1], 'sprites')
    made = 0
    for new, old, hue, sat, val in SPRITES:
        src, dst = os.path.join(spr, old), os.path.join(spr, new)
        if os.path.exists(dst):
            continue
        if not os.path.isdir(src):
            sys.exit('missing original sprite folder ' + src)
        os.makedirs(dst)
        for f in sorted(os.listdir(src)):
            if not f.upper().endswith('.PNG') or not f.startswith(old):
                continue
            raw = open(os.path.join(src, f), 'rb').read()
            out = os.path.join(dst, new + f[4:])
            shift(Image.open(os.path.join(src, f)), hue, sat, val).save(out)
            png = open(out, 'rb').read()
            open(out, 'wb').write(with_grab(png, grab_chunk(raw)))
            made += 1
    import shutil
    for old in REPLACED:
        if os.path.isdir(os.path.join(spr, old)):
            shutil.rmtree(os.path.join(spr, old))
    print('made %d placeholder sprite lumps' % made)


if __name__ == '__main__':
    main()
