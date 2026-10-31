#!/usr/bin/env python3
"""Begotten test map for line portals: MAP01 (UDMF), built with ZDBSP.

usage: make_portal_test_map.py OUT.wad [ZDBSP]

Three rooms far apart (they do not touch):  A (start), B (higher floor, bright), C (lower floor, dim).
  S  (A east wall)   -> D (B west wall), straight through.   Two switches in A re-point S: at C's south wall (F, a quarter
                         turn) and back at D.
  P  (A north wall)  <-> E (C east wall), a quarter turn, both ways.
  D  (B west wall)   -> S.
"""
import os, shutil, struct, subprocess, sys, tempfile

def rect(x1, y1, x2, y2):          # clockwise: the room is on the right of every line
    return [(x1, y1), (x1, y2), (x2, y2), (x2, y1)]

rooms = {
    'A': dict(poly=rect(0, 0, 512, 512), floor=0, ceil=128, light=140, ftex='FLOOR2', ctex='CEIL1', wall='WALL02'),
    'B': dict(poly=rect(2000, 0, 2512, 512), floor=32, ceil=160, light=210, ftex='FLOOR3', ctex='CEIL2', wall='WALL05'),
    'C': dict(poly=rect(4000, 0, 4512, 512), floor=-16, ceil=112, light=int(os.environ.get('CLIGHT', 100)), ftex='FLOOR1', ctex='CEIL1', wall='WALL09'),
}
# extra vertices on the walls (where a portal or a switch starts and ends) -> line extras
# key: (room, a, b): the line from a to b (a piece of the wall)
special = {
    # S: A's east wall, going down, 128 long
    ('A', (512, 320), (512, 192)): dict(id=1, special=156, args=(2, 0, 2, 0, 0)),
    # P: A's north wall, going east
    ('A', (192, 512), (320, 512)): dict(id=4, special=156, args=(3, 0, 2, 0, 0)),
    # the switches on A's west wall (going up): re-point S to F / back to D
    ('A', (0, 200), (0, 240)): dict(id=0, special=107, args=(1, 6, 0, 0, 0), playeruse=True, repeatspecial=True, wall='WALL10'),
    ('A', (0, 272), (0, 312)): dict(id=0, special=107, args=(1, 2, 0, 0, 0), playeruse=True, repeatspecial=True, wall='WALL11'),
    # D: B's west wall (going up)
    ('B', (2000, 192), (2000, 320)): dict(id=2, special=156, args=(1, 0, 2, 0, 0)),
    # E: C's east wall (going down), P's partner
    ('C', (4512, 320), (4512, 192)): dict(id=3, special=156, args=(4, 0, 2, 0, 0)),
    # F: C's south wall (going west): S's other destination
    ('C', (4320, 0), (4192, 0)): dict(id=6, special=156, args=(1, 0, 2, 0, 0)),
}

def build():
    V = []
    def vid(p):
        if p not in V: V.append(p)
        return V.index(p)
    lines, sides, sectors = [], [], []
    for si, (name, r) in enumerate(rooms.items()):
        sectors.append(r)
        poly = r['poly']
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            # cut the wall at every special piece that lies on it
            pts = [a]
            cuts = []
            for (rn, p, q), sp in special.items():
                if rn != name: continue
                def on(pt):
                    return (a[0] == b[0] == pt[0] and min(a[1], b[1]) <= pt[1] <= max(a[1], b[1])) or \
                           (a[1] == b[1] == pt[1] and min(a[0], b[0]) <= pt[0] <= max(a[0], b[0]))
                if on(p) and on(q):
                    cuts.append((p, q))
            seq = [a]
            dirx = (b[0] > a[0]) - (b[0] < a[0]); diry = (b[1] > a[1]) - (b[1] < a[1])
            def along(pt): return (pt[0] - a[0]) * dirx + (pt[1] - a[1]) * diry
            cuts.sort(key=lambda c: along(c[0]) if along(c[0]) < along(c[1]) else along(c[1]))
            for (p, q) in cuts:
                lo, hi = (p, q) if along(p) < along(q) else (q, p)
                seq.append(lo); seq.append(hi)
            seq.append(b)
            for u, v in zip(seq[:-1], seq[1:]):
                if u == v: continue
                sp = special.get((name, u, v), {})
                sides.append(dict(sector=si, tex=sp.get('wall', r['wall'])))
                lines.append(dict(v1=vid(u), v2=vid(v), side=len(sides) - 1, **{k: x for k, x in sp.items() if k != 'wall'}))
    if os.environ.get('POLY'):
        # a thin panel in A that can turn (Polyobj_RotateLeft): both its faces are portals.  Counter-clockwise, so the faces look outwards
        cx, cy = int(os.environ.get('POLYX', 300)), int(os.environ.get('POLYY', 256))
        pr = [(cx - 4, cy - 64), (cx + 4, cy - 64), (cx + 4, cy + 64), (cx - 4, cy + 64)]
        pil = [dict(special=1, args=(1, 0, 0, 0, 0)),                        # starts the polyobject
               dict(id=7, special=156, args=(2, 0, 2, 0, 0)),                # the east face: to D
               dict(),
               dict(id=8, special=156, args=(3, 0, 2, 0, 0))]                # the west face: to E
        for i in range(4):
            sides.append(dict(sector=0, tex='WALL07'))
            lines.append(dict(v1=vid(pr[i]), v2=vid(pr[(i + 1) % 4]), side=len(sides) - 1, **pil[i]))
    out = ['namespace = "zdoom";']
    for (x, y) in V:
        out.append('vertex { x = %d.0; y = %d.0; }' % (x, y))
    for l in lines:
        t = ['linedef { v1 = %d; v2 = %d; sidefront = %d; blocking = true;' % (l['v1'], l['v2'], l['side'])]
        if l.get('id'): t.append(' id = %d;' % l['id'])
        if l.get('special'):
            t.append(' special = %d;' % l['special'])
            for i, a in enumerate(l.get('args', ())):
                t.append(' arg%d = %d;' % (i, a))
        if l.get('playeruse'): t.append(' playeruse = true;')
        if l.get('repeatspecial'): t.append(' repeatspecial = true;')
        t.append(' }')
        out.append(''.join(t))
    for s in sides:
        out.append('sidedef { sector = %d; texturemiddle = "%s"; }' % (s['sector'], s['tex']))
    for r in sectors:
        out.append('sector { heightfloor = %d; heightceiling = %d; texturefloor = "%s"; textureceiling = "%s"; lightlevel = %d; }' %
                   (r['floor'], r['ceil'], r['ftex'], r['ctex'], r['light']))
    n = 0
    def thing(typ, x, y, ang=0):
        out.append('thing { x = %d.0; y = %d.0; angle = %d; type = %d; skill1 = true; skill2 = true; skill3 = true; skill4 = true; skill5 = true; '
                   'single = true; coop = true; dm = true; class1 = true; class2 = true; class3 = true; }' % (x, y, ang, typ))
    px, py, pa = int(os.environ.get('PX', 256)), int(os.environ.get('PY', 256)), int(os.environ.get('PANG', 0))
    thing(1, px, py, pa)
    thing(2, 256, 230, 0)
    # B: things to see (keys, weapons, a pillar of ammo)
    for k, (x, y) in enumerate([(2200, 256), (2300, 200), (2300, 312), (2400, 256)]):
        thing([5, 6, 13, 2001][k], x, y)
    # C: other things
    for k, (x, y) in enumerate([(4200, 100), (4300, 400), (4400, 256)]):
        thing([2002, 2003, 2004][k], x, y)
    if os.environ.get('POLY'):
        out.append('thing { x = %d.0; y = %d.0; angle = 1; type = 9300; skill1 = true; skill2 = true; skill3 = true; skill4 = true; skill5 = true; single = true; coop = true; dm = true; }' % (cx, cy))
        out.append('thing { x = %d.0; y = %d.0; angle = 1; type = 9301; skill1 = true; skill2 = true; skill3 = true; skill4 = true; skill5 = true; single = true; coop = true; dm = true; }' % (cx, cy))
    # A: a marker near the portals
    thing(2007, 400, 100)
    return '\n'.join(out) + '\n'

def wad(path, lumps):
    d = b'PWAD' + struct.pack('<ii', len(lumps), 12 + sum(len(b) for _, b in lumps))
    tbl = b''; pos = 12
    for n, b in lumps:
        d += b
        tbl += struct.pack('<ii8s', pos if b else 0, len(b), n.encode().ljust(8, b'\0'))
        pos += len(b)
    open(path, 'wb').write(d + tbl)

def read_wad(path):
    d = open(path, 'rb').read()
    n, off = struct.unpack('<ii', d[4:12]); o = []
    for i in range(n):
        p, sz, nm = struct.unpack('<ii8s', d[off + 16 * i: off + 16 * i + 16])
        o.append((nm.rstrip(b'\0').decode(), d[p:p + sz] if sz else b''))
    return o

def main():
    out = sys.argv[1]
    zd = sys.argv[2] if len(sys.argv) > 2 else shutil.which('zdbsp')
    tmp = tempfile.mkdtemp(); src = os.path.join(tmp, 'in.wad')
    wad(src, [('MAP01', b''), ('TEXTMAP', build().encode()), ('ENDMAP', b'')])
    if not zd:
        shutil.copy(src, out); print('no zdbsp'); return
    subprocess.check_call([zd, '-w', '-o', os.path.join(tmp, 'out.wad'), src])
    wad(out, read_wad(os.path.join(tmp, 'out.wad')))
    print('wrote', out)

if __name__ == '__main__':
    main()
