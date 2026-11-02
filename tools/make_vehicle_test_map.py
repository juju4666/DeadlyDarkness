#!/usr/bin/env python3
"""Begotten test map for the vehicles: one big room with two pillars, a step, a row of imps and a motorcycle and a mecha.

usage: make_vehicle_test_map.py OUT.wad [ZDBSP]      env: EXTRA='type x y [angle];...' more things, PX/PY/PANG the start
Thing 21201 is the motorcycle, 21202 the mecha (BGVOXELV lump, BEGOTTEN_ENGINE.md 0.14.0).
"""
import os, shutil, struct, subprocess, sys, tempfile

def rect(x1, y1, x2, y2): return [(x1, y1), (x1, y2), (x2, y2), (x2, y1)]            # clockwise: room on the right
def hole(x1, y1, x2, y2): return list(reversed(rect(x1, y1, x2, y2)))                # counter-clockwise: a pillar

def build():
    V, lines, sides = [], [], []
    def vid(p):
        if p not in V: V.append(p)
        return V.index(p)
    def loop(poly, sector, tex='WALL02'):
        for i in range(len(poly)):
            sides.append(dict(sector=sector, tex=tex))
            lines.append((vid(poly[i]), vid(poly[(i + 1) % len(poly)]), len(sides) - 1))
    loop(rect(0, 0, 2048, 2048), 0)
    loop(hole(900, 800, 964, 864), 0, 'WALL05')
    loop(hole(1300, 1300, 1364, 1364), 0, 'WALL05')
    out = ['namespace = "zdoom";']
    out += ['vertex { x = %d.0; y = %d.0; }' % p for p in V]
    out += ['linedef { v1 = %d; v2 = %d; sidefront = %d; blocking = true; }' % l for l in lines]
    out += ['sidedef { sector = %d; texturemiddle = "%s"; }' % (s['sector'], s['tex']) for s in sides]
    out.append('sector { heightfloor = 0; heightceiling = 320; texturefloor = "FLOOR2"; textureceiling = "CEIL1"; lightlevel = 170; }')
    def thing(t, x, y, a=0):
        out.append('thing { x = %d.0; y = %d.0; angle = %d; type = %d; skill1 = true; skill2 = true; skill3 = true; skill4 = true; '
                   'skill5 = true; single = true; coop = true; dm = true; class1 = true; class2 = true; class3 = true; }' % (x, y, a, t))
    thing(1, int(os.environ.get('PX', 256)), int(os.environ.get('PY', 1024)), int(os.environ.get('PANG', 0)))
    thing(2, int(os.environ.get('PX', 256)), int(os.environ.get('PY', 1024)) + 60, int(os.environ.get('PANG', 0)))
    thing(21201, 400, 900, 0)
    thing(21202, 400, 1200, 0)
    for k in range(6): thing(3001, 1500, 300 + 250 * k, 180)
    for e in filter(None, os.environ.get('EXTRA', '').split(';')):
        v = [int(q) for q in e.split()]
        thing(v[0], v[1], v[2], v[3] if len(v) > 3 else 0)
    return '\n'.join(out) + '\n'

def wad(path, lumps):
    d = b'PWAD' + struct.pack('<ii', len(lumps), 12 + sum(len(b) for _, b in lumps)); tbl = b''; pos = 12
    for n, b in lumps:
        d += b; tbl += struct.pack('<ii8s', pos if b else 0, len(b), n.encode().ljust(8, b'\0')); pos += len(b)
    open(path, 'wb').write(d + tbl)

def read_wad(path):
    d = open(path, 'rb').read(); n, off = struct.unpack('<ii', d[4:12]); o = []
    for i in range(n):
        p, sz, nm = struct.unpack('<ii8s', d[off + 16 * i: off + 16 * i + 16]); o.append((nm.rstrip(b'\0').decode(), d[p:p + sz] if sz else b''))
    return o

def main():
    out = sys.argv[1]; zd = sys.argv[2] if len(sys.argv) > 2 else shutil.which('zdbsp')
    tmp = tempfile.mkdtemp(); src = os.path.join(tmp, 'in.wad')
    wad(src, [('MAP01', b''), ('TEXTMAP', build().encode()), ('ENDMAP', b'')])
    if not zd: shutil.copy(src, out); print('no zdbsp'); return
    subprocess.check_call([zd, '-w', '-o', os.path.join(tmp, 'out.wad'), src]); wad(out, read_wad(os.path.join(tmp, 'out.wad'))); print('wrote', out)

if __name__ == '__main__': main()
