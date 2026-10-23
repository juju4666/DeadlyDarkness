#!/usr/bin/env python3
"""A test PWAD (MAP01) for the baked-light shadows: a 512 x 512 room with a pillar, built with a tiny BSP builder."""
import struct, math, sys, os
HEXEN = bool(os.environ.get('HEXEN'))
DOORLINE = os.environ.get('DOORTAG')

ROOM = [((0, 0), (0, 512)), ((0, 512), (512, 512)), ((512, 512), (512, 0)), ((512, 0), (0, 0))]
PIL = [((224, 224), (288, 224)), ((288, 224), (288, 288)), ((288, 288), (224, 288)), ((224, 288), (224, 224))]
WALLTEX = sys.argv[2] if len(sys.argv) > 2 else 'WALL01'

lines = ROOM + PIL
verts = []
def vid(p):
    if p not in verts:
        verts.append(p)
    return verts.index(p)

def seg_of(a, b, line):
    la = lines[line][0]
    off = int(round(math.hypot(a[0] - la[0], a[1] - la[1])))
    return (a, b, line, off)

segs0 = [seg_of(a, b, i) for i, (a, b) in enumerate(lines)]

def side(part, p):
    (x, y, dx, dy) = part
    return dx * (p[1] - y) - dy * (p[0] - x)    # < 0 right, > 0 left

def convex(segs):
    for (a, b, _, _) in segs:
        for (c, d, _, _) in segs:
            for p in (c, d):
                if (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) > 0:
                    return False
    return True

ssectors = []   # list of lists of segs
nodes = []

def bbox(segs):
    xs = [p[0] for s in segs for p in s[:2]]
    ys = [p[1] for s in segs for p in s[:2]]
    return (max(ys), min(ys), min(xs), max(xs))   # top bottom left right

def build(segs):
    if convex(segs):
        ssectors.append(segs)
        return 0x8000 | (len(ssectors) - 1)
    best = None
    for s in segs:
        part = (s[0][0], s[0][1], s[1][0] - s[0][0], s[1][1] - s[0][1])
        r = l = sp = 0
        for t in segs:
            sa, sb = side(part, t[0]), side(part, t[1])
            if sa == 0 and sb == 0:
                continue
            if sa <= 0 and sb <= 0: r += 1
            elif sa >= 0 and sb >= 0: l += 1
            else: sp += 1
        if r + sp == 0 or l + sp == 0:
            continue
        score = sp * 8 + abs(r - l)
        if best is None or score < best[0]:
            best = (score, part)
    part = best[1]
    right, left = [], []
    for t in segs:
        sa, sb = side(part, t[0]), side(part, t[1])
        if sa == 0 and sb == 0:
            same = (t[1][0] - t[0][0]) * part[2] + (t[1][1] - t[0][1]) * part[3] > 0
            (right if same else left).append(t)
        elif sa <= 0 and sb <= 0: right.append(t)
        elif sa >= 0 and sb >= 0: left.append(t)
        else:
            # split at the partition
            x, y, dx, dy = part
            (ax, ay), (bx, by) = t[0], t[1]
            den = (bx - ax) * dy - (by - ay) * dx
            u = ((x - ax) * dy - (y - ay) * dx) / den
            m = (int(round(ax + (bx - ax) * u)), int(round(ay + (by - ay) * u)))
            a_seg = (t[0], m, t[2], t[3])
            b_seg = (m, t[1], t[2], t[3] + int(round(math.hypot(m[0] - t[0][0], m[1] - t[0][1]))))
            (right if sa < 0 else left).append(a_seg if sa < 0 else a_seg)
            (left if sa < 0 else right).append(b_seg)
    node = [part, bbox(right), bbox(left), None, None]
    nodes.append(node)
    idx = len(nodes) - 1
    node[3] = build(right)
    node[4] = build(left)
    return idx

root = build(segs0)
# the root must be the last node in the lump (Doom starts at the last one); reorder so root is last
order = list(range(len(nodes)))
# nodes were appended pre-order; Doom wants the root last: renumber by reversing
n = len(nodes)
remap = {i: n - 1 - i for i in range(n)}
nodes_r = [None] * n
for i, nd in enumerate(nodes):
    c3 = nd[3] if nd[3] & 0x8000 else remap[nd[3]]
    c4 = nd[4] if nd[4] & 0x8000 else remap[nd[4]]
    nodes_r[remap[i]] = (nd[0], nd[1], nd[2], c3, c4)

V = b''
for (x, y) in []: pass
vert_idx = {}
for s in segs0: pass
all_pts = []
for ss in ssectors:
    for s in ss:
        for p in s[:2]:
            vid(p)
for a, b in lines:
    vid(a); vid(b)
vert = b''.join(struct.pack('<hh', x, y) for x, y in verts)
ld = sd = b''
for i, (a, b) in enumerate(lines):
    if HEXEN:
        sp = (12, int(DOORLINE), 0, 0, 0, 0) if (DOORLINE and i == 0) else (0, 0, 0, 0, 0, 0)
        ld += struct.pack('<hhhB5Bhh', vid(a), vid(b), 1, sp[0], *sp[1:], i, -1)
    else:
        ld += struct.pack('<hhhhhhh', vid(a), vid(b), 1, 0, 0, i, -1)
    sd += struct.pack('<hh8s8s8sh', 0, 0, b'-', b'-', WALLTEX.encode().ljust(8, b'\0'), 0)
segb = b''
ssb = b''
first = 0
for ss in ssectors:
    ssb += struct.pack('<hh', len(ss), first)
    for (a, b, line, off) in ss:
        t = math.atan2(b[1] - a[1], b[0] - a[0])
        segb += struct.pack('<hhHhhh', vid(a), vid(b), int(round(t / (2 * math.pi) * 65536)) & 0xFFFF, line, 0, off)
    first += len(ss)
nb = b''
for (part, rb, lb, c1, c2) in nodes_r:
    nb += struct.pack('<hhhh', *part) + struct.pack('<hhhh', *rb) + struct.pack('<hhhh', *lb) + struct.pack('<HH', c1, c2)
sector = struct.pack('<hh8s8shhh', 0, 128, b'FLOOR1', b'CEIL1', 45, 0, int(DOORLINE) if DOORLINE else 0)

things = b''
def th(x, y, ang, typ):
    global things
    if HEXEN:
        things += struct.pack('<hhhhhhhB5B', 0, x, y, 0, ang, typ, 2023, 0, 0, 0, 0, 0, 0)
    else:
        things += struct.pack('<hhhhh', x, y, ang, typ, 7)
th(int(os.environ.get('PX', 70)), int(os.environ.get('PY', 70)), int(os.environ.get('PANG', 55)), 1); th(300, 60, 90, 2); th(200, 60, 90, 3); th(340, 60, 90, 4)
LIGHTS = [tuple(int(v) for v in a.split(',')) for a in sys.argv[3:]]   # x,y,type
for x, y, typ in LIGHTS:
    th(x, y, 0, typ)
BGL = open(sys.argv[2+0] if False else '/tmp/bglight_test.txt').read().encode() if __import__('os').path.exists('/tmp/bglight_test.txt') else b''
lumps = [('MAP01', b''), ('THINGS', things), ('LINEDEFS', ld), ('SIDEDEFS', sd), ('VERTEXES', vert), ('SEGS', segb),
         ('SSECTORS', ssb), ('NODES', nb), ('SECTORS', sector), ('REJECT', b'\0'), ('BLOCKMAP', b'')] + ([('BGLIGHT', BGL)] if BGL else []) + ([('BEHAVIOR', b'ACS\x00' + struct.pack('<iii', 8, 0, 0))] if HEXEN else [])
d = b'PWAD' + struct.pack('<ii', len(lumps), 12 + sum(len(x[1]) for x in lumps))
pos = 12; tbl = b''
for nme, b in lumps:
    d += b
    tbl += struct.pack('<ii8s', pos if b else 0, len(b), nme.encode().ljust(8, b'\0'))
    pos += len(b)
open(sys.argv[1], 'wb').write(d + tbl)
print('ssectors', len(ssectors), 'nodes', len(nodes_r), 'verts', len(verts))
