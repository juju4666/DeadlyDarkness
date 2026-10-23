#!/usr/bin/env python3
"""bgclear.py MAP.wad [OUT.wad] [--texture CLEAR]

Fills every upper or lower wall part that UDB would show as an orange "missing texture" with the transparent texture
CLEAR (default).  A part counts when a double-sided line has a height difference there and no texture on it:
  upper: this side's ceiling is higher than the other side's;  lower: this side's floor is lower than the other side's.
Works on binary (Hexen format) maps, any number of maps in the wad.  Nodes stay valid (only SIDEDEFS change).
Without OUT.wad the file is changed in place and MAP.wad.bak keeps the original.  Single-sided lines without a
middle texture are only reported (they are real walls).
"""
import os, shutil, struct, sys

def read(path):
    d = open(path, 'rb').read(); n, o = struct.unpack('<ii', d[4:12]); r = []
    for i in range(n):
        p, s, nm = struct.unpack('<ii8s', d[o+16*i:o+16*i+16]); r.append([nm.rstrip(b'\0').decode(), bytearray(d[p:p+s])])
    return d[:4], r

def write(path, ident, lumps):
    data = b''; ents = []; pos = 12
    for n, d in lumps:
        ents.append((pos, len(d), n)); data += d; pos += len(d)
    dirr = b''.join(struct.pack('<ii8s', p, s, n.encode().ljust(8, b'\0')) for p, s, n in ents)
    open(path, 'wb').write(ident + struct.pack('<ii', len(lumps), 12 + len(data)) + data + dirr)

def main(argv):
    tex = 'CLEAR'
    if '--texture' in argv:
        i = argv.index('--texture'); tex = argv[i + 1].upper(); del argv[i:i + 2]
    if not argv: print(__doc__); return 1
    src = argv[0]; dst = argv[1] if len(argv) > 1 else src
    ident, lumps = read(src); name = tex.encode().ljust(8, b'\0')
    idx = {}
    for i, (n, d) in enumerate(lumps): idx.setdefault(n, []).append(i)
    if 'TEXTMAP' in idx: print('This map is UDMF; bgclear.py only handles binary (Hexen format) maps.'); return 1
    total = 0; singles = 0
    for m, start in enumerate(i for i, (n, d) in enumerate(lumps) if n == 'LINEDEFS'):
        # the lumps of this map follow its LINEDEFS lump closely: find SIDEDEFS and SECTORS after it
        grp = {n: i for i, (n, d) in enumerate(lumps) if i >= start - 1 and n in ('LINEDEFS', 'SIDEDEFS', 'SECTORS') and i < start + 12}
        ld, sd, sc = lumps[grp['LINEDEFS']][1], lumps[grp['SIDEDEFS']][1], lumps[grp['SECTORS']][1]
        sec = lambda s: struct.unpack('<hh', sc[26*s:26*s+4])
        side_sector = lambda s: struct.unpack('<H', sd[30*s+28:30*s+30])[0]
        changed = 0
        for i in range(len(ld) // 16):
            f, b = struct.unpack('<hh', ld[16*i+12:16*i+16])
            if f >= 0 and b < 0:
                if sd[30*f+20:30*f+22] == b'-\0': singles += 1
                continue
            if f < 0 or b < 0: continue
            for a, o in ((f, b), (b, f)):
                fa, ca = sec(side_sector(a)); fo, co = sec(side_sector(o))
                if ca > co and sd[30*a+4:30*a+6] == b'-\0': sd[30*a+4:30*a+12] = name; changed += 1
                if fa < fo and sd[30*a+12:30*a+14] == b'-\0': sd[30*a+12:30*a+20] = name; changed += 1
        total += changed
    if dst == src and total: shutil.copyfile(src, src + '.bak')
    write(dst, ident, [(n, bytes(d)) for n, d in lumps])
    print('%d wall parts set to %s%s' % (total, tex, '' if dst != src else ' (backup: %s.bak)' % src))
    if singles: print('note: %d single-sided lines have no middle texture (real walls, left alone)' % singles)
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
