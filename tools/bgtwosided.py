#!/usr/bin/env python3
"""bgtwosided.py MAP.wad [OUT.wad]

Sets the "Doublesided" flag on every line that has a back sidedef but not the flag, and clears it on lines that have
the flag but no back sidedef.  UDB (Hexen / Doom format) treats a line without the flag as single-sided and paints a
big orange MISSING texture over it in 3D mode, even though the game sees through it.  UDMF maps do not care, which is
why such lines looked fine there.  Impassable and every other flag are left alone.  Nodes stay valid.
Without OUT.wad the file is changed in place and MAP.wad.bak keeps the original.
"""
import shutil, struct, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from bgclear import read, write

def main(argv):
    if not argv: print(__doc__); return 1
    src = argv[0]; dst = argv[1] if len(argv) > 1 else src
    ident, lumps = read(src)
    if any(n == 'TEXTMAP' for n, d in lumps): print('This map is UDMF; bgtwosided.py only handles binary (Hexen format) maps.'); return 1
    setn = clr = 0
    for n, d in lumps:
        if n != 'LINEDEFS': continue
        for i in range(len(d) // 16):
            fl = struct.unpack('<H', d[16*i+4:16*i+6])[0]; b = struct.unpack('<h', d[16*i+14:16*i+16])[0]
            if b >= 0 and not fl & 4: fl |= 4; setn += 1; print('  line %d: Doublesided set' % i)
            elif b < 0 and fl & 4: fl &= ~4; clr += 1; print('  line %d: Doublesided cleared (single-sided)' % i)
            else: continue
            d[16*i+4:16*i+6] = struct.pack('<H', fl)
    if dst == src and (setn or clr): shutil.copyfile(src, src + '.bak')
    write(dst, ident, [(n, bytes(d)) for n, d in lumps])
    print('%d lines got the Doublesided flag, %d lost it' % (setn, clr))
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
