#!/usr/bin/env python3
"""Begotten: turn single-rotation sprite frames (NAMEx0.png) into 8 separate numbered rotation lumps.

usage: make_rotation_placeholders.py SPRITE_DIR NAME FRAMES      e.g.  ... /tmp/tree/sprites/SSWV SSWV EFG

For every frame letter a copy of NAMEx0.png is written as NAMEx1.png .. NAMEx8.png (none mirrored) with the
rotation number drawn on it, keeping the hot-spot offsets, and NAMEx0.png is removed. Placeholder art only:
replace the eight lumps with real renders. Frames that already have rotations are skipped.
"""
import os, sys
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pack_wad import png_bytes, png_offsets


def main():
    d, name, frames = sys.argv[1:4]
    for f in frames:
        src = os.path.join(d, '%s%s0.png' % (name, f))
        if not os.path.exists(src):
            print('skip', name + f, '(no single-rotation lump)')
            continue
        raw = open(src, 'rb').read()
        off = png_offsets(raw) or (0, 0)
        base = Image.open(src).convert('RGBA')
        for r in range(1, 9):
            im = base.copy()
            dr = ImageDraw.Draw(im)
            w, h = im.size
            dr.rectangle([w // 2 - 4, h // 3 - 1, w // 2 + 4, h // 3 + 9], fill=(0, 0, 0, 255))
            dr.text((w // 2 - 2, h // 3 - 1), str(r), fill=(255, 255, 255, 255))
            open(os.path.join(d, '%s%s%d.png' % (name, f, r)), 'wb').write(png_bytes(im, *off))
        os.remove(src)
        print('rotated', name + f)


main()
