"""art_monsters.py - the monster roster drawn from primitives.  Each family gives figure(frame_index) -> Fig."""
import math
import numpy as np
from art_render import Fig, mat, rot_matrix, frame_along
from art_rig import biped, pose, walk_pose, apply_fall, ease, gibs, blob_pool, clamp01

M = mat
BLOOD = M((150, 20, 24), spec=0.3, noise=0.1)
BLOOD2 = M((200, 60, 60), spec=0.3, noise=0.1)
FLESH = M((190, 110, 100), noise=0.1)
BONE = M((225, 215, 190), noise=0.05)
STEEL = M((130, 136, 150), spec=0.5, shin=30, noise=0.03)
DSTEEL = M((70, 74, 86), spec=0.4, shin=24, noise=0.03)
BLACK = M((24, 24, 28), spec=0.2, noise=0.02)


def letter_cat(layout, idx):
    """layout string like 'WWWWAAAPDDDXXX' -> (category, index within the category, count of that category)"""
    c = layout[min(idx, len(layout) - 1)]
    n = layout.count(c)
    first = layout.index(c)
    return c, idx - first, n


def death_pose(p, back=1.0, arms=1.0, base=None):
    """p = 0..1 how far down.  back = +1 falls backwards, -1 forwards"""
    q = ease(p)
    d = pose(fall=back * 88 * q, shL=30 + 100 * q * arms, shR=30 + 100 * q * arms, abL=20 + 60 * q, abR=20 + 60 * q,
             elL=20 + 30 * q, elR=20 + 30 * q, hipL=18 * q * (1 - q) * 4, hipR=-8 * q * (1 - q) * 4, kneeL=40 * q * (1 - q) * 4,
             kneeR=30 * q * (1 - q) * 4, hd=-10 * q if back > 0 else 10 * q, lean=0)
    if base:
        d.update(base)
    return d


def pain_pose(**kw):
    d = pose(lean=-16, shL=60, shR=50, abL=45, abR=45, elL=40, elR=40, hd=24, hipL=-10, hipR=8, kneeL=20, kneeR=6)
    d.update(kw)
    return d


class Humanoid:
    layout = 'WWWWAAAPDDDDD'
    sp = {}
    height = 56

    def sp_(self):
        return self.sp

    def pose_for(self, cat, i, n):
        if cat == 'W':
            return self.walk(i)
        if cat == 'A':
            return self.attack(i, n)
        if cat == 'P':
            return self.pain(i, n)
        if cat == 'D':
            return self.death(i, n)
        return self.xdeath(i, n)

    def walk(self, i):
        return walk_pose(i)

    def attack(self, i, n):
        return pose()

    def pain(self, i, n):
        return pain_pose()

    def death(self, i, n):
        return death_pose((i + 1) / n)

    def xdeath(self, i, n):
        return death_pose((i + 1) / n, back=-1)

    def decorate(self, f, J, ps, cat, i, n):
        pass

    def figure(self, idx):
        cat, i, n = letter_cat(self.layout, idx)
        ps = self.pose_for(cat, i, n)
        f, J = biped(self.sp, ps)
        self.decorate(f, J, ps, cat, i, n)
        if cat in 'DX' and ps.get('fall') is not None:
            f = apply_fall(f, ps['fall'], ps.get('fall_roll', 0), pivot=(0, 0, 0))
            # centre the lying body on the origin
            ys = [p['c'][1] for p in f.prims]
            cy = (min(ys) + max(ys)) / 2
            f = f.transformed(np.eye(3), t=(0, -cy, 0))
            if cat == 'X':
                p = (i + 1) / n
                gibs(f, p, 28, [BLOOD, BLOOD2, FLESH], rng_seed=hash(type(self).__name__) % 97)
            elif i + 1 == n:
                blob_pool(f, (0, 0, 0.2), 12, BLOOD, 0.25)
        return f


def hand_axis(J, tR, tL):
    """grip point and the unit direction of a two-handed weapon from the two hands"""
    a, b = J['handR'], J['handL']
    d = b - a
    n = np.linalg.norm(d)
    return a, (d / n if n > 1e-6 else np.array([0, 1.0, 0]))


class RocketTrooper(Humanoid):
    layout = 'WWWWAAPDDDDDXXXXXXXXX'
    ARM = M((92, 104, 74), noise=0.08)
    sp = dict(leg1=13, leg2=13, torso_len=19, torso_w=7.6, torso_d=5, torso_kind='box', sh_w=9, arm1=9, arm2=9, head_r=4.6,
              pants=M((66, 72, 54), noise=0.08), boots=M((44, 40, 36), spec=0.2), cloth=ARM, sleeve=M((80, 92, 64), noise=0.08),
              glove=M((52, 48, 44)), skin=M((196, 146, 112), noise=0.05), leg_r=3.4, arm_r=2.7)

    def tubepose(self, lo=0.0, **kw):
        return dict(tgtR=(-3.5, 8, 14.5 + lo), tgtL=(3.0, 20, 15.5 + lo), **kw)

    def walk(self, i):
        d = walk_pose(i, amp=24, arm=0)
        d.update(self.tubepose(lo=1.0 * (i % 2)))
        return d

    def attack(self, i, n):
        d = pose(**self.tubepose(lean=-4, lo=0.0, hd=0))
        d['hipL'], d['hipR'], d['kneeL'], d['kneeR'] = 14, -10, 20, 8
        if i == 1:
            d['lean'] = -9
            d['tgtR'] = (-3.5, 6, 15)
        return d

    def pain(self, i, n):
        return pain_pose(tgtR=(-6, 5, 17), tgtL=(5, 9, 22))

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        # helmet
        helm = M((98, 108, 82), spec=0.25, shin=18, noise=0.06)
        f.ell(H([0, -0.2, 1.1]), [5.3, 5.5, 4.6], helm, rot=Rh)
        f.box(H([0, 4.6, 0.9]), [4.2, 0.7, 1.0], helm, rot=Rh)           # brim
        f.box(H([0, 4.3, -0.5]), [3.3, 0.55, 1.15], M((255, 150, 40), glow=0.95, noise=0), rot=Rh)    # visor slit
        f.box(H([0, 2.5, -3.6]), [2.6, 2.4, 1.2], M((50, 52, 46)), rot=Rh)    # mask / respirator
        # chest rig and pouches
        for sx in (-3.3, 0, 3.3):
            f.box(T([sx, 5.6, 10.5]), [1.8, 1.1, 2.5], M((60, 64, 48)), rot=Rt)
        f.box(T([0, 0, 17.5]), [8.4, 5.2, 1.2], M((62, 70, 52)), rot=Rt)              # shoulder yoke
        f.ell(T([8.5, 0, 17.5]), [3.0, 3.2, 2.4], M((110, 118, 90), spec=0.2), rot=Rt)
        f.ell(T([-8.5, 0, 17.5]), [3.0, 3.2, 2.4], M((110, 118, 90), spec=0.2), rot=Rt)
        # backpack with two rocket tips
        f.box(T([0, -6.8, 11]), [5.2, 2.4, 6.5], M((70, 76, 58)), rot=Rt)
        for sx in (-2.4, 2.4):
            f.cone(T([sx, -6.8, 14]), T([sx, -6.8, 24]), 1.3, 0.2, M((190, 60, 40), spec=0.3), rot=None) if False else \
                f.cone(T([sx, -6.8, 14]), T([sx, -6.8, 23.5]), 1.5, 0.3, M((196, 62, 40), spec=0.3))
        # the launcher
        a, ax = hand_axis(J, None, None)
        base = a - ax * 7
        tip = a + ax * 20
        f.cone(base, tip, 2.5, 2.5, M((78, 80, 86), spec=0.4), caps=True)
        f.cone(tip - ax * 0.2, tip + ax * 3.0, 3.1, 3.1, M((255, 140, 40), spec=0.3))
        f.cone(base - ax * 0.2, base - ax * 2.5, 2.2, 3.4, M((60, 62, 68)))
        f.box(a + np.array([0, 0, 3.0]), [0.7, 2.5, 1.2], M((50, 50, 56)), rot=Rt)
        if cat == 'A' and i == 1:
            f.sph(tip + ax * 6, 4.0, M((255, 220, 120), glow=1.0, noise=0, flat=True))


class ShockTrooper(Humanoid):
    layout = 'WWWWAAAPPDDDDDXXXXXXXX'
    sp = dict(leg1=13, leg2=13, torso_len=19, torso_w=7.0, torso_d=4.6, sh_w=8.6, arm1=9, arm2=9, head_r=4.5,
              pants=M((34, 38, 62), noise=0.06), boots=M((24, 24, 30), spec=0.3), cloth=M((42, 48, 82), noise=0.07),
              sleeve=M((38, 44, 76), noise=0.07), glove=M((26, 26, 34)), skin=M((210, 170, 140)), leg_r=3.2, arm_r=2.6)

    def rifle(self, lo=0.0, fwd=0.0, **kw):
        return dict(tgtR=(-3.2, 7 + fwd, 14 + lo), tgtL=(2.8, 19 + fwd, 15 + lo), **kw)

    def walk(self, i):
        d = walk_pose(i, amp=26, arm=0)
        d.update(self.rifle(lo=-1.5 + 0.8 * (i % 2)))
        return d

    def attack(self, i, n):
        d = pose(**self.rifle(lo=1.0, fwd=0, lean=-3))
        d.update(hipL=16, hipR=-12, kneeL=14, kneeR=10)
        if i == 2:
            d.update(lean=-8, tgtR=(-3.2, 5, 15.5), tgtL=(2.8, 17, 16.5))
        return d

    def pain(self, i, n):
        return pain_pose(tgtR=(-7, 4, 18), tgtL=(7, 7, 23))

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        hood = M((26, 28, 46), noise=0.05)
        f.ell(H([0, -0.4, 0.6]), [5.1, 5.3, 5.2], hood, rot=Rh)               # hood
        f.ell(H([0, 2.8, -0.2]), [3.5, 2.3, 3.2], M((210, 170, 140)), rot=Rh)   # face
        f.box(H([0, 4.4, 0.5]), [3.6, 0.5, 0.9], M((90, 210, 255), glow=1.0, noise=0, flat=True), rot=Rh)   # goggle strip
        f.cone(H([0, 3, 0.6]), H([0, 5.2, 0.6]), 0.8, 0.8, M((40, 40, 50)))
        # shoulder plates and chest armour
        for s in (-1, 1):
            f.ell(T([s * 8.6, 0, 18]), [3.8, 3.8, 2.8], M((70, 78, 120), spec=0.45), rot=Rt)
        f.box(T([0, 4.6, 12]), [4.8, 0.9, 5.2], M((60, 68, 108), spec=0.4), rot=Rt)
        f.box(T([0, 4.9, 12]), [1.0, 0.5, 4.2], M((90, 210, 255), glow=1.0, noise=0, flat=True), rot=Rt)
        f.box(T([0, 0, 0.8]), [7.2, 4.8, 1.0], M((90, 70, 40), spec=0.3), rot=Rt)    # belt
        # the shock rifle
        a, ax = hand_axis(J, None, None)
        base = a - ax * 6
        tip = a + ax * 18
        f.cone(base, a + ax * 9, 2.0, 2.2, M((96, 100, 120), spec=0.5))
        f.cone(a + ax * 9, tip, 1.0, 0.8, M((60, 62, 74), spec=0.5))
        for k in (11, 14, 17):
            f.cone(a + ax * (k - 0.6), a + ax * (k + 0.6), 2.1, 2.1, M((90, 200, 255), glow=0.9, noise=0, flat=True))
        f.box(base + np.array([0, 0, -2.4]), [0.8, 1.0, 2.6], M((40, 40, 50)), rot=Rt)
        f.box(a - ax * 3 + np.array([0, 0, 2.6]), [0.6, 3.5, 0.9], M((60, 62, 74)), rot=Rt)
        if cat == 'A' and i == 2:
            f.sph(tip + ax * 3, 3.2, M((170, 230, 255), glow=1.0, noise=0, flat=True))


MONSTERS = {'RTRP': RocketTrooper(), 'STRP': ShockTrooper()}
