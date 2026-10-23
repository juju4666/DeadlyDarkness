"""art_props.py - pickups, ammo and scenery as 3D models.  PROPS[family] = (builder(frame_index) -> Fig, options)
options: spin (degrees of turn per frame; the model turns on the spot), elev (camera elevation), facing (fixed turn)"""
import math
import numpy as np
from art_render import Fig, mat, rot_matrix
from art_rig import biped, pose, ease, blob_pool
from art_monsters import M, BLOOD, BLOOD2, FLESH, BONE, STEEL, DSTEEL, BLACK
from art_monsters2 import GLOW, flame, ring
import art_guns

WHITE = M((236, 236, 232), noise=0.04)
STONE = M((150, 146, 138), noise=0.16)
DSTONE = M((104, 100, 96), noise=0.16)
MARBLE = M((52, 120, 84), spec=0.5, shin=26, noise=0.12)
RED_M = M((150, 36, 40), spec=0.4, noise=0.1)
GOLD = M((232, 190, 70), spec=0.7, shin=30, noise=0.03)
WOODM = M((112, 76, 44), spec=0.2, noise=0.22)
LEAF = M((46, 112, 52), noise=0.2)
RUST = M((130, 70, 40), spec=0.3, noise=0.2)


def lift(f, z=0.0, centre=True):
    """move a figure so that its lowest point is at height z (and, if asked, centre it in x/y)"""
    g = f.transformed(np.eye(3), t=(0, 0, z - f.lowest()))
    if centre:
        xs = [p['c'][0] for p in g.prims]
        ys = [p['c'][1] for p in g.prims]
        g = g.transformed(np.eye(3), t=(-(min(xs) + max(xs)) / 2, -(min(ys) + max(ys)) / 2, 0))
    return g


def scaled(f, s):
    g = Fig()
    for p in f.prims:
        q = dict(p)
        q['c'] = p['c'] * s
        q['s'] = p['s'] * s
        g.prims.append(q)
    return g


# ---------------------------------------------------------------------------------------------- weapons on the floor
def gun_pickup(name, s):
    def b(fi):
        g = art_guns.gun(name)
        g = g.transformed(rot_matrix(rx=0), t=(0, 0, 0))
        return lift(scaled(g, s), 1.5)
    return b


# ---------------------------------------------------------------------------------------------- keys
def key_model(rgb, dark=False):
    body = M(rgb, spec=0.8, shin=34, noise=0.03)
    edge = M((220, 220, 226) if dark else tuple(min(255, c + 60) for c in rgb), spec=0.8, shin=34, noise=0.03)
    f = Fig()
    # standing upright, bow at the top; the bow is a ring of beads
    ring(f, (0, 0, 17), 4.4, 14, 1.35, body, 'y')
    f.sph((0, 0, 17), 2.2, M((20, 20, 24)) if not dark else M((200, 200, 210), spec=0.8))
    f.cone((0, 0, 2.5), (0, 0, 12.8), 1.1, 1.1, body)
    f.box((0, 0, 8.2), (0.7, 0.7, 0.45), edge) if False else None
    for z, ln in ((3.6, 3.6), (6.0, 2.6), (8.2, 3.8)):
        f.box((ln / 2.0 + 0.4, 0, z), (ln / 2.0, 0.9, 0.7), body)
    f.cone((0, 0, 12.4), (0, 0, 13.4), 1.7, 1.7, edge)
    return f


KEYCOL = {'BKEY': (40, 90, 255), 'YKEY': (245, 220, 40), 'RKEY': (240, 40, 40), 'GKEY': (50, 210, 70), 'CKEY': (50, 225, 235),
          'MKEY': (230, 60, 220), 'KKEY': (36, 36, 44), 'AKEY': (150, 152, 162), 'WKEY': (246, 246, 246)}


def skull_key(rgb):
    def b(fi):
        f = Fig()
        f.cyl((0, 0, 1.5), 7, 3.0, M((70, 66, 66), spec=0.4, noise=0.2))
        f.ell((0, 0, 10), (5.2, 5.4, 5.6), BONE)
        f.ell((0, 3.6, 7.0), (3.3, 2.6, 2.8), BONE)
        f.sph((-2.0, 4.6, 10.6), 1.4, GLOW(*rgb))
        f.sph((2.0, 4.6, 10.6), 1.4, GLOW(*rgb))
        f.sph((0, 0, 15.5), 1.4, GLOW(*rgb)) if fi else None
        return f
    return b


# ---------------------------------------------------------------------------------------------- health and armour
def stimpack(fi):
    f = Fig()
    f.box((0, 0, 4.5), (7.2, 4.6, 4.2), WHITE)
    f.box((0, 0, 9.0), (7.4, 4.8, 0.6), M((200, 200, 200), spec=0.3))
    f.box((0, 4.7, 4.7), (3.4, 0.25, 1.0), M((210, 30, 36)))
    f.box((0, 4.7, 4.7), (1.0, 0.25, 3.4), M((210, 30, 36)))
    f.box((0, 0, 9.8), (2.2, 0.6, 0.5), M((120, 124, 134), spec=0.6))
    return f


def potion(fi):
    f = Fig()
    glass = M((70, 120, 255), spec=0.9, shin=40, noise=0.02, glow=0.3)
    f.ell((0, 0, 7.0), (5.6, 5.6, 6.0), glass)
    f.cone((0, 0, 11.0), (0, 0, 17.0), 2.6, 1.9, glass)
    f.cone((0, 0, 17.0), (0, 0, 20.0), 2.2, 2.4, M((120, 80, 44), noise=0.2))
    f.ell((0, 0, 5.0), (4.6, 4.6, 4.2), GLOW(120, 190, 255) if fi % 2 == 0 else GLOW(170, 220, 255))
    f.ell((-2.2, 4.6, 9.0), (1.0, 0.5, 1.8), M((255, 255, 255), glow=1, flat=True))
    return f


def helmet_bonus(fi):
    f = Fig()
    g = M((60, 170, 80), spec=0.8, shin=34, noise=0.04)
    f.ell((0, 0, 8), (9, 10, 8.6), g)
    f.cyl((0, 0, 3.6), 11.0, 1.4, M((40, 120, 60), spec=0.6, noise=0.04))
    f.box((0, 9.6, 7.2), (1.4, 0.4, 5.4), M((40, 120, 60), spec=0.6, noise=0.04))
    f.sph((0, 0, 17), 0.8, GLOW(200, 255, 200)) if fi % 2 else None
    return f


def vest(green):
    def b(fi):
        f = Fig()
        col = M((70, 150, 80) if green else (70, 110, 230), spec=0.5, shin=26, noise=0.05)
        panel = M((108, 188, 116) if green else (130, 170, 255), spec=0.6, shin=30, noise=0.05)
        dk = M((40, 80, 46) if green else (40, 60, 140), noise=0.06)
        f.ell((0, 0, 10), (12.5, 6.0, 9.0), col)
        f.box((0, 5.4, 10), (8.4, 0.9, 7.2), panel)
        for s in (-1, 1):
            f.box((s * 10.6, 0, 18.5), (3.4, 5.4, 1.4), col, rot=(0, s * 24, 0))
            f.ell((s * 12.4, 0, 17), (3.4, 4.5, 2.8), dk)
        f.box((0, 0, 3.2), (12.2, 6.2, 1.1), dk)
        if fi:
            for k in (-4, 0, 4):
                f.box((k, 6.0, 10.5), (0.45, 0.2, 6), GLOW(255, 255, 255))
        if not green:
            f.box((0, 6.0, 12), (2.2, 0.3, 2.2), GOLD)
        return lift(f, 0.5)
    return b


def berserk(fi):
    f = Fig()
    f.box((0, 0, 6), (16, 9, 6), M((176, 28, 34), spec=0.3, noise=0.06))
    f.box((0, 0, 12.5), (16.4, 9.4, 0.7), M((120, 20, 24)))
    f.box((0, 9.2, 6.2), (6, 0.3, 1.8), WHITE)
    f.box((0, 9.2, 6.2), (1.8, 0.3, 5.0), WHITE)
    for s in (-1, 1):
        f.box((s * 10, 0, 12.8), (1.6, 3.0, 0.6), STEEL)
    return f


def computer_map(fi):
    f = Fig()
    f.box((0, 0, 7), (12.5, 1.6, 10), M((56, 60, 70), spec=0.5, noise=0.04))
    f.box((0, 1.5, 7), (10.6, 0.3, 8.2), M((16, 40, 24), spec=0.5, noise=0.02))
    for k in range(-3, 4):
        f.box((k * 2.8, 1.9, 7), (0.12, 0.1, 7.6), GLOW(40, 220, 90))
        f.box((0, 1.9, 7 + k * 2.4), (9.8, 0.1, 0.12), GLOW(40, 220, 90))
    f.box((-6 + 3 * (fi % 4), 2.1, 6 + (fi * 2) % 6), (1.2, 0.2, 1.2), GLOW(255, 255, 160))
    f.box((0, 0, 0.8), (6, 3, 0.8), M((40, 44, 52)))
    f.box((0, 0, 2.5), (1.4, 1.2, 2.0), M((40, 44, 52)))
    return f


def goggles(fi):
    f = Fig()
    band = M((46, 50, 56), noise=0.1)
    lens = GLOW(120, 255, 130) if fi == 0 else GLOW(190, 255, 190)
    for s in (-1, 1):
        f.cyl((s * 6.2, 0, 5.2), 4.6, 5.0, M((70, 74, 82), spec=0.6, noise=0.04), rot=(90, 0, 0))
        f.cyl((s * 6.2, 2.7, 5.2), 3.5, 0.8, lens, rot=(90, 0, 0))
    f.box((0, 0, 5.4), (2.2, 1.0, 1.0), band)
    f.box((0, -3, 5.4), (11, 0.7, 1.4), band) if False else None
    f.cone((-11, 0, 5.4), (-15, -6, 5.4), 1.2, 1.2, band)
    f.cone((11, 0, 5.4), (15, -6, 5.4), 1.2, 1.2, band)
    return f


def rad_suit(fi):
    f, J = biped(dict(leg1=14, leg2=14, torso_len=20, torso_w=7.4, torso_d=5, torso_kind='box', sh_w=9, arm1=9, arm2=9, head_r=4.8,
                      pants=M((220, 120, 30), noise=0.06), boots=M((60, 60, 60)), cloth=M((230, 130, 36), noise=0.06),
                      sleeve=M((230, 130, 36), noise=0.06), glove=M((60, 60, 60)), skin=M((230, 130, 36)), leg_r=3.6, arm_r=2.8),
                 pose(shL=14, shR=14, abL=18, abR=18))
    f.ell(J['head'] + np.array([0, 0.6, 0.6]), (5.8, 5.8, 5.6), M((230, 130, 36), spec=0.4, noise=0.05))
    f.ell(J['head'] + np.array([0, 3.2, 0.5]), (4.0, 2.4, 3.0), M((30, 60, 40), spec=0.9, shin=40, noise=0.02))
    f.cyl(J['pelvis'] + np.array([0, 0, 15]), 3.4, 4, M((240, 220, 50)), rot=(0, 0, 0)) if False else None
    f.box(J['pelvis'] + np.array([0, 5.2, 12]), (3.4, 0.4, 3.4), M((250, 230, 50)))
    # hang it from a hook: a stand pole behind it
    f.cone((0, -7, 0), (0, -7, 52), 1.0, 1.0, DSTEEL)
    f.box((0, -4, 52), (6, 3, 0.8), DSTEEL)
    return f


# ---------------------------------------------------------------------------------------------- ammo
def clip(fi):
    f = Fig()
    f.box((0, 0, 5.5), (2.0, 3.4, 5.4), M((80, 84, 90), spec=0.5, noise=0.04))
    f.box((0, 0, 11.3), (1.7, 3.0, 0.4), BRASS)
    f.box((0, 3.4, 5.5), (1.6, 0.2, 4.8), M((50, 52, 58)))
    return f


BRASS = M((222, 176, 70), spec=0.7, shin=30, noise=0.04)
COPPER = M((200, 110, 60), spec=0.6, shin=28, noise=0.05)


def bullet_box(fi):
    f = Fig()
    f.box((0, 0, 4.5), (8.4, 6.4, 4.2), M((80, 100, 70), noise=0.1))
    f.box((0, 6.3, 4.6), (4.6, 0.25, 1.6), M((230, 210, 70)))
    for x in range(-3, 4):
        for y in (-2.0, 2.0):
            f.cone((x * 1.7, y, 8.0), (x * 1.7, y, 11.2), 0.7, 0.7, BRASS)
            f.cone((x * 1.7, y, 11.2), (x * 1.7, y, 12.6), 0.7, 0.1, COPPER)
    return f


def shell(c0, c1):
    f = Fig()
    f.cone(c0, c1, 1.7, 1.7, M((216, 40, 40), spec=0.4, noise=0.05))
    return f


def shells(fi):
    f = Fig()
    for (x, y, rz) in ((-2.5, 0, 0), (2.5, 0, 0), (0, 3.4, 0)):
        f.cone((x - 4.5, y, 1.8), (x + 3.5, y, 1.8), 1.8, 1.8, M((216, 40, 40), spec=0.4, noise=0.05))
        f.cone((x - 5.5, y, 1.8), (x - 4.5, y, 1.8), 1.85, 1.85, BRASS)
    return f


def shell_box(fi):
    f = Fig()
    f.box((0, 0, 5), (16, 6, 4.6), M((190, 50, 40), noise=0.1))
    f.box((0, 6.1, 5.2), (8, 0.25, 1.8), M((246, 220, 80)))
    for x in range(-5, 6):
        f.cone((x * 2.9, -1.6, 9.5), (x * 2.9, -1.6, 13.0), 1.2, 1.2, M((216, 40, 40), spec=0.4))
        f.cone((x * 2.9, -1.6, 9.0), (x * 2.9, -1.6, 9.8), 1.25, 1.25, BRASS)
    return f


def cell(fi):
    f = Fig()
    f.cyl((0, 0, 6), 4.2, 12, M((60, 90, 150), spec=0.6, noise=0.04))
    f.cyl((0, 0, 6), 4.5, 3, GLOW(100, 210, 255))
    f.cyl((0, 0, 12.4), 2.0, 0.8, STEEL)
    return f


def cell_pack(fi):
    f = Fig()
    f.box((0, 0, 7), (14, 6, 7), M((54, 78, 128), spec=0.5, noise=0.05))
    for x in (-7, -2.3, 2.3, 7):
        f.cyl((x, 6.1, 7), 1.7, 0.6, GLOW(100, 210, 255), rot=(90, 0, 0))
    f.box((0, 0, 14.4), (14.4, 6.4, 0.8), DSTEEL)
    return f


def rocket_ammo(fi):
    f = Fig()
    f.cone((0, 0, 0.5), (0, 0, 20), 2.3, 2.3, M((110, 118, 96), spec=0.4, noise=0.06))
    f.cone((0, 0, 20), (0, 0, 25), 2.3, 0.2, M((226, 70, 40), spec=0.4))
    for k in range(4):
        a = k * math.pi / 2
        f.box((math.cos(a) * 2.8, math.sin(a) * 2.8, 2.5), (0.3, 1.4, 2.2) if k % 2 == 0 else (1.4, 0.3, 2.2), DSTEEL)
    return f


def rocket_box(fi):
    f = Fig()
    f.box((0, 0, 4.5), (26, 8.6, 4.2), M((92, 104, 70), noise=0.1))
    f.box((0, 8.7, 4.7), (10, 0.3, 1.8), M((246, 190, 60)))
    for k in range(4):
        x = -15 + k * 10
        f.cone((x, 0, 9), (x, 0, 16), 3.0, 3.0, M((108, 114, 92), spec=0.3)) if False else None
        f.cone((x, 0, 8.6), (x, 0, 15.5), 2.4, 2.4, M((110, 118, 96), spec=0.4, noise=0.06))
        f.cone((x, 0, 15.5), (x, 0, 19.5), 2.4, 0.2, M((226, 70, 40), spec=0.4))
    return f


def backpack(fi):
    f = Fig()
    cl = M((64, 84, 54), noise=0.12)
    f.ell((0, 0, 11), (10, 6, 11), cl)
    f.box((0, 6.4, 6), (7, 1.8, 4), M((54, 70, 46), noise=0.1))
    for s in (-1, 1):
        f.box((s * 10, 1, 7), (2.6, 3.2, 5), M((54, 70, 46), noise=0.1))
        f.cone((s * 4.5, 4.0, 22), (s * 5.5, 4.5, 8), 0.9, 0.9, M((120, 90, 50), noise=0.2))
    f.cone((0, 0, 20), (0, 0, 24), 5.0, 3.2, M((54, 70, 46), noise=0.1))
    f.ell((0, 0, 24), (3.4, 3, 1.2), M((230, 190, 60), spec=0.5))
    return f


# ---------------------------------------------------------------------------------------------- decorations
def column(h, r, mat_body, mat_cap, fi=0, top=None):
    f = Fig()
    f.box((0, 0, 1.5), (r * 1.5, r * 1.5, 1.5), mat_cap)
    f.cone((0, 0, 3), (0, 0, h - 3), r, r * 0.9, mat_body)
    for k in range(8):
        a = k * math.pi / 4
        f.cone((math.cos(a) * r * 0.95, math.sin(a) * r * 0.95, 4), (math.cos(a) * r * 0.85, math.sin(a) * r * 0.85, h - 4), 0.7, 0.6, mat_cap)
    f.box((0, 0, h - 1.5), (r * 1.5, r * 1.5, 1.5), mat_cap)
    if top:
        top(f, h)
    return f


def col_tech(h):
    def b(fi):
        f = Fig()
        dk = M((60, 66, 80), spec=0.5, noise=0.05)
        f.box((0, 0, 2), (9, 9, 2), dk)
        f.box((0, 0, h / 2), (6.5, 6.5, h / 2 - 2), M((96, 104, 124), spec=0.5, noise=0.06))
        for z in range(10, int(h) - 6, 11):
            f.box((0, 6.7, z), (4, 0.3, 2.4), M((24, 28, 36)))
            f.box((0, 6.9, z), (3.4, 0.2, 0.5), GLOW(80, 220, 255) if (z // 11 + fi) % 2 else GLOW(255, 120, 60))
        f.box((0, 0, h - 1.5), (8, 8, 1.5), dk)
        return f
    return b


def col_heart(fi):
    f = column(30, 8, MARBLE, STONE)
    p = 1.0 + 0.12 * fi
    f.ell((0, 0, 37), (6 * p, 5 * p, 6.5 * p), M((200, 30, 50), spec=0.6, noise=0.12, glow=0.1 * fi))
    f.limb((-2, 0, 41), (-3, 0, 45), 1.4, 1.1, M((150, 20, 50)))
    f.limb((2, 0, 41), (3, 0, 45), 1.4, 1.1, M((60, 60, 150)))
    return f


def col_skull(fi):
    f = column(34, 8, M((140, 36, 40), spec=0.4, noise=0.1), DSTONE)
    f.ell((0, 0, 41), (5, 5.2, 5.5), BONE)
    f.ell((0, 3.4, 38.6), (3.2, 2.4, 2.6), BONE)
    f.sph((-1.9, 4.4, 41.6), 1.3, M((20, 10, 10)))
    f.sph((1.9, 4.4, 41.6), 1.3, M((20, 10, 10)))
    return f


def floor_lamp(fi):
    f = Fig()
    f.cyl((0, 0, 1.5), 6, 3, DSTEEL)
    f.cone((0, 0, 3), (0, 0, 44), 1.4, 1.1, M((80, 84, 96), spec=0.6, noise=0.04))
    f.sph((0, 0, 47), 4.4, GLOW(255, 240, 190))
    f.cone((0, 0, 44), (0, 0, 46), 3.0, 4.0, DSTEEL)
    return f


def tech_pylon(fi):
    f = Fig()
    dk = M((54, 58, 70), spec=0.5, noise=0.05)
    f.cyl((0, 0, 4), 10, 8, dk)
    f.cone((0, 0, 8), (0, 0, 110), 4.6, 3.4, M((84, 92, 110), spec=0.6, noise=0.05))
    for k, z in enumerate(range(20, 104, 16)):
        ring(f, (0, 0, z), 7, 10, 1.0, GLOW(90, 220, 255) if (k + fi) % 2 else M((120, 130, 150), spec=0.6), 'z')
    f.sph((0, 0, 118), 7.0, GLOW(160, 235, 255))
    f.cone((0, 0, 110), (0, 0, 114), 5, 3, dk)
    return f


def barrel(fi):
    f = Fig()
    g = M((74, 130, 70), spec=0.4, shin=22, noise=0.1)
    f.cyl((0, 0, 14), 9.5, 28, g)
    for z in (4, 14, 24):
        f.cone((0, 0, z - 0.5), (0, 0, z + 0.5), 9.9, 9.9, M((50, 90, 48), spec=0.4, noise=0.05), caps=False)
    f.cyl((0, 0, 28.2), 9.8, 1.0, M((60, 100, 56), spec=0.4))
    f.box((0, 9.5, 15), (4.0, 0.3, 4.0), M((250, 220, 40)))
    f.box((0, 9.8, 15), (0.9, 0.2, 2.4), BLACK)
    f.sph((0, 9.8, 12.8), 0.7, BLACK)
    if fi:
        f.sph((5, 9.4, 25), 1.2, GLOW(255, 80, 40))
    return f


def burning_barrel(fi):
    f = barrel(0)
    flame(f, (0, 0, 28), 16 + 2 * (fi % 2), 6, GLOW(255, 240, 120), GLOW(255, 120, 30), 6, 4 + fi)
    return f


def candle(fi):
    f = Fig()
    f.cyl((0, 0, 3), 1.8, 6, M((236, 226, 196), noise=0.1))
    f.cone((0, 0, 6), (0, 0, 8.5), 0.3, 0.3, BLACK)
    f.sph((0, 0, 9.6), 1.4, GLOW(255, 230, 120))
    f.cyl((0, 0, 0.3), 3.6, 0.6, M((200, 190, 160)))
    return f


def candelabra(fi):
    f = Fig()
    f.cyl((0, 0, 1), 6, 2, GOLD)
    f.cone((0, 0, 2), (0, 0, 22), 1.3, 1.0, GOLD)
    for s in (-1, 0, 1):
        x = s * 8
        if s:
            f.limb((0, 0, 20), (x, 0, 28), 0.8, 0.8, GOLD)
        f.cyl((x, 0, 31), 1.6, 5, M((236, 226, 196), noise=0.1))
        f.cone((x, 0, 33.5), (x, 0, 35.5), 0.3, 0.3, BLACK)
        f.sph((x, 0, 37), 1.5 + 0.2 * (fi % 2), GLOW(255, 230, 120))
        f.cyl((x, 0, 28.3), 2.4, 0.8, GOLD)
    return f


def torch(rgb, h):
    def b(fi):
        f = Fig()
        f.cyl((0, 0, 1.5), 6, 3, DSTONE)
        f.cone((0, 0, 3), (0, 0, h), 2.0, 1.5, M((100, 80, 60), noise=0.2))
        f.cone((0, 0, h - 1), (0, 0, h + 5), 2.8, 4.2, M((150, 120, 60), spec=0.6))
        flame(f, (0, 0, h + 4), 12 + (fi % 2) * 3, 4.4, GLOW(*[min(255, c + 80) for c in rgb]), GLOW(*rgb), 6, 7 + fi)
        return f
    return b


def lamp_short(fi):
    f = Fig()
    f.cyl((0, 0, 1), 5, 2, DSTEEL)
    f.cone((0, 0, 2), (0, 0, 28), 1.2, 1.0, M((90, 90, 100), spec=0.5))
    f.sph((0, 0, 32), 3.8 + 0.3 * (fi % 2), GLOW(255, 240, 190))
    return f


def evil_eye(fi):
    f = Fig()
    f.ell((0, 0, 20), (14, 14, 14), M((90, 30, 40), noise=0.15))
    f.ell((0, 8.5, 20), (9, 6, 9), M((240, 236, 226), spec=0.5, noise=0.05))
    f.sph((0, 13.0, 20), 4.2, M((220, 80, 30) if fi != 1 else (240, 200, 40), glow=0.5, noise=0.05))
    f.sph((0, 15.5, 20), 2.0, BLACK)
    return f


def stalagmite(h, r, n):
    def b(fi):
        f = Fig()
        rs = np.random.RandomState(h)
        for k in range(n):
            a = rs.uniform(0, 6.28)
            d = 0 if k == 0 else rs.uniform(r * 0.6, r * 1.3)
            hh = h * (1.0 if k == 0 else rs.uniform(0.35, 0.7))
            rr = r * (1.0 if k == 0 else rs.uniform(0.45, 0.7))
            f.cone((math.cos(a) * d, math.sin(a) * d, 0.2), (math.cos(a) * d * 0.9, math.sin(a) * d * 0.9, hh), rr, 0.4, M((126, 108, 90), noise=0.25))
        return f
    return b


def tree_dead(fi):
    f = Fig()
    bark = M((92, 74, 60), noise=0.25)
    f.cone((0, 0, 0), (0, 0, 40), 6.5, 3.2, bark)
    for (a, z0, ln, up) in ((0.5, 22, 22, 14), (2.6, 30, 20, 10), (4.4, 38, 16, 12), (1.6, 44, 12, 14), (3.4, 14, 14, 6)):
        p0 = np.array([0, 0, z0])
        p1 = p0 + np.array([math.cos(a) * ln, math.sin(a) * ln * 0.6, up])
        f.cone(p0, p1, 2.4, 0.8, bark)
        f.cone(p1, p1 + np.array([math.cos(a + 0.6) * 8, math.sin(a + 0.6) * 5, 8]), 0.9, 0.2, bark)
    f.cone((0, 0, 40), (1, 0, 66), 3.2, 0.5, bark)
    return f


def tree_big(fi):
    f = Fig()
    bark = M((100, 76, 54), noise=0.25)
    f.cone((0, 0, 0), (0, 0, 52), 9.0, 6.0, bark)
    rs = np.random.RandomState(3)
    for k in range(16):
        a = rs.uniform(0, 6.28)
        d = rs.uniform(0, 26)
        z = 64 + rs.uniform(-6, 30) - d * 0.3
        f.ell((math.cos(a) * d, math.sin(a) * d, z), (rs.uniform(14, 24), rs.uniform(14, 24), rs.uniform(10, 17)), M((40 + k % 3 * 10, 110 + k % 4 * 8, 50), noise=0.2))
    return f


def gore_corpse(variant, hanging=True):
    def b(fi):
        sp = dict(leg1=13, leg2=13, torso_len=19, torso_w=7, torso_d=4.6, sh_w=8.5, arm1=9, arm2=9, head_r=4.4,
                  pants=M((60, 56, 50), noise=0.1), boots=M((40, 36, 34)), cloth=M((130, 40, 40), noise=0.2), sleeve=M((120, 36, 36), noise=0.2),
                  glove=M((190, 120, 100)), skin=M((190, 130, 110), noise=0.1), leg_r=3.2, arm_r=2.5)
        sw = 6 * math.sin(fi * 2.1)
        ps = pose(shL=170, shR=170, abL=12, abR=12, elL=0, elR=0, hipL=3 + sw * 0.4, hipR=-3, kneeL=6, kneeR=6, hd=30, lean=2)
        if variant == 2:
            ps.update(shL=110, shR=60, abL=70, abR=70, hd=20, hipL=8, hipR=-6)
        if variant == 3:
            ps.update(hipR=20, kneeR=70, shL=130, shR=140)
        if variant == 4:
            ps.update(shL=150, shR=150)
        f, J = biped(sp, ps)
        if variant == 4:       # headless
            f.prims = [p for p in f.prims if not (np.allclose(p['c'], J['head'], atol=1e-6))]
            f.limb(J['neck'] - np.array([0, 0, 1]), J['neck'] + np.array([0, 0, 1.5]), 2.0, 1.2, BLOOD)
        if variant == 5:       # torso only
            f.prims = [p for p in f.prims if p['c'][2] > J['pelvis'][2] - 1]
            f.limb(J['pelvis'], J['pelvis'] + np.array([0, 0, -6]), 2.5, 1.2, BLOOD)
        # blood
        for (x, z) in ((2, 20), (-3, 10), (1, 30)):
            f.ell((x, 4.8, z), (1.2, 0.4, 3.0), BLOOD)
        g = lift(f, 0.0, centre=False)
        top = max(p['c'][2] + Fig._radius(p) for p in g.prims)
        wrist = J['handL'][2]
        g.cone((0, 0, wrist + 4), (0, 0, top + 14), 0.7, 0.7, DSTEEL)
        g.sph((0, 0, top + 14.5), 1.2, DSTEEL)
        return g
    return b


def skull_pile(fi):
    f = Fig()
    rs = np.random.RandomState(9)
    for k in range(14):
        a = rs.uniform(0, 6.28)
        d = rs.uniform(0, 9) * (1 if k < 9 else 0.4)
        z = 2.4 + (0 if k < 9 else rs.uniform(4, 7))
        f.ell((math.cos(a) * d, math.sin(a) * d, z), (2.6, 2.8, 2.6), BONE)
        f.sph((math.cos(a) * d - 0.9, math.sin(a) * d + 2.2, z + 0.4), 0.7, M((30, 20, 20)))
        f.sph((math.cos(a) * d + 0.9, math.sin(a) * d + 2.2, z + 0.4), 0.7, M((30, 20, 20)))
    for (x, y) in ((-6, 3), (6, -2), (0, 8)):
        f.cyl((x, y, 9), 1.6, 5, M((236, 226, 196), noise=0.1))
        f.sph((x, y, 12.6), 1.3 + 0.2 * (fi % 2), GLOW(255, 230, 120))
    return f


def skull_pole(fi):
    f = Fig()
    f.cone((0, 0, 0), (0, 0, 52), 1.5, 1.0, WOODM)
    for k, z in enumerate((44, 34, 25, 17, 9)):
        f.ell((0, 0, z), (4.0, 4.2, 3.9), BONE)
        f.sph((-1.4, 3.2, z + 0.5), 0.9, M((30, 20, 20)))
        f.sph((1.4, 3.2, z + 0.5), 0.9, M((30, 20, 20)))
    return f


def impaled(fi):
    f = Fig()
    f.cone((0, 0, 0), (0, 0, 40), 2.4, 1.2, WOODM)
    body = M((140, 50, 50), noise=0.2)
    f.ell((0, 0, 30), (6, 4, 9), body)
    f.sph((0, 0, 41), 4.2, M((190, 130, 110), noise=0.1))
    f.cone((0, 0, 40), (0, 0, 66), 1.4, 0.2, WOODM)
    for s in (-1, 1):
        f.limb((s * 6, 0, 36), (s * 12, 2, 24 + 4 * math.sin(fi)), 2.0, 1.6, body)
    f.limb((-2, 0, 22), (-3, 0, 6), 2.4, 2.0, M((90, 70, 60)))
    f.limb((2, 0, 22), (3, 2, 8), 2.4, 2.0, M((90, 70, 60)))
    return f


def blood_pool(w):
    def b(fi):
        f = Fig()
        f.ell((0, 0, 0.3), (w, w * 0.62, 0.7), BLOOD)
        f.ell((w * 0.2, -2, 0.7), (w * 0.4, w * 0.3, 0.5), BLOOD2)
        return f
    return b


def gore_pile(fi):
    f = Fig()
    f.ell((0, 0, 0.3), (24, 8, 0.7), BLOOD)
    rs = np.random.RandomState(5)
    for k in range(10):
        f.ell((rs.uniform(-16, 16), rs.uniform(-4, 4), 1.5), (rs.uniform(1.5, 3.5),) * 2 + (1.5,), M((190, 100, 100), noise=0.2))
    f.cone((-6, 0, 1), (6, 2, 3), 0.8, 0.8, BONE)
    return f


def small_skull(fi):
    f = Fig()
    f.ell((0, 0, 5), (5, 5.2, 5), BONE)
    return f


def torso_stand(fi):
    return Fig()


# ---------------------------------------------------------------------------------------------- the 0.10.13 items
GLASS_RED = M((220, 40, 56), spec=0.9, shin=40, noise=0.02, glow=0.35)
CORK = M((150, 104, 60), noise=0.2)


def flask(liquid, r, h, glow, fi, neck=1.8):
    """a round potion flask with a cork; liquid is the (r, g, b) of the glass"""
    f = Fig()
    glass = M(liquid, spec=0.9, shin=40, noise=0.02, glow=0.3)
    f.ell((0, 0, r), (r, r, r * 0.95), glass)
    f.cone((0, 0, r * 1.7), (0, 0, r * 1.7 + h), neck + 0.5, neck, glass)
    f.cone((0, 0, r * 1.7 + h), (0, 0, r * 1.7 + h + 2.6), neck + 0.2, neck + 0.4, CORK)
    f.ell((0, 0, r * 0.8), (r * 0.8, r * 0.8, r * 0.7), GLOW(*(glow if fi % 2 == 0 else tuple(min(255, c + 40) for c in glow))))
    f.ell((-r * 0.4, r * 0.86, r * 1.3), (1.0, 0.5, 1.8), M((255, 255, 255), glow=1, flat=True))
    return f


def health_potion(fi):
    return flask((230, 50, 60), 5.6, 3.5, (255, 120, 130), fi)


def health_vial(fi):
    """a tall thin vial with a little red liquid, smaller than the potion"""
    f = Fig()
    glass = M((235, 245, 250), spec=0.9, shin=40, noise=0.02, glow=0.1)
    f.cone((0, 0, 1.0), (0, 0, 11.0), 2.2, 2.0, glass)
    f.cone((0, 0, 1.0), (0, 0, 7.0), 2.35, 2.35, GLASS_RED)
    f.cone((0, 0, 11.0), (0, 0, 13.4), 1.8, 1.9, CORK)
    f.ell((-0.7, 1.9, 8), (0.35, 0.3, 1.8), M((255, 255, 255), glow=1, flat=True))
    return lift(f, 0.5)


def shield_potion(fi):
    f = flask((60, 110, 255), 6.2, 3.6, (150, 190, 255), fi)
    # a gold shield on the front of the flask
    gold = GOLD
    f.box((0, 6.0, 6.4), (2.6, 0.5, 3.0), gold)
    f.cone((0, 6.1, 3.5), (0, 6.1, 1.6), 2.6, 0.2, gold)
    return f


def shield_charge(fi):
    """a modern power cell: a yellow-black striped canister with a glowing tip"""
    f = Fig()
    y = M((240, 200, 40), spec=0.5, shin=26, noise=0.05)
    k = M((36, 36, 40), noise=0.06)
    f.cone((0, 0, 1.0), (0, 0, 11.0), 3.6, 3.6, y)
    for z in (3.0, 6.0, 9.0):
        f.cone((0, 0, z), (0, 0, z + 1.1), 3.7, 3.7, k)
    f.cone((0, 0, 11.0), (0, 0, 12.4), 2.4, 2.4, M((120, 124, 134), spec=0.6))
    f.sph((0, 0, 13.6), 1.7, GLOW(255, 235, 110) if fi % 2 == 0 else GLOW(255, 250, 190))
    f.cone((0, 0, 0.2), (0, 0, 1.0), 4.0, 3.8, k)
    return lift(f, 0.5)


def vest_modern(fi):
    return vest(True)(fi)


def crystal_spell(core, halo, ring_rgb=None, fi=0, twin=None):
    """a floating spell crystal with a halo ring of sparks"""
    f = Fig()
    c = GLOW(*core)
    f.cone((0, 0, 12), (0, 0, 21), 5.5, 0.2, c)
    f.cone((0, 0, 12), (0, 0, 3), 5.5, 0.2, c)
    f.sph((0, 0, 12), 3.4, GLOW(*[min(255, v + 40) for v in core]))
    ring(f, (0, 0, 12), 8.5, 10, 0.9, GLOW(*(ring_rgb or halo)), phase=fi * 0.3)
    if twin:
        f.cone((0, 0, 12), (0, 0, 17), 3.0, 0.2, GLOW(*twin))
        ring(f, (0, 0, 12), 6.0, 8, 0.7, GLOW(*twin), phase=-fi * 0.3)
    return f


def health_spell(fi):
    return crystal_spell((255, 70, 90), (255, 150, 160), fi=fi)


def life_spell(fi):
    return crystal_spell((255, 70, 90), (255, 220, 120), ring_rgb=(255, 220, 120), fi=fi, twin=(90, 150, 255))


def immortal_spell(fi):
    """a golden sphere inside two crossed halos"""
    f = Fig()
    f.sph((0, 0, 12), 5.5, GLOW(255, 225, 120))
    f.sph((0, 0, 12), 3.0, GLOW(255, 255, 230))
    ring(f, (0, 0, 12), 9.0, 12, 0.9, GLOW(255, 240, 170), axis='x', phase=fi * 0.3)
    ring(f, (0, 0, 12), 9.0, 12, 0.9, GLOW(170, 210, 255), axis='y', phase=-fi * 0.3)
    return f


def cartography_spell(fi):
    """a rolled map scroll with a blue glow above it"""
    f = Fig()
    paper = M((226, 206, 150), noise=0.14)
    f.cone((-7.5, 0, 6.0), (7.5, 0, 6.0), 4.0, 4.0, paper)
    for sx in (-1, 1):
        f.cone((sx * 7.5, 0, 6.0), (sx * 9.2, 0, 6.0), 4.3, 4.3, M((120, 80, 44), noise=0.2))
    f.cone((-2.5, 0, 6.0), (-2.2, 0, 6.0), 4.2, 4.2, M((176, 40, 40)))
    f.cone((2.5, 0, 6.0), (2.8, 0, 6.0), 4.2, 4.2, M((176, 40, 40)))
    f.sph((0, 0, 14.5), 2.2, GLOW(110, 190, 255) if fi % 2 == 0 else GLOW(170, 220, 255))
    ring(f, (0, 0, 14.5), 4.2, 8, 0.6, GLOW(110, 190, 255), phase=fi * 0.4)
    return f


def adrenaline(fi):
    """a modern auto-injector syringe lying on its side: purple fluid, plunger, needle"""
    f = Fig()
    glass = M((225, 235, 245), spec=0.9, shin=40, noise=0.02, glow=0.1)
    f.cone((-8, 0, 3.6), (6, 0, 3.6), 3.0, 3.0, glass)
    f.cone((-6, 0, 3.6), (5, 0, 3.6), 2.5, 2.5, GLOW(170, 60, 255) if fi % 2 == 0 else GLOW(200, 100, 255))
    f.cone((-8, 0, 3.6), (-12, 0, 3.6), 1.0, 1.0, M((120, 124, 134), spec=0.6))
    f.cone((-12, 0, 3.6), (-12.8, 0, 3.6), 3.4, 3.4, M((60, 60, 66)))
    f.cone((6, 0, 3.6), (8, 0, 3.6), 1.4, 0.9, M((120, 124, 134), spec=0.6))
    f.cone((8, 0, 3.6), (15, 0, 3.6), 0.45, 0.1, M((200, 204, 212), spec=0.8))
    f.box((-1, 3.0, 3.6), (4, 0.2, 1.4), M((40, 40, 48)))
    return lift(f, 0.2)


def boots(fi):
    """a pair of heat resistant boots: grey with an orange sole and a flame mark"""
    f = Fig()
    body = M((96, 98, 108), spec=0.3, noise=0.08)
    sole = M((238, 130, 36), noise=0.08)
    for sx in (-1, 1):
        x = sx * 5.2
        f.cone((x, -2.0, 1.4), (x, -2.0, 15), 3.2, 3.5, body)             # shaft
        f.box((x, 3.2, 3.2), (3.6, 6.0, 2.4), body)                        # foot
        f.ell((x, 8.2, 3.2), (3.4, 2.6, 2.4), body)
        f.box((x, 3.6, 0.7), (3.9, 7.4, 0.8), sole)
        f.cone((x, -2.0, 14.4), (x, -2.0, 16.0), 3.7, 3.7, M((60, 60, 66)))
        f.cone((x, 1.2, 8.4), (x, 4.4, 6.4), 0.1, 1.0, GLOW(255, 150, 40)) if False else None
        f.cone((x, 3.4, 5.9), (x, 3.4, 8.8), 1.6, 0.1, GLOW(255, 170, 50) if fi % 2 == 0 else GLOW(255, 210, 90))
    return lift(f, 0.2)


PROPS = {}


def _reg(fam, builder, **opt):
    PROPS[fam] = (builder, opt)


# weapon pickups: each turns 45 degrees per frame (A-H)
def tome(cover, gem):
    """a spell tome: the bare-hand spells (fireball, storm, spirit, hellfire) are picked up as books with a glowing gem"""
    def b(fi):
        f = Fig()
        cov = M(cover, spec=0.3, shin=20, noise=0.1)
        page = M((236, 226, 196), noise=0.08)
        gold = M((226, 180, 70), spec=0.6, shin=30, noise=0.03)
        f.box((0, 0, 1.6), (7.0, 9.0, 1.6), cov)                          # cover
        f.box((0.7, 0, 3.4), (6.0, 8.2, 0.9), page)                       # pages
        f.box((0, 0, 4.7), (7.0, 9.0, 0.9), cov)                          # top cover
        f.box((-6.6, 0, 3.0), (0.8, 9.0, 2.8), gold)                      # spine
        for sy in (-1, 1):
            f.box((0, sy * 8.3, 5.2), (7.0, 0.5, 0.3), gold)
        f.sph((0, 0, 6.6), 2.0, GLOW(*gem))
        f.sph((0, 0, 6.6), 3.0, GLOW(*[int(c * 0.6) for c in gem])) if False else None
        for k in range(4):
            a = k * math.pi / 2 + 0.4
            f.cone((math.cos(a) * 1.5, math.sin(a) * 1.5, 6.6), (math.cos(a) * 3.8, math.sin(a) * 3.8, 8.6), 0.7, 0.05, GLOW(*gem))
        return f
    return b


for fam, name, s in (('SHOT', 'tripleshotgun', 1.15), ('MGUN', 'chaingun', 1.25), ('CSAW', 'chainsaw', 1.25), ('SGN2', 'alienlauncher', 1.2)):
    _reg(fam, gun_pickup(name, s), spin=45, facing=90, elev=26)
for fam, cover, gem in (('LAUN', (150, 40, 24), (255, 150, 40)), ('PLAS', (40, 70, 140), (120, 220, 255)),
                        ('BFUG', (150, 150, 176), (210, 230, 255))):
    _reg(fam, tome(cover, gem), spin=45, elev=30)
for fam, rgb in KEYCOL.items():
    _reg(fam, (lambda rgb, dark: (lambda fi: key_model(rgb, dark)))(rgb, fam == 'KKEY'), spin=45, elev=16)
for fam, rgb in (('BSKU', (60, 100, 255)), ('YSKU', (255, 230, 60)), ('RSKU', (255, 60, 60))):
    _reg(fam, skull_key(rgb), elev=14)
_reg('STIM', health_potion, spin=22, elev=12)
_reg('BON1', health_vial, spin=22, elev=12)
_reg('BON2', shield_charge, spin=22, elev=14)
_reg('ARM1', vest_modern, elev=16)
_reg('ARM2', shield_potion, spin=22, elev=12)
_reg('PSTR', adrenaline, spin=22, elev=24)
_reg('PMAP', cartography_spell, spin=22, elev=18)
_reg('SOUL', health_spell, spin=22, elev=14)
_reg('MEGA', life_spell, spin=22, elev=14)
_reg('PINV', immortal_spell, spin=0, elev=14)
_reg('PVIS', goggles, elev=16)
_reg('SUIT', boots, spin=22, elev=16)
_reg('CLIP', clip, elev=20)
_reg('AMMO', bullet_box, elev=24)
_reg('SHEL', shells, elev=24)
_reg('SBOX', shell_box, elev=24)
_reg('CELL', cell, elev=16)
_reg('CELP', cell_pack, elev=20)
_reg('ROCK', rocket_ammo, elev=14)
_reg('BROK', rocket_box, elev=24)
_reg('BPAK', backpack, elev=14)
_reg('COL1', lambda fi: column(56, 8, STONE, DSTONE), elev=10)
_reg('COL2', lambda fi: column(40, 8, STONE, DSTONE), elev=10)
_reg('COL3', lambda fi: column(54, 8, MARBLE, STONE), elev=10)
_reg('COL4', lambda fi: column(36, 8, MARBLE, STONE), elev=10)
_reg('COL5', col_heart, elev=10)
_reg('COL6', col_skull, elev=10)
_reg('COLU', floor_lamp, elev=8)
_reg('ELEC', tech_pylon, elev=8)
_reg('BAR1', barrel, elev=14)
_reg('FCAN', burning_barrel, elev=14)
_reg('CAND', candle, elev=12)
_reg('CBRA', candelabra, elev=12)
_reg('TBLU', torch((70, 130, 255), 52), elev=8)
_reg('TGRN', torch((80, 255, 110), 56), elev=8)
_reg('TRED', torch((255, 80, 60), 52), elev=8)
_reg('SMBT', torch((70, 130, 255), 32), elev=8)
_reg('SMGT', torch((80, 255, 110), 34), elev=8)
_reg('SMRT', torch((255, 80, 60), 32), elev=8)
_reg('TLMP', floor_lamp, elev=8)
_reg('TLP2', lamp_short, elev=8)
_reg('CEYE', evil_eye, elev=8)
_reg('SMT2', stalagmite(36, 8, 4), elev=10)
_reg('SMIT', stalagmite(52, 12, 7), elev=10)
_reg('TRE1', tree_dead, elev=10)
_reg('TRE2', tree_big, elev=10)
for i, fam in enumerate(('GOR1', 'GOR2', 'GOR3', 'GOR4', 'GOR5')):
    _reg(fam, gore_corpse(i + 1), elev=6)
for i, fam in enumerate(('HDB1', 'HDB2', 'HDB3', 'HDB4', 'HDB5', 'HDB6')):
    _reg(fam, gore_corpse(1 + i % 5), elev=6)
_reg('POL1', impaled, elev=8)
_reg('POL2', skull_pole, elev=8)
_reg('POL3', skull_pile, elev=14)
_reg('POL4', tree_dead, elev=10)
_reg('POL5', gore_pile, elev=30)
_reg('POL6', impaled, elev=8)
_reg('POB1', blood_pool(24), elev=40)
_reg('POB2', blood_pool(16), elev=40)
_reg('KEEN', gore_corpse(2), elev=6)


def flaming_skull(fi):
    f = Fig()
    f.cyl((0, 0, 1.5), 7, 3.0, M((70, 66, 66), spec=0.4, noise=0.2))
    f.ell((0, 0, 10), (5.2, 5.4, 5.6), BONE)
    f.ell((0, 3.6, 7.0), (3.3, 2.6, 2.8), BONE)
    f.sph((-2.0, 4.6, 10.6), 1.3, GLOW(255, 60, 40))
    f.sph((2.0, 4.6, 10.6), 1.3, GLOW(255, 60, 40))
    flame(f, (0, -1, 14), 10 + 3 * (fi % 2), 4.2, GLOW(255, 240, 120), GLOW(255, 110, 30), 5, 3 + fi)
    return f


_reg('FSKU', flaming_skull, elev=10)
_reg('PIST', gun_pickup('revolver', 2.0), facing=90, elev=26)
_reg('BRS1', gore_pile, elev=30)
