#!/usr/bin/env python3
"""Begotten: build a COLORMAP lump with fullbright palette indices.

A fullbright index maps to itself in all 32 light levels, so a sprite pixel painted with it
stays bright no matter how dark the sector or how far away it is (eyes, muzzle glow...).

usage:
  make_colormap.py WAD --fullbright 200-207,250 [-o COLORMAP.lmp]
  make_colormap.py WAD --check                      (lists which indices are fullbright now)

WAD is any wad/iwad containing PLAYPAL (and optionally COLORMAP). If it has a COLORMAP, that
is the base and only the listed indices are overridden; otherwise a standard one is generated
from PLAYPAL. Put the resulting lump in begotten.wad as COLORMAP.
"""
import argparse, struct, sys


def lump(path, name):
    d = open(path, 'rb').read()
    n, o = struct.unpack('<ii', d[4:12])
    for i in range(n):
        p, s, nm = struct.unpack('<ii8s', d[o + 16 * i:o + 16 * i + 16])
        if nm.rstrip(b'\0').decode().upper() == name:
            return d[p:p + s]
    return None


def parse_indices(text):
    out = set()
    for part in text.split(','):
        part = part.strip()
        if not part:
            continue
        a, _, b = part.partition('-')
        lo, hi = int(a), int(b or a)
        if not (0 <= lo <= hi <= 255):
            sys.exit('indices must be 0-255: ' + part)
        out.update(range(lo, hi + 1))
    return sorted(out)


def generate(pal):
    rgb = [tuple(pal[i * 3:i * 3 + 3]) for i in range(256)]

    def nearest(r, g, b):
        best, bd = 0, 1 << 30
        for i, (pr, pg, pb) in enumerate(rgb):
            d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
            if d < bd:
                best, bd = i, d
        return best

    maps = []
    for lvl in range(32):
        f = (32 - lvl) / 32.0
        maps.append(bytes(nearest(int(r * f), int(g * f), int(b * f)) for r, g, b in rgb))
    inv = []
    for r, g, b in rgb:
        v = 255 - int(0.299 * r + 0.587 * g + 0.114 * b)
        inv.append(nearest(v, v, v))
    maps.append(bytes(inv))
    maps.append(bytes(0 for _ in range(256)))
    return b''.join(maps)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('wad')
    ap.add_argument('--fullbright', default='')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('-o', '--output', default='COLORMAP.lmp')
    a = ap.parse_args()

    cm = lump(a.wad, 'COLORMAP')
    if cm is None:
        pal = lump(a.wad, 'PLAYPAL')
        if pal is None:
            sys.exit('no PLAYPAL or COLORMAP in ' + a.wad)
        cm = generate(pal[:768])
        print('no COLORMAP in the wad: generated a standard one from PLAYPAL')
    if len(cm) < 34 * 256:
        sys.exit('COLORMAP is too short (%d bytes)' % len(cm))
    cm = bytearray(cm[:34 * 256])

    if a.check:
        fb = [c for c in range(256) if all(cm[l * 256 + c] == c for l in range(32))]
        print('fullbright indices now (map to themselves in all 32 light levels): %s'
              % (', '.join(map(str, fb)) or 'none'))
        return

    idx = parse_indices(a.fullbright)
    if not idx:
        sys.exit('give --fullbright, e.g. --fullbright 200-207')
    for c in idx:
        for l in range(32):
            cm[l * 256 + c] = c
    open(a.output, 'wb').write(bytes(cm))
    print('wrote %s: %d fullbright indices (%s)' % (a.output, len(idx), ', '.join(map(str, idx))))


main()
