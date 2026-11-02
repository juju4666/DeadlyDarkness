#!/usr/bin/env python3
"""
make_rail_demo.py - adds a grind rail course to the Vehicle Hall of MAP01 (the swordsman's rails, special 300, BEGOTTEN_ENGINE.md 0.15.3).

    python make_rail_demo.py IN.wad OUT.wad [ZDBSP]

Appends vertices and two-sided lines (both sides in the hall sector, so nothing in the map changes shape) to MAP01 and rebuilds the
nodes. Refuses when the rails are already there. The rail runs along the south half of the hall, bends, and comes back along the
platform side; a jump off it at full speed clears the vehicles.
"""
import os, re, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_vehicle_hall import Add, blocks, kv, read_wad, write_wad

RAILS = [  # (x1, y1, x2, y2, height) - joined end to end
    (-2250, -300, -1250, -300, 32),
    (-1250, -300, -850, -200, 32),
    (-850, -200, -850, 450, 32),
    (-850, 450, -1250, 520, 32),
]


def build(text):
    if re.search(r'special\s*=\s*300;', text):
        sys.exit('the rails are already in this map')
    A = Add(text)
    secs = blocks(text, 'sector')
    sH = None
    for i, b in enumerate(secs):
        k = kv(b.group(1))
        if k.get('texturefloor') == 'FLOOR3' and k.get('heightceiling') == '256' and k.get('heightfloor') == '0':
            sH = i
            break
    if sH is None:
        sys.exit('cannot find the hall sector')
    for x1, y1, x2, y2, h in RAILS:
        A.line((x1, y1), (x2, y2), A.side(sH), A.side(sH), 300, (h, 0))
    return text.rstrip('\n') + '\n\n' + '\n'.join(A.V + A.S + A.L)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    zd = sys.argv[3] if len(sys.argv) > 3 else shutil.which('zdbsp')
    typ, lumps = read_wad(src)
    i = [k for k, (n, b) in enumerate(lumps) if n == 'MAP01' and not b][0]
    j = i + 1
    while lumps[j][0] != 'ENDMAP':
        j += 1
    group = {n: b for n, b in lumps[i + 1:j + 1]}
    text = build(group['TEXTMAP'].decode())
    tmp = tempfile.mkdtemp()
    w = os.path.join(tmp, 'in.wad')
    write_wad(w, b'PWAD', [('MAP01', b''), ('TEXTMAP', text.encode()), ('BEHAVIOR', group['BEHAVIOR']), ('ENDMAP', b'')])
    subprocess.check_call([zd, '-o', os.path.join(tmp, 'out.wad'), w])
    _, built = read_wad(os.path.join(tmp, 'out.wad'))
    assert 'ZNODES' in [n for n, _ in built]
    write_wad(dst, typ, lumps[:i] + built + lumps[j + 1:])
    print('wrote', dst)


if __name__ == '__main__':
    main()
