#!/usr/bin/env python3
"""Begotten script lab: a wing of MAP01 with one door and one room for every ACS function of the manual.

usage: make_script_lab.py OUT.wad [ZDBSP] [ACC_DIR]

Adds to the showcase map of make_showcase_map.py (Gallery of Lights):

  * a door in the south wall of the first hall (room A) that leads to a corridor and a long hall (the spine);
  * 16 wings leave the spine to the south, one per group of the manual (built-in functions, doors, floors, ...);
  * a wing is a corridor with a room on each side, every room behind its own door.  Walk in: the script of that
    function prints its name and the call, then does it.  The switch on the far wall of every room runs it again;
    the demos that leave the map (exits, new map, end of game) only run from the red switch;
  * the ACS (lab.acs) is compiled with ACC -h into the BEHAVIOR lump of the map.

The table of demos is script_lab_demos.py.  Environment: LAB_AUTORUN=1 adds a script that starts every demo at once
and BEGOTTEN_DUMP=file makes the engine log what changed (tools/check_script_lab.py reads it); neither goes into the
shipped wad.
"""
import os, re, subprocess, sys, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_showcase_map as M
from script_lab_demos import DEMOS

STR = 176            # distance between two rooms along a corridor
RD = 192             # depth of a room
RW = 160             # width of a room
PITCH = 576          # distance between wings
SPINE_Y = (-256, -192)
FWALL = ['WALL02', 'WALL03', 'WALL04', 'WALL05', 'WALL06', 'BROWN1', 'GREEN1', 'BLUE1', 'RED1', 'YELLOW1', 'GREY1',
         'WALL07', 'WALL08', 'WALL09', 'WALL10', 'WALL11']
FLOORS = ['FLOOR1', 'FLOOR2', 'FLOOR3']

S, T, LINKS, ONE, PILLARS = M.S, M.T, M.LINKS, M.ONE, M.PILLARS
thing, sector = M.thing, M.sector


def rect(x1, y1, x2, y2):
    return [(min(x1, x2), min(y1, y2)), (max(x1, x2), min(y1, y2)), (max(x1, x2), max(y1, y2)), (min(x1, x2), max(y1, y2))]


class Room:
    def __init__(self, n, side, x_ent, yc, wall, ftex):
        self.n, self.sgn, self.x_ent, self.yc, self.wall, self.ftex = n, (1 if side == 'E' else -1), x_ent, yc, wall, ftex
        self.R = 'R%d' % n
        self.angle_fw = 0 if side == 'E' else 180
        self.angle_back = 180 - self.angle_fw

    def pt(self, u, v):
        return (self.x_ent + self.sgn * u, self.yc + v)

    def rect(self, u1, v1, u2, v2):
        a, b = self.pt(u1, v1), self.pt(u2, v2)
        return rect(a[0], a[1], b[0], b[1])


def tid(kind, n):
    return {'S': 1000, 'M': 2000, 'M2': 3000, 'Dst': 1500}[kind] + n


# ---------------------------------------------------------------------------------------------------------- the kits
# Each kit builds its part of the room.  Returns a dict with the extra placeholders for the script text.
SLAB = (100, 0, 32)          # centre u, centre v, half size

def slab_sector(r, name, u1, v1, u2, v2, tag=0, **kw):
    props = dict(floor=0, ceil=160, light=120, ftex='FLOOR2', ctex='CEIL2', wall='GREY1', id=tag)
    props.update(kw)
    sector(name, r.rect(u1, v1, u2, v2), **props)
    return props


def kit_none(r, d, ex):
    return []

def add_mon(r, kind, u, v, tkey, typ=3004, **kw):
    thing(typ, *r.pt(u, v), ang=r.angle_back, id=tid(tkey, r.n), **kw) if False else \
        thing(typ, r.pt(u, v)[0], r.pt(u, v)[1], r.angle_back, id=tid(tkey, r.n), **kw)

def add_spot(r, u, v, key='S', typ=9001, ang=None):
    p = r.pt(u, v)
    thing(typ, p[0], p[1], r.angle_fw if ang is None else ang, id=tid(key, r.n))

def hole_tiles(r, hole, tiles, links=None):
    """tiles is a list of (name, rect, props); hole is the rectangle that carries them."""
    S[r.R]['holes'].append(r.rect(*hole))
    for name, rc, props in tiles:
        sector(name, r.rect(*rc), **props)
    for k, v in (links or {}).items():
        LINKS[frozenset(k)] = v

def kit_basic(r, d, ex, *, mon=False, spot=False, dormant=False, dest=False, slab=None, tiles=None):
    pass


def build_kit(r, d, ex):
    """Geometry and things of the room's kit.  ex gets the placeholder values."""
    k, n, R = d['kit'], r.n, r.R
    ko = d['kopt']
    sx, sy = SLAB[0], SLAB[1]

    def slab(name='T%d' % n, f=0, c=160, ftex='FLOOR2', ctex='CEIL2', wall='GREY1', half=32, light=120, tag=n, link=None):
        hole_tiles(r, (sx - half, sy - half, sx + half, sy + half),
                   [(name, (sx - half, sy - half, sx + half, sy + half),
                     dict(floor=f, ceil=c, light=light, ftex=ftex, ctex=ctex, wall=wall, id=tag))],
                   {(R, name): link} if link else None)
        return name

    def tri(f3, c3, light3=(120, 120, 120), ftex3=('FLOOR2', 'FLOOR3', 'LIQUID1'), tag=n, link=None):
        """A slab with a neighbour on each side along v: middle T, high H (+v), low L (-v)."""
        hole = (sx - 32, sy - 72, sx + 32, sy + 72)
        tiles = []
        # order: T first, so that lines from T to its neighbours have T in front
        tiles.append(('T%d' % n, (sx - 32, sy - 24, sx + 32, sy + 24),
                      dict(floor=f3[0], ceil=c3[0], light=light3[0], ftex=ftex3[0], ctex='CEIL2', wall='GREY1', id=tag)))
        tiles.append(('H%d' % n, (sx - 32, sy + 24, sx + 32, sy + 72),
                      dict(floor=f3[1], ceil=c3[1], light=light3[1], ftex=ftex3[1], ctex='CEIL1', wall='GREY1')))
        tiles.append(('L%d' % n, (sx - 32, sy - 72, sx + 32, sy - 24),
                      dict(floor=f3[2], ceil=c3[2], light=light3[2], ftex=ftex3[2], ctex='CEIL1', wall='GREY1')))
        hole_tiles(r, hole, tiles, {(R, 'T%d' % n): link} if link else None)

    def barrel(u=sx, v=sy, key='M'):
        thing(2035, r.pt(u, v)[0], r.pt(u, v)[1], 0, id=tid(key, n))

    if k == 'none':
        pass
    elif k == 'spot':
        add_spot(r, 150, -50)
    elif k == 'mon':
        add_mon(r, 'M', 110, 0, 'M')
    elif k == 'mon_spot':
        add_mon(r, 'M', 110, 0, 'M'); add_spot(r, 150, -50)
    elif k == 'mon_dormant':
        add_mon(r, 'M', 110, 0, 'M', dormant=True)
    elif k == 'mon_door':
        slab(f=0, c=0, link=dict(tex='DOOR1'))
        add_mon(r, 'M', 150, 0, 'M')
    elif k == 'duo':
        add_mon(r, 'M', 120, -40, 'M', typ=3001); add_mon(r, 'M2', 120, 40, 'M2', typ=3001)
    elif k == 'sleepers':
        add_mon(r, 'M', 160, -55, 'M'); add_mon(r, 'M2', 160, 55, 'M2')
        M.PILLARS.append((r.rect(60, -24, 108, 24), R, 'WALL07'))
    elif k == 'tele':
        add_spot(r, 150, -50, ang=r.angle_fw)
        p = r.pt(150, 50)
        thing(14, p[0], p[1], r.angle_fw, id=tid('Dst', n))
    elif k == 'group':
        add_spot(r, 100, -50)
        for du, dv in ((0, 0), (20, 0), (0, 20)):
            thing(2035, r.pt(100 + du, -50 + dv)[0], r.pt(100 + du, -50 + dv)[1], 0, id=tid('M', n))
        p = r.pt(150, 50)
        thing(14, p[0], p[1], r.angle_fw, id=tid('Dst', n))
    elif k == 'sectormon':
        slab(f=0, c=160)
        for du, dv in ((-10, -10), (12, 12)):
            thing(2035, r.pt(sx + du, sy + dv)[0], r.pt(sx + du, sy + dv)[1], 0, id=tid('M', n))
        add_spot(r, sx, sy)
        p = r.pt(150, 50)
        thing(14, p[0], p[1], r.angle_fw, id=tid('Dst', n))
    elif k == 'telline':
        hole_tiles(r, (110, 40, 126, 72), [('X%d' % n, (110, 40, 126, 72), dict(floor=0, ceil=160, light=120, ftex='FLOOR3', ctex='CEIL2', wall='GREY1'))],
                   {(R, 'X%d' % n): dict(id=n)})
    elif k == 'door_c':
        slab(f=0, c=0, wall='DOORTRAK', link=dict(tex='DOOR1'))
    elif k == 'door_o':
        slab(f=0, c=96, wall='DOORTRAK', link=dict(tex='DOOR1'))
    elif k == 'door_k':
        slab(f=0, c=0, wall='DOORTRAK', link=dict(tex='DOOR1'))
        p = r.pt(40, -60)
        thing(5, p[0], p[1], 0)
    elif k == 'floor':
        tri((32, 72, -24), (160, 160, 160))
    elif k == 'floor_v':
        slab(f=32, c=96)
        barrel(sx, sy)
    elif k == 'ceil':
        tri((0, 0, 0), (112, 176, 72))
    elif k == 'ceil_v':
        slab(f=0, c=112)
        barrel(sx, sy)
    elif k == 'fc':
        tri((32, 72, -24), (104, 176, 72))
    elif k == 'lift':
        tri((40, 72, -24), (160, 160, 160))
    elif k in ('stairs', 'stairs_hi'):
        f = 96 if k == 'stairs_hi' else 0
        steps = []
        for i in range(6):
            u1 = 20 + 26 * i
            steps.append(('P%d_%d' % (n, i), (u1, -40, u1 + 26, 40),
                          dict(floor=f, ceil=190, light=120, ftex='FLOOR3', ctex='CEIL2', wall='GREY1',
                               id=(n if i == 0 else 600 + 10 * n + i))))
        hole_tiles(r, (20, -40, 20 + 26 * 6, 40), steps)
        for i in range(6):
            ex['t%d' % i] = n if i == 0 else 600 + 10 * n + i
    elif k == 'pillar':
        slab(f=0, c=160)
    elif k == 'pillar_c':
        slab(f=56, c=56)
    elif k == 'pillar_v':
        slab(f=0, c=96)
        barrel(sx, sy)
    elif k == 'stage':
        S[R]['id'] = n
        S[R]['light'] = 80
        hole_tiles(r, (60, 36, 124, 72), [('H%d' % n, (60, 36, 124, 72), dict(floor=0, ceil=160, light=220, ftex='FLOOR3', ctex='CEIL2', wall='GREY1'))])
        hole_tiles(r, (60, -72, 124, -36), [('L%d' % n, (60, -72, 124, -36), dict(floor=0, ceil=160, light=10, ftex='FLOOR3', ctex='CEIL2', wall='GREY1'))])
    elif k == 'flat':
        slab(f=0, c=128, ftex='FLOOR3', ctex='CEIL1', half=40)
    elif k == 'hazard':
        slab(f=-8, c=160, ftex='LAVA1', ctex='CEIL1')
    elif k == 'donut':
        ring = 700 + n
        hole_tiles(r, (sx - 48, sy - 48, sx + 48, sy + 48),
                   [('Q%d' % n, (sx - 16, sy - 16, sx + 16, sy + 16), dict(floor=48, ceil=160, light=120, ftex='FLOOR3', ctex='CEIL2', wall='GREY1', id=n)),
                    ('G%d' % n, (sx - 48, sy - 48, sx + 48, sy + 48), dict(floor=-24, ceil=160, light=120, ftex='FLOOR2', ctex='CEIL2', wall='GREY1', id=ring,
                                                                           holes=[r.rect(sx - 16, sy - 16, sx + 16, sy + 16)]))])
        ex['ring'] = ring
    elif k in ('cage', 'cage_w'):
        A = 'A%d' % n
        hole_tiles(r, (sx - 24, sy - 24, sx + 24, sy + 24),
                   [(A, (sx - 24, sy - 24, sx + 24, sy + 24), dict(floor=0, ceil=160, light=120, ftex='FLOOR3', ctex='CEIL2', wall='GREY1', id=n))],
                   {(R, A): dict(id=n, impassable=True, **({'mid': 'WALL05', 'special': 33, 'use': True, 'repeat': True} if k == 'cage_w' else {}))})
        p = r.pt(sx, sy)
        thing(2011, p[0], p[1], 0)
    elif k == 'wallseg':
        pass
    elif k in ('poly', 'poly_bar', 'poly_spot'):
        ang = n
        if k == 'poly_bar':
            rc = r.rect(60, -6, 140, 6)
            cx, cy = r.pt(60, 0)
        else:
            rc = r.rect(sx - 24, sy - 24, sx + 24, sy + 24)
            cx, cy = r.pt(sx, sy)
        PILLARS.append((rc, R, 'WALL07'))
        # the first edge of the pillar (counter-clockwise from its lowest-left corner) starts the polyobject
        a, b = rc[0], rc[1]
        ONE[frozenset((a, b))] = dict(special=1, args=(n, 0, 0, 0, 0), tex='WALL07')
        thing(9300, cx, cy, ang)
        thing(9301, cx, cy, ang)
        ex['px'], ex['py'] = cx, cy
        p2 = r.pt(sx, sy + 40)
        ex['px2'], ex['py2'] = p2
        if k == 'poly_spot':
            add_spot(r, 150, -50)
        ex['up'], ex['down'] = 64, 192
    else:
        raise SystemExit('unknown kit ' + k)


# ---------------------------------------------------------------------------------------------------------- the lab
def build_lab():
    groups = []
    for d in DEMOS:
        if d['grp'] not in groups:
            groups.append(d['grp'])
    # entrance: door in the south wall of the first hall, then a corridor to the spine
    sector('CN', rect(160, -192, 224, -16), ceil=128, light=110, ftex='FLOOR1', ctex='CEIL1', wall='WALL02')
    sector('LD', rect(160, -16, 224, 0), ceil=0, light=90, ftex='FLOOR1', ctex='CEIL1', wall='DOORTRAK', id=250)
    nb = len(groups)
    xmax = 288 + PITCH * (nb - 1) + 32 + 96
    sector('SP', rect(96, SPINE_Y[0], xmax, SPINE_Y[1]), ceil=160, light=110, ftex='FLOOR3', ctex='CEIL2', wall='GREY1')
    door = dict(special=12, args=(250, 16, 150, 0, 0), use=True, repeat=True, tex='DOOR1')
    LINKS[frozenset(('A', 'LD'))] = door
    LINKS[frozenset(('LD', 'CN'))] = door
    LINKS[frozenset(('CN', 'SP'))] = dict(special=80, args=(192, 0, 0, 0, 0), cross=True, repeat=True)

    # script numbers: a script started by a line special or by ACS_Execute is called by a byte (0-255)
    nxt_red, nxt_help = 209, 214
    for d in DEMOS:
        if d['warn']:
            d['_red'] = nxt_red; nxt_red += 1
        if d['helper']:
            d['_H'] = nxt_help; nxt_help += 1
    assert nxt_red <= 214 and nxt_help <= 250, (nxt_red, nxt_help)
    # wing sectors first (they must come before their doors and rooms), then the rooms
    wing_of = {}
    rooms = []
    for gi, g in enumerate(groups):
        members = [i for i, d in enumerate(DEMOS) if d['grp'] == g]
        rows = (len(members) + 1) // 2
        bx = 288 + PITCH * gi
        L = rows * STR + 32
        sector('BR%d' % gi, rect(bx - 32, SPINE_Y[0] - L, bx + 32, SPINE_Y[0]), ceil=128, light=110, ftex='FLOOR1', ctex='CEIL1',
               wall=FWALL[gi % len(FWALL)])
        LINKS[frozenset(('SP', 'BR%d' % gi))] = dict(special=80, args=(193 + gi, 0, 0, 0, 0), cross=True, repeat=True)
        wing_of[gi] = (bx, members)
    for gi, g in enumerate(groups):
        bx, members = wing_of[gi]
        wall = FWALL[gi % len(FWALL)]
        for j, i in enumerate(members):
            n = i + 1
            side = 'W' if j % 2 == 0 else 'E'
            row = j // 2
            yt = SPINE_Y[0] - 16 - row * STR
            yc = yt - RW // 2
            sgn = -1 if side == 'W' else 1
            x0 = bx + sgn * 32
            x_ent = x0 + sgn * 56
            ftex = FLOORS[(gi + j) % 3]
            r = Room(n, side, x_ent, yc, wall, ftex)
            d = DEMOS[i]
            rooms.append((r, d, gi))
            br = 'BR%d' % gi
            # entry strip E, door D, room R, in this order (see the comments in the module doc)
            sector('E%d' % n, r.rect(-40, -32, 0, 32), ceil=128, light=120, ftex='FLOOR1', ctex='CEIL1', wall=wall)
            sector('D%d' % n, r.rect(-56, -32, -40, 32), ceil=0, light=110, ftex='FLOOR1', ctex='CEIL1', wall='DOORTRAK', id=1000 + n)
            ceil = 144 if d['kit'].startswith('ceil') else 160
            far = 192
            poly = [r.pt(0, -80), r.pt(far, -80), r.pt(far, -72), r.pt(far, -48), r.pt(far, -24), r.pt(far, 24),
                    r.pt(far, 48), r.pt(far, 72), r.pt(far, 80), r.pt(0, 80)]
            sector(r.R, poly, ceil=ceil, light=120, ftex=ftex, ctex='CEIL1', wall=wall)
            # the door D sits between the corridor and E: D must also be later than BR
            LINKS[frozenset((br, 'D%d' % n))] = dict(special=12, args=(1000 + n, 32, 150, 0, 0), use=True, repeat=True, tex='DOOR1')
            LINKS[frozenset(('D%d' % n, 'E%d' % n))] = dict(special=12, args=(1000 + n, 32, 150, 0, 0), use=True, repeat=True, tex='DOOR1')
            LINKS[frozenset(('E%d' % n, r.R))] = dict(special=80, args=(n, 0, 0, 0, 0), cross=True, repeat=bool(d['again']))
            # switches and the line with the id of the room on the far wall
            sw = frozenset((r.pt(far, -24), r.pt(far, 24)))
            ONE[sw] = dict(special=80, args=(n, 0, 0, 0, 0), use=True, repeat=True, tex='SWS1')
            red = frozenset((r.pt(far, -72), r.pt(far, -48)))
            ONE[red] = dict(special=80, args=(d['_red'], 0, 0, 0, 0), use=True, repeat=True, tex='SWX1') if d['warn'] else dict(tex='GREY1')
            seg = frozenset((r.pt(far, 48), r.pt(far, 72)))
            ONE[seg] = dict(id=n, tex='GREY1')
            if d['kit'] == 'stage':
                ONE[seg].update(use_nospecial=True, tex='SWS1')
            S[r.R]['holes'] = []
    # the kits come after all the rooms (the sectors of a kit follow the room in the table)
    for r, d, gi in rooms:
        ex = {}
        build_kit(r, d, ex)
        d['_ex'] = ex
    if os.environ.get('LAB_AUTORUN'):      # developer: a teleport destination in front of the entry line of every room that needs the player
        S['A']['holes'].append(rect(120, 92, 126, 108))     # and a strip the (auto-walking) player crosses to start the walk
        sector('DEV', rect(120, 92, 126, 108), ceil=160, light=90, ftex='FLOOR2', ctex='CEIL1', wall='WALL02')
        LINKS[frozenset(('A', 'DEV'))] = dict(special=80, args=(250, 0, 0, 0, 0), cross=True)
        for r, d, gi in rooms:
            if d['walk']:
                p = r.pt(-20, 0)
                thing(14, p[0], p[1], r.angle_fw, id=1700 + r.n)
    # torches along the spine and at the foot of each wing
    for x in range(224, xmax - 64, 384):
        thing(20201, x, -250)
        thing(20201, x, -198)
    for gi in range(nb):
        bx, members = wing_of[gi]
        rows = (len(members) + 1) // 2
        thing(20205, bx, SPINE_Y[0] - rows * STR - 16)
    return groups, rooms


# ---------------------------------------------------------------------------------------------------------- the ACS
def subst(s, r, d):
    n = r.n
    ex = dict(d.get('_ex', {}))
    mp = dict(t=n, S=tid('S', n), M=tid('M', n), M2=tid('M2', n), Dst=tid('Dst', n), H=d.get('_H', 0), fw=r.angle_fw,
              fwb=0 if r.sgn == 1 else 128, up=64, down=192)
    mp.update(ex)
    return re.sub(r'\{(\w+)\}', lambda m: str(mp[m.group(1)]) if m.group(1) in mp else m.group(0), s)


def acs_text(groups, rooms, autorun=False):
    out = ['#include "zcommon.acs"', '', '// generated by tools/make_script_lab.py: do not edit', '']
    out.append('script 192 (void)\n{\n    Print(s:"SCRIPT LAB\\n", s:"One wing for every group of functions. Pick a door.");\n}\n')
    cnt = {g: sum(1 for d in DEMOS if d['grp'] == g) for g in groups}
    for gi, g in enumerate(groups):
        out.append('script %d (void)\n{\n    Print(s:"%s\\n", s:"%d rooms");\n}\n' % (193 + gi, g, cnt[g]))
    for r, d, gi in rooms:
        n = r.n
        call = subst(d['call'], r, d)
        reset = subst(d['reset'], r, d) if d['reset'] else ''
        text = d['text'] if d['text'] else call.split('\n')[0].strip().rstrip(';')
        text = subst(text, r, d).replace('"', "'").replace('\\n', ' ')
        name = d['name'].replace('"', "'")
        if d['helper']:
            out.append(subst(d['helper'], r, d) + '\n')
        head = '    Print(s:"%s\\n", s:"%s");' % (name, text)
        vars_ = ('    ' + d['vars'] + '\n') if d['vars'] else ''
        if d['warn']:
            out.append('script %d (void)\n{\n    Print(s:"%s\\n", s:"This one leaves the map. Press the red switch to do it.");\n}\n' % (n, name))
            out.append('script %d (void)\n{\n%s\n    Delay(52);\n    %s\n}\n' % (d['_red'], head, call.replace('\n', '\n    ')))
        else:
            body = '%s%s\n    Delay(52);\n    %s\n' % (vars_, head, call.replace('\n', '\n    '))
            body += '    Delay(%d);\n' % d['wait']
            if reset:
                body += '    %s\n' % reset.replace('\n', '\n    ')
            out.append('script %d (void)\n{\n%s}\n' % (n, body))
    if autorun:
        out.append('script 900 OPEN\n{\n    Delay(35);')
        lo, hi = [int(x) for x in os.environ.get('LAB_RANGE', '1-999').split('-')]
        for r, d, gi in rooms:
            if not d['warn'] and lo <= r.n <= hi:
                out.append('    ACS_Execute(%d, 0, 0, 0, 0);' % r.n)
        out.append('}\n')
        out.append('script 250 (void)\n{\n    Delay(700);')
        for r, d, gi in rooms:
            if d['walk']:
                out.append('    Teleport(%d);\n    Delay(10);\n    ThrustThing(%d, 25);\n    Delay(%d);' % (1700 + r.n, 0 if r.sgn == 1 else 128, d['wait'] + 130))
        out.append('}\n')
    return '\n'.join(out)


def compile_acs(text, accdir):
    d = tempfile.mkdtemp()
    for f in ('zcommon.acs', 'zdefs.acs', 'zspecial.acs', 'zwvars.acs'):
        shutil.copy(os.path.join(accdir, f), d)
    open(os.path.join(d, 'lab.acs'), 'w').write(text)
    r = subprocess.run([os.path.join(accdir, 'acc'), '-h', 'lab.acs', 'lab.o'], cwd=d, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(os.path.join(d, 'lab.o')):
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit('ACS did not compile')
    return open(os.path.join(d, 'lab.o'), 'rb').read(), text


def main():
    out = sys.argv[1]
    zd = sys.argv[2] if len(sys.argv) > 2 else shutil.which('zdbsp')
    accdir = sys.argv[3] if len(sys.argv) > 3 else os.environ.get('ACC_DIR', '/home/claude/zdoom/acc')
    groups, rooms = build_lab()
    text = M.build()
    src = acs_text(groups, rooms, bool(os.environ.get('LAB_AUTORUN')))
    open(os.path.splitext(out)[0] + '.acs', 'w').write(src)
    acs, src = compile_acs(src, accdir)
    tmp = tempfile.mkdtemp()
    srcw = os.path.join(tmp, 'in.wad')
    M.wad(srcw, [('MAP01', b''), ('TEXTMAP', text.encode()), ('ENDMAP', b'')])
    if not zd:
        raise SystemExit('zdbsp not found')
    subprocess.check_call([zd, '-w', '-o', os.path.join(tmp, 'out.wad'), srcw])
    lumps = M.read_wad(os.path.join(tmp, 'out.wad'))
    names = [n for n, _ in lumps]
    i = names.index('ENDMAP') if 'ENDMAP' in names else len(lumps)
    lumps.insert(i, ('BEHAVIOR', acs))
    M.wad(out, lumps)
    import json
    man = dict(sectors=list(M.S), rooms=[dict(n=r.n, name=d['name'], grp=d['grp'], kit=d['kit'], expect=d['expect'], warn=d['warn'], walk=d['walk'],
                                              wing=gi, side='E' if r.sgn == 1 else 'W', x_ent=r.x_ent, yc=r.yc) for r, d, gi in rooms])
    json.dump(man, open(os.path.splitext(out)[0] + '.json', 'w'), indent=1)
    print('wrote', out, [n for n, _ in lumps], '%d sectors' % len(M.S), '%d rooms' % len(rooms))


if __name__ == '__main__':
    main()
