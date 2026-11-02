#!/usr/bin/env python3
"""
make_vehicle_hall.py - adds the Vehicle Hall to MAP01 of a Begotten wad: a door in the west wall of the first room (the start
room, x 0..384, y 0..256) opening on a short corridor and a big hall with the motorcycles, the mechas, the tank and the cars.

    python make_vehicle_hall.py IN.wad OUT.wad [ZDBSP]

Reads MAP01 (UDMF TEXTMAP and BEHAVIOR) from IN.wad, appends the new sectors, lines and things (nothing that exists is moved,
so every index in the map stays the same; only the west wall of the start room, one line, is cut in three), rebuilds the nodes
with ZDBSP and writes the wad with the new MAP01. Run it on a wad that has not had the hall added (it refuses otherwise).

The layout (the door is on the west wall, y 64..192):
    door sector  x -16..0                       Door_Raise, tag TAG, opens from both sides
    corridor     x -640..-16, y 64..192, ceiling 128
    hall         x -2400..-640, y -560..720, ceiling 256: four pillars, a stepped platform (8, 16, 24 units high) in the middle,
                 the vehicles along the walls, a row of barrels to run over, spotlights in the ceiling.
"""
import os, re, shutil, struct, subprocess, sys, tempfile

TAG = 9101
HX1, HX2, HY1, HY2 = -2400, -640, -560, 720       # the hall
CY1, CY2 = 64, 192                                 # the corridor and door: 128 wide, so the tank and the mechas can drive out


def read_wad(path):
    d = open(path, 'rb').read()
    n, off = struct.unpack('<ii', d[4:12])
    lumps = []
    for i in range(n):
        p, sz, nm = struct.unpack('<ii8s', d[off + 16 * i:off + 16 * i + 16])
        lumps.append((nm.rstrip(b'\0').decode(), d[p:p + sz] if sz else b''))
    return d[:4], lumps


def write_wad(path, typ, lumps):
    parts, tbl, pos = [], [], 12
    for n, b in lumps:
        parts.append(b)
        tbl.append(struct.pack('<ii8s', pos if b else 0, len(b), n.encode().ljust(8, b'\0')))
        pos += len(b)
    with open(path, 'wb') as f:
        f.write(typ + struct.pack('<ii', len(lumps), pos))
        f.write(b''.join(parts))
        f.write(b''.join(tbl))


def blocks(text, kind):
    return list(re.finditer(r'^%s\s*\n?\{(.*?)\n?\}\s*$' % kind, text, re.M | re.S))


def kv(body):
    return {k: v.strip().strip('"') for k, v in re.findall(r'(\w+)\s*=\s*([^;]+);', body)}


class Add:
    def __init__(self, text):
        self.t = text
        self.nv = len(blocks(text, 'vertex'))
        self.nl = len(blocks(text, 'linedef'))
        self.ns = len(blocks(text, 'sidedef'))
        self.nsec = len(blocks(text, 'sector'))
        self.V, self.L, self.S, self.SEC, self.T = [], [], [], [], []
        self.vid = {}
        for i, b in enumerate(blocks(text, 'vertex')):
            k = kv(b.group(1))
            self.vid.setdefault((float(k['x']), float(k['y'])), i)

    def vert(self, x, y):
        key = (float(x), float(y))
        if key not in self.vid:
            self.vid[key] = self.nv + len(self.V)
            self.V.append('vertex\n{\nx = %.1f;\ny = %.1f;\n}\n' % key)
        return self.vid[key]

    def sector(self, floor, ceil, ftex, ctex, light, tag=0):
        s = 'sector\n{\nheightfloor = %d;\nheightceiling = %d;\ntexturefloor = "%s";\ntextureceiling = "%s";\nlightlevel = %d;\n' % (floor, ceil, ftex, ctex, light)
        if tag:
            s += 'id = %d;\n' % tag
        self.SEC.append(s + '}\n')
        return self.nsec + len(self.SEC) - 1

    def side(self, sector, top='-', bottom='-', middle='-'):
        self.S.append('sidedef\n{\nsector = %d;\ntexturetop = "%s";\ntexturebottom = "%s";\ntexturemiddle = "%s";\n}\n' % (sector, top, bottom, middle))
        return self.ns + len(self.S) - 1

    def line(self, a, b, front, back=None, special=None, args=(), use=False):
        s = 'linedef\n{\nv1 = %d;\nv2 = %d;\nsidefront = %d;\n' % (self.vert(*a), self.vert(*b), front)
        if back is None:
            s += 'blocking = true;\n'
        else:
            s += 'sideback = %d;\ntwosided = true;\n' % back
        if special:
            s += 'special = %d;\n' % special
            for i, v in enumerate(args):
                s += 'arg%d = %d;\n' % (i, v)
        if use:
            s += 'playeruse = true;\nrepeatspecial = true;\n'
        self.L.append(s + '}\n')

    def thing(self, typ, x, y, angle=0):
        self.T.append('thing\n{\nx = %.1f;\ny = %.1f;\nangle = %d;\ntype = %d;\nskill1 = true;\nskill2 = true;\nskill3 = true;\n'
                      'skill4 = true;\nskill5 = true;\nsingle = true;\ncoop = true;\ndm = true;\n}\n' % (x, y, angle, typ))

    def wall(self, a, b, sector, tex):
        self.line(a, b, self.side(sector, middle=tex))

    def ring(self, x1, y1, x2, y2, outer, inner, tex='GREY1'):
        """a hole in `outer`, counter-clockwise (outer on the right of every line), inner inside it"""
        pts = [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]
        for i in range(4):
            self.line(pts[i], pts[(i + 1) % 4], self.side(outer, top=tex, bottom=tex), self.side(inner, top=tex, bottom=tex))


def build(text):
    if 'id = %d;' % TAG in text:
        sys.exit('the vehicle hall is already in this map')
    used = {int(v) for v in re.findall(r'\bid\s*=\s*(\d+);', text)}
    assert TAG not in used
    A = Add(text)
    sectA = 0                                              # the start room
    # the west wall of the start room: line (0,0)-(0,256)
    lds = blocks(text, 'linedef')
    vs = {i: kv(b.group(1)) for i, b in enumerate(blocks(text, 'vertex'))}
    target = None
    for i, b in enumerate(lds):
        k = kv(b.group(1))
        a, c = vs[int(k['v1'])], vs[int(k['v2'])]
        if float(a['x']) == 0 and float(c['x']) == 0 and {float(a['y']), float(c['y'])} == {0.0, 256.0} and 'sideback' not in k:
            target = (i, k, b)
            break
    if not target:
        sys.exit('cannot find the west wall of the start room (x=0, y 0..256)')
    li, lk, lb = target
    assert float(vs[int(lk['v1'])]['y']) == 0.0, 'the wall runs the other way than expected'
    sds = blocks(text, 'sidedef')
    sectA = int(kv(sds[int(lk['sidefront'])].group(1))['sector'])
    wall_tex = kv(sds[int(lk['sidefront'])].group(1)).get('texturemiddle', 'WALL02')

    # sectors
    sD = A.sector(0, 0, 'FLOOR1', 'CEIL1', 90, TAG)
    sC = A.sector(0, 128, 'FLOOR1', 'CEIL1', 80)
    sH = A.sector(0, 256, 'FLOOR3', 'CEIL2', 150)
    sP1 = A.sector(8, 256, 'FLOOR3', 'CEIL2', 150)
    sP2 = A.sector(16, 256, 'FLOOR3', 'CEIL2', 154)
    sP3 = A.sector(24, 256, 'FLOOR1', 'CEIL2', 160)

    # the start room's west wall, cut in three; the first piece is the old line
    v96, v160 = A.vert(0, CY1), A.vert(0, CY2)
    newtext_line = lb.group(0).replace('v2 = %s;' % lk['v2'], 'v2 = %d;' % v96, 1)
    # the door: front is the start room, back the door sector; opens with Use
    dsA = A.side(sectA, top='DOOR1', bottom='DOOR1')
    dsD = A.side(sD, top='DOOR1', bottom='DOOR1')
    A.line((0, CY1), (0, CY2), dsA, dsD, 12, (TAG, 16, 150, 0, 0), True)
    A.wall((0, CY2), (0, 256), sectA, wall_tex)
    # the door sector's side walls, and its far face (front = corridor, so it opens from that side too)
    A.wall((0, CY1), (-16, CY1), sD, 'DOORTRAK')
    A.wall((-16, CY2), (0, CY2), sD, 'DOORTRAK')
    A.line((-16, CY2), (-16, CY1), A.side(sC, top='DOOR1', bottom='DOOR1'), A.side(sD, top='DOOR1', bottom='DOOR1'), 12, (TAG, 16, 150, 0, 0), True)
    # the corridor
    A.wall((-16, CY1), (HX2, CY1), sC, 'GREY1')
    A.wall((HX2, CY2), (-16, CY2), sC, 'GREY1')
    # the hall, clockwise: the room is on the right of every line
    A.wall((HX1, HY2), (HX2, HY2), sH, 'WALL07')
    A.wall((HX2, HY2), (HX2, CY2), sH, 'WALL07')
    A.line((HX2, CY2), (HX2, CY1), A.side(sH), A.side(sC))             # the way in
    A.wall((HX2, CY1), (HX2, HY1), sH, 'WALL07')
    A.wall((HX2, HY1), (HX1, HY1), sH, 'WALL07')
    A.wall((HX1, HY1), (HX1, HY2), sH, 'WALL07')
    # a stepped platform in the middle (steps of 8 units), pillars at the corners of the floor
    px, py = -1500, 100
    A.ring(px - 240, py - 240, px + 240, py + 240, sH, sP1)
    A.ring(px - 200, py - 200, px + 200, py + 200, sP1, sP2)
    A.ring(px - 160, py - 160, px + 160, py + 160, sP2, sP3)
    for cx, cy in ((-1000, -100), (-1000, 300), (-2050, -100), (-2050, 300)):
        # a pillar is a hole in the hall: counter-clockwise, one-sided
        h = 48
        pts = [(cx - h, cy - h), (cx + h, cy - h), (cx + h, cy + h), (cx - h, cy + h)]
        for i in range(4):
            A.wall(pts[i], pts[(i + 1) % 4], sH, 'WALL05')

    # lights: a grid of ceiling spots (baked, with real shadows), strips in the corridor
    for x in (-2100, -1500, -900):
        for y in (-300, 100, 500):
            A.thing(20103, x, y, 0)
    for x in (-800, -1300, -1800, -2300):      # lamps along the north and south walls light the vehicles
        A.thing(20102, x, 690, 0)
        A.thing(20102, x, -530, 0)
    A.thing(20104, -330, 128, 0)
    A.thing(20104, -90, 128, 0)

    # the vehicles: two motorcycles and two mechas facing the hall along the north wall (angle 270 = south), the tank, the cars
    for x in (-800, -900):
        A.thing(21201, x, 610, 270)
    for x in (-1250, -1550):
        A.thing(21202, x, 600, 270)
    A.thing(21203, -1950, 560, 270)
    for k, x in enumerate((-800, -1100, -1400, -1700, -2000, -2250)):
        A.thing(21101, x, -430, 0 if k % 2 == 0 else 180)
    # targets to run over and shoot: barrels along the west wall
    for y in (-400, -250, -100, 50, 200, 350, 500):
        A.thing(2035, -2330, y, 0)
    # a few cars to ram, in the open
    A.thing(21101, -1000, 130, 90)
    A.thing(21101, -2000, 140, 45)

    out = text.replace(lb.group(0), newtext_line, 1)
    return out.rstrip('\n') + '\n\n' + '\n'.join(A.V + A.SEC + A.S + A.L + A.T)


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
    names = [n for n, _ in built]
    assert 'ZNODES' in names and 'BEHAVIOR' in names, names
    write_wad(dst, typ, lumps[:i] + built + lumps[j + 1:])
    print('wrote', dst, [(n, len(b)) for n, b in built])


if __name__ == '__main__':
    main()
