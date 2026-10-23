#!/usr/bin/env python3
"""Begotten: make an EDITOR wad whose art is at 1 picture pixel per map unit.

usage: make_editor_wad.py CONTENT_TREE OUT.wad

The game draws its art at several pixels per map unit (data/BGSCALE.txt, 2.5 by default) so pictures can be
sharp while the map stays Doom-sized.  Map editors know nothing about that and draw one pixel as one unit, so
everything looks 2.5 times too big and textures cannot be aligned.  Load OUT.wad in the editor INSTEAD of
begotten.wad (as the IWAD resource): the same lump names, shrunk to the real size in map units.  Never play with it.

  sprites/  scaled down by the `sprites` value, hot spots too
  textures/ and patches/  scaled down by the `textures` value
  flats/    128x128 -> 64x64, 256x256 -> 128x128 ... (a flat is drawn over 64 map units across)
  sky textures (SKY*) left alone: the game does not scale the sky
  graphics/ left alone (the editor does not draw them)
  sounds/, music/, maps/ left out (a smaller file)
Needs Pillow.  Run it again whenever the art changes.
"""
import os, re, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
import pack_wad as P


def scales(tree):
    s = {'sprites': 1.0, 'textures': 1.0, 'weapons': 1.0}
    f = os.path.join(tree, 'data', 'BGSCALE.txt')
    if os.path.exists(f):
        for line in open(f):
            m = re.match(r'\s*(sprites|textures|weapons)\s+([0-9.]+)', line)
            if m:
                s[m.group(1)] = float(m.group(2))
    return s


def shrink(path, factor, flat=False):
    data = open(path, 'rb').read()
    ox, oy = P.png_offsets(data) or (0, 0)
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    if flat:
        factor = 2.0 if w >= 128 else 1.0      # a 128-pixel flat covers 64 map units
    nw, nh = max(1, round(w / factor)), max(1, round(h / factor))
    if (nw, nh) == (w, h):
        return
    a = im.getchannel('A')
    small = im.resize((nw, nh), Image.LANCZOS)
    small.putalpha(a.resize((nw, nh), Image.BILINEAR))
    out = P.png_bytes(small, int(round(ox / factor)), int(round(oy / factor)))
    open(path, 'wb').write(out)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    tree, out = sys.argv[1], sys.argv[2]
    sc = scales(tree)
    tmp = tempfile.mkdtemp(prefix='bgeditor_')
    try:
        dst = os.path.join(tmp, 'tree')
        os.makedirs(dst)
        for entry in os.listdir(tree):
            if entry in ('sounds', 'music', 'maps', 'SOUND_MAP.md'):
                continue
            s = os.path.join(tree, entry)
            (shutil.copytree if os.path.isdir(s) else shutil.copy2)(s, os.path.join(dst, entry))
        if os.path.exists(os.path.join(dst, 'textures.txt')):
            print('warning: textures.txt (composite textures) is not rescaled; those textures keep their game size')
        n = 0
        for sub, key, flat in (('sprites', 'sprites', False), ('textures', 'textures', False), ('patches', 'textures', False), ('flats', None, True)):
            base = os.path.join(dst, sub)
            for root, _, files in os.walk(base):
                for f in files:
                    if f.lower().endswith('.png') and not (sub in ('textures', 'patches') and f.upper().startswith('SKY')):   # the sky is not scaled in the game
                        shrink(os.path.join(root, f), sc[key] if key else 1.0, flat)
                        n += 1
        bg = os.path.join(dst, 'data', 'BGSCALE.txt')
        if os.path.exists(bg):
            os.remove(bg)                  # the editor wad is 1 pixel per unit
        ident, lumps = P.pack(dst, out)
        print('wrote %s: %s, %d lumps, %d pictures shrunk (sprites /%.2f, textures /%.2f)' % (out, ident, len(lumps), n, sc['sprites'], sc['textures']))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
