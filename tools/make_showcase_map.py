#!/usr/bin/env python3
"""Begotten placeholder MAP01: "Gallery of Lights", a UDMF map that shows off the lighting.

usage: make_showcase_map.py OUT.wad [ZDBSP]

Writes a UDMF map (no nodes) and runs ZDBSP (https://github.com/rheit/zdbsp) to build the nodes, blockmap and reject.
Without ZDBSP on the path only the node-less map is written.  The rooms, left to right:

  A  Hall of Torches   start; stationary torches and a candle, a door with a movable torch behind it
  B  Hall of Shadows   one white lamp and five pillars: true shadows
  N  Strip corridor    low ceiling, strip lights and a ceiling spot, leading to
  V  Vault             seven power-ups, each glowing in its own colour
  C  Chamber of Colour red, green and blue lights round one pillar: coloured shadows and mixing
  F  Dark arena        monsters throw fireballs (dynamic lights); a lift with a movable red light
  G  Ledge             exit switch and the biggest gun
  (C has tag 5, the thick haze; the Sun court has tag 6, clear air: both are BGLIGHT `fog ... sector TAG` zones)
  Y  Sun court         north of the first hall: open sky and a sun (BGLIGHT `sun`); a walled room with a window
                       and a doorway lets the sun in as patches on the floor, shaded by sill and lintel
"""
import os, subprocess, struct, sys, shutil, tempfile

# ------------------------------------------------------------------------------------------------ the layout
def rect(x1, y1, x2, y2):
    return [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]

S = {}   # name -> dict(poly, floor, ceil, light, ftex, ctex, wall, id)
def sector(name, poly, floor=0, ceil=160, light=32, ftex='FLOOR1', ctex='CEIL1', wall='GREY1', id=0, holes=()):
    S[name] = dict(poly=poly, floor=floor, ceil=ceil, light=light, ftex=ftex, ctex=ctex, wall=wall, id=id, holes=list(holes))

sector('A', rect(0, 0, 384, 256), ceil=160, light=90, ftex='FLOOR2', wall='WALL02')
sector('DOOR', rect(384, 96, 400, 160), ceil=0, light=90, ftex='FLOOR1', ctex='CEIL1', wall='DOORTRAK', id=1)
sector('C1', rect(400, 96, 720, 160), ceil=128, light=70, ftex='FLOOR1', wall='GREY1')
sector('B', rect(720, -128, 1232, 384) , ceil=192, light=36, ftex='FLOOR3', wall='WALL03')
# B's north and east walls have gaps; the gap sectors are part of the corridors, so B's outline carries those vertices
S['B']['poly'] = [(720, -128), (1232, -128), (1232, 96), (1232, 160), (1232, 384), (1104, 384), (1040, 384), (720, 384)]
sector('N', rect(1040, 384, 1104, 672), ceil=96, light=70, ftex='FLOOR1', ctex='CEIL2', wall='GREY1')
sector('V', rect(944, 672, 1200, 864), ceil=160, light=24, ftex='FLOOR2', wall='BROWN1')
S['V']['poly'] = [(944, 672), (1040, 672), (1104, 672), (1200, 672), (1200, 864), (944, 864)]
sector('E1', rect(1232, 96, 1296, 160), ceil=128, light=70, wall='GREY1')
sector('C', [(1296, -64), (1680, -64), (1680, 96), (1680, 160), (1680, 320), (1296, 320), (1296, 160), (1296, 96)],
       ceil=160, light=30, ftex='FLOOR3', wall='WALL04', id=5)
sector('E2', rect(1680, 96, 1744, 160), ceil=128, light=70, wall='GREY1')
sector('F', [(1744, -128), (2032, -128), (2032, 96), (2032, 160), (2032, 384), (1744, 384), (1744, 160), (1744, 96)],
       ceil=192, light=30, ftex='FLOOR1', ctex='CEIL2', wall='RED1')
sector('L', rect(2032, 96, 2096, 160), floor=96, ceil=192, light=50, ftex='FLOOR2', wall='LIFT1', id=2)
sector('G', [(2096, -32), (2224, -32), (2224, 96), (2224, 160), (2224, 288), (2096, 288), (2096, 160), (2096, 96)],
       floor=96, ceil=192, light=80, ftex='FLOOR2', wall='WALL05')

# Y: the sun court and a room beside it. The room's wall has a doorway and a window (sill 48, lintel 112).
sector('YN', rect(160, 256, 224, 320), ceil=128, light=70, ftex='FLOOR1', ctex='CEIL2', wall='GREY1')
sector('YC', rect(-128, 320, 512, 576), ceil=224, light=60, ftex='FLOOR3', ctex='F_SKY1', wall='WALL02', id=6)
sector('YD', rect(150, 576, 182, 592), ceil=96, light=40, ftex='FLOOR1', ctex='CEIL1', wall='WALL03')
sector('YW', rect(224, 576, 288, 592), floor=48, ceil=112, light=40, ftex='FLOOR1', ctex='CEIL1', wall='WALL03')
sector('YR', rect(128, 592, 384, 800), ceil=128, light=16, ftex='FLOOR2', ctex='CEIL1', wall='WALL03')

# solid pillars: (polygon, the sector around it, texture)
PILLARS = [
    (rect(944, 96, 1008, 160), 'B', 'WALL03'),
    (rect(830, 230, 878, 278), 'B', 'WALL03'),
    (rect(830, -22, 878, 26), 'B', 'WALL03'),
    (rect(1100, 40, 1164, 104), 'B', 'WALL03'),
    (rect(1100, 152, 1164, 216), 'B', 'WALL03'),
    (rect(1456, 96, 1520, 160), 'C', 'WALL04'),
    (rect(1850, 40, 1914, 104), 'F', 'RED1'),
    (rect(1850, 152, 1914, 216), 'F', 'RED1'),
    (rect(20, 420, 68, 468), 'YC', 'WALL02'),
    (rect(380, 380, 428, 428), 'YC', 'WALL02'),
    (rect(320, 700, 352, 732), 'YR', 'WALL03'),
]

# lines between two sectors (or from one, for one-sided lines in ONE) that need a special or other textures
DOORSP = dict(special=12, args=(1, 16, 150, 0, 0), use=True, repeat=True, tex='DOOR1')
LINKS = {
    frozenset(('A', 'DOOR')): DOORSP,
    frozenset(('DOOR', 'C1')): DOORSP,
    frozenset(('F', 'L')): dict(special=62, args=(2, 32, 105, 0, 0), use=True, repeat=True, tex='LIFT1'),
    frozenset(('L', 'G')): dict(tex='LIFT1'),
}
ONE = {   # exact one-sided segments
    frozenset(((2224, 96), (2224, 160))): dict(special=243, args=(0, 0, 0, 0, 0), use=True, tex='SWX1'),
}

# ------------------------------------------------------------------------------------------------ things
# type, x, y, angle, args (light things: arg0 radius/8, arg1 strength %, arg2-4 colour; 0 = the definition's own)
T = []
def thing(typ, x, y, ang=0, args=(0, 0, 0, 0, 0), **kw):
    if os.environ.get('ALLMOVABLE') and 20101 <= typ <= 20108:
        typ += 100       # developer check: every light live
    T.append((typ, x, y, ang, args, kw))

for i, (x, y) in enumerate([(48, 100), (48, 156), (96, 100), (96, 156)]):
    thing(i + 1, x, y, 0)
for x, y in [(160, 40), (160, 216), (300, 60), (300, 200)]:
    thing(11, x, y, 0, dm=True)

# A: torches along the west wall (far enough from the door that they are not "near a moving sector"), a candle
thing(20101, 24, 24); thing(20101, 24, 232); thing(20101, 24, 128)
thing(20105, 120, 128)
# weapons and ammo, for trying out the dynamic lights
for k, dt in enumerate((2001, 82, 2002, 2003, 2004)):
    thing(dt, 150 + k * 40, 224)
for k, dt in enumerate((2048, 2049, 2046, 2047)):
    thing(dt, 150 + k * 40, 32)
thing(2012, 90, 128)

# the movable torch in the corridor behind the door: its light shines into the hall when the door opens
thing(20201, 560, 128)

# B: one white lamp, shadows of five pillars
thing(20102, 880, 128)

# N: strip lights along the ceiling, a spot over the entrance of the vault
thing(20104, 1072, 450, 90); thing(20104, 1072, 540, 90); thing(20103, 1072, 640, 0)

# V: power-ups
for typ, x, y in [(2013, 976, 840), (83, 1024, 840), (2022, 1072, 840), (2023, 1120, 840), (2025, 1168, 840),
                  (2026, 1024, 760), (2045, 1120, 760)]:
    thing(typ, x, y)

# C: red, green and blue round the pillar
thing(20107, 1330, -30); thing(20106, 1646, -30); thing(20108, 1488, 290)

# F: the arena
thing(20105, 1768, -100); thing(20105, 1768, 356)
for typ, x, y in [(3001, 1990, -90), (3001, 1990, 340), (3001, 1960, 128), (3004, 1880, -80), (3004, 1880, 330),
                  (3005, 1980, 20)]:
    thing(typ, x, y, 180)
for typ, x, y in [(2008, 1790, 60), (2007, 1790, 200), (2001, 1790, 128)]:
    thing(typ, x, y)

# Y: a few things standing in the sun (and one in the shade), a health bonus on the lit patch in the room
for typ, x, y in [(3001, 300, 420), (3004, 90, 520), (2014, 262, 650), (2014, 160, 640)]:
    thing(typ, x, y, 270)
thing(2018, 250, 700)

# L and G: a red movable light on the lift, two movable torches on the ledge, the BFG
thing(20207, 2064, 128)
thing(20201, 2160, 20); thing(20201, 2160, 236)
thing(2006, 2160, 128)

# ------------------------------------------------------------------------------------------------ the builder
if os.environ.get('BOSSTEST'):
    # developer check (never in the shipped wad): a plain trooper tagged as a boss (TID 30000), next to the player starts
    thing(3004, 130, 128, 180, id=30000); thing(3004, 130, 100, 180)

def area(poly):
    return 0.5 * sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
                     for i in range(len(poly)))

def build():
    verts = []
    def vid(p):
        if p not in verts:
            verts.append(p)
        return verts.index(p)

    # edges: (a, b, sector, kind) with the sector on the right of a -> b (clockwise rings), pillars counter-clockwise
    edges = []
    for name, s in S.items():
        poly = list(s['poly'])
        if area(poly) > 0:
            poly.reverse()
        for i in range(len(poly)):
            edges.append([poly[i], poly[(i + 1) % len(poly)], name, 'wall'])
        for hole in s.get('holes', ()):          # a hole in this sector: another sector (or a pillar) sits inside it
            hole = list(hole)
            if area(hole) < 0:
                hole.reverse()
            for i in range(len(hole)):
                edges.append([hole[i], hole[(i + 1) % len(hole)], name, 'wall'])
    for poly, name, tex in PILLARS:
        poly = list(poly)
        if area(poly) < 0:
            poly.reverse()
        for i in range(len(poly)):
            edges.append([poly[i], poly[(i + 1) % len(poly)], name, tex])
    # split every edge at vertices that lie on it
    allpts = set(p for e in edges for p in e[:2])
    out = []
    for a, b, sec, kind in edges:
        pts = [a, b]
        for p in allpts:
            if p in (a, b):
                continue
            cross = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
            dot = (p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1])
            if cross == 0 and 0 < dot < (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2:
                pts.append(p)
        pts.sort(key=lambda p: (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2)
        for i in range(len(pts) - 1):
            out.append((pts[i], pts[i + 1], sec, kind))
    edges = out
    # pair up the reversed twins
    by = {}
    for i, (a, b, sec, kind) in enumerate(edges):
        by[(a, b)] = i
    names = list(S)
    sec_index = {n: i for i, n in enumerate(names)}
    sides = []     # sidedef dicts
    lines = []
    used = set()
    for i, (a, b, sec, kind) in enumerate(edges):
        if i in used:
            continue
        used.add(i)
        j = by.get((b, a))
        sa = S[sec]
        if j is None or kind != 'wall' or edges[j][3] != 'wall':
            if j is not None:
                raise SystemExit('pillar edge %s -> %s touches another sector' % (a, b))
            opts = ONE.get(frozenset((a, b)), {})
            tex = opts.get('tex', kind if kind != 'wall' else sa['wall'])
            if sa['wall'] == 'DOORTRAK' and kind == 'wall':
                tex = 'DOORTRAK'
            sides.append(dict(sector=sec_index[sec], middle=tex, top='-', bottom='-'))
            lines.append(dict(v1=vid(a), v2=vid(b), front=len(sides) - 1, back=-1, **opts))
            continue
        used.add(j)
        sb = S[edges[j][2]]
        other = edges[j][2]
        opts = LINKS.get(frozenset((sec, other)), {})
        tex = opts.get('tex')
        mid = opts.get('mid', '-')
        sides.append(dict(sector=sec_index[sec], middle=mid, top=tex or sa['wall'], bottom=tex or sa['wall']))
        sides.append(dict(sector=sec_index[other], middle=mid, top=tex or sb['wall'], bottom=tex or sb['wall']))
        lines.append(dict(v1=vid(a), v2=vid(b), front=len(sides) - 2, back=len(sides) - 1, **opts))

    o = ['namespace = "dsda";']
    for i, (x, y) in enumerate(verts):
        o.append('vertex // %d\n{\nx = %d.0;\ny = %d.0;\n}' % (i, x, y))
    for ln in lines:
        s = ['linedef\n{', 'v1 = %d;' % ln['v1'], 'v2 = %d;' % ln['v2'], 'sidefront = %d;' % ln['front']]
        if ln['back'] >= 0:
            s += ['sideback = %d;' % ln['back'], 'twosided = true;']
        else:
            s += ['blocking = true;']
        if ln.get('id'):
            s.append('id = %d;' % ln['id'])
        if ln.get('impassable') and ln['back'] >= 0:
            s.append('blocking = true;')
        if ln.get('use_nospecial'):
            s.append('playeruse = true;')
        if ln.get('special'):
            s.append('special = %d;' % ln['special'])
            for k, v in enumerate(ln.get('args', ())):
                if v:
                    s.append('arg%d = %d;' % (k, v))
            if ln.get('use'):
                s.append('playeruse = true;')
            if ln.get('cross'):
                s.append('playercross = true;')
            if ln.get('repeat'):
                s.append('repeatspecial = true;')
        s.append('}')
        o.append('\n'.join(s))
    for sd in sides:
        o.append('sidedef\n{\nsector = %d;\ntexturetop = "%s";\ntexturebottom = "%s";\ntexturemiddle = "%s";\n}' %
                 (sd['sector'], sd['top'], sd['bottom'], sd['middle']))
    for n in names:
        s = S[n]
        o.append('sector // %s\n{\nheightfloor = %d;\nheightceiling = %d;\ntexturefloor = "%s";\ntextureceiling = "%s";\n'
                 'lightlevel = %d;\n%s}' % (n, s['floor'], s['ceil'], s['ftex'], s['ctex'], s['light'],
                                           'id = %d;\n' % s['id'] if s['id'] else ''))
    for typ, x, y, ang, args, kw in T:
        s = ['thing\n{', 'x = %d.0;' % x, 'y = %d.0;' % y, 'angle = %d;' % ang, 'type = %d;' % typ,
             'skill1 = true;', 'skill2 = true;', 'skill3 = true;', 'skill4 = true;', 'skill5 = true;',
             ]
        if kw.get('id'):
            s.append('id = %d;' % kw['id'])
        if kw.get('ambush'):
            s.append('ambush = true;')
        if kw.get('dormant'):
            s.append('dormant = true;')
        if kw.get('z'):
            s.append('height = %d.0;' % kw['z'])
        if kw.get('special'):
            s.append('special = %d;' % kw['special'])
        if kw.get('dm'):
            s.append('dm = true;')
        else:
            s += ['single = true;', 'coop = true;', 'dm = true;']
        for k, v in enumerate(args):
            if v:
                s.append('arg%d = %d;' % (k, v))
        s.append('}')
        o.append('\n'.join(s))
    return '\n'.join(o) + '\n'

def wad(path, lumps):
    d = b'PWAD' + struct.pack('<ii', len(lumps), 12 + sum(len(b) for _, b in lumps))
    tbl = b''
    pos = 12
    for n, b in lumps:
        d += b
        tbl += struct.pack('<ii8s', pos if b else 0, len(b), n.encode().ljust(8, b'\0'))
        pos += len(b)
    open(path, 'wb').write(d + tbl)

def read_wad(path):
    d = open(path, 'rb').read()
    n, off = struct.unpack('<ii', d[4:12])
    out = []
    for i in range(n):
        p, sz, nm = struct.unpack('<ii8s', d[off + 16 * i: off + 16 * i + 16])
        out.append((nm.rstrip(b'\0').decode(), d[p:p + sz] if sz else b''))
    return out

def main():
    out = sys.argv[1]
    zd = sys.argv[2] if len(sys.argv) > 2 else shutil.which('zdbsp')
    text = build()
    tmp = tempfile.mkdtemp()
    src = os.path.join(tmp, 'in.wad')
    wad(src, [('MAP01', b''), ('TEXTMAP', text.encode()), ('ENDMAP', b'')])
    if not zd:
        shutil.copy(src, out)
        print('no zdbsp: wrote the map without nodes')
        return
    subprocess.check_call([zd, '-w', '-o', os.path.join(tmp, 'out.wad'), src])
    lumps = read_wad(os.path.join(tmp, 'out.wad'))
    wad(out, lumps)
    print('wrote', out, [n for n, _ in lumps])

if __name__ == '__main__':
    main()
