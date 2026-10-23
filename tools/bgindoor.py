#!/usr/bin/env python3
"""bgindoor.py MAP.wad [OUT.wad] [--map MAP01]

Puts a BGLIGHT lump into your map wad that switches off the fog, sun and rain of the Gallery of Lights.  begotten.wad
gives MAP01 a blue haze, a low sun and rain (it is the showcase map), and every map you save as MAP01 inherits them: far
walls fade into the blue haze instead of into darkness.  The lump says, for this map name:

    fog map MAP01 density 0
    fog map MAP01 sector 5 density 0      (the showcase's violet chamber)
    fog map MAP01 sector 6 density 0
    sun map MAP01 strength 0
    weather map MAP01 none

Edit the lump later in any wad editor if you want some fog back (see LIGHTING.md).  Without OUT.wad the file is changed in
place and MAP.wad.bak keeps the original.  Run it again after the map editor has saved if the lump is ever gone.
"""
import shutil, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bgclear import read, write

def main(argv):
    mp = 'MAP01'
    if '--map' in argv:
        i = argv.index('--map'); mp = argv[i + 1].upper(); del argv[i:i + 2]
    if not argv: print(__doc__); return 1
    src = argv[0]; dst = argv[1] if len(argv) > 1 else src
    text = ("# This map is not the Gallery of Lights: no fog, sun or rain (see LIGHTING.md to bring some back).\n"
            "fog map %s density 0\nfog map %s sector 5 density 0\nfog map %s sector 6 density 0\n"
            "sun map %s strength 0\nweather map %s none\n" % ((mp,) * 5)).encode()
    ident, lumps = read(src)
    lumps = [(n, d) for n, d in lumps if n != 'BGLIGHT']          # replace an older one
    if dst == src: shutil.copyfile(src, src + '.bak')
    write(dst, ident, [('BGLIGHT', text)] + [(n, bytes(d)) for n, d in lumps])
    print('BGLIGHT lump added for %s: no fog, sun or rain' % mp)
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
