"""art_monsters2.py - the rest of the roster: beasts, casters, floaters and machines"""
import math
import numpy as np
from art_render import Fig, mat, rot_matrix, frame_along
from art_rig import biped, pose, walk_pose, apply_fall, ease, gibs, blob_pool, clamp01
from art_monsters import (Humanoid, M, BLOOD, BLOOD2, FLESH, BONE, STEEL, DSTEEL, BLACK, letter_cat, death_pose, pain_pose,
                          hand_axis, MONSTERS)

GLOW = lambda r, g, b: M((r, g, b), glow=1.0, noise=0, flat=True)


def ring(f, c, radius, n, r, m, axis='z', phase=0.0):
    for k in range(n):
        a = phase + 2 * math.pi * k / n
        c0, s0 = math.cos(a) * radius, math.sin(a) * radius
        off = {'z': (c0, s0, 0), 'x': (0, c0, s0), 'y': (c0, 0, s0)}[axis]
        f.sph(np.asarray(c, float) + np.array(off), r, m)


def flame(f, c, h, r, inner, outer, n=5, seed=1, sway=0.0):
    """a small tuft of flames: n cones fanning upward"""
    rs = np.random.RandomState(seed)
    c = np.asarray(c, float)
    for k in range(n):
        a = 2 * math.pi * k / n + rs.uniform(-0.4, 0.4)
        d = rs.uniform(0.45, 1.0)
        base = c + np.array([math.cos(a) * r * 0.6, math.sin(a) * r * 0.6, 0])
        tip = base + np.array([math.cos(a) * r * 0.4 + sway, math.sin(a) * r * 0.4, h * d])
        f.cone(base, tip, r * 0.55, 0.05, outer)
        f.cone(base + np.array([0, 0, 0.3]), base + (tip - base) * 0.65, r * 0.32, 0.05, inner)


def robe(f, z0, z1, r0, r1, m, trim=None, trim_h=2.0, sway=0.0, tilt=(0, 0)):
    top = np.array([sway * 0.5, 0, z1]) + np.array([tilt[0], tilt[1], 0])
    f.cone(np.array([sway, 0, z0]), top, r0, r1, m, caps=False)
    if trim is not None:
        f.cone(np.array([sway, 0, z0]), np.array([sway, 0, z0 + trim_h]), r0 * 1.03, r0 * 0.99, trim, caps=False)
    f.ell(np.array([sway, 0, z0 + 0.5]), [r0 * 0.99, r0 * 0.99, 0.8], m)


class Werewolf(Humanoid):
    layout = 'WWWWAAAPDDDDDD'
    FUR = M((110, 78, 52), noise=0.22)
    FUR2 = M((70, 48, 34), noise=0.22)
    sp = dict(leg1=14, leg2=13, torso_len=19, torso_w=9.5, torso_d=7, sh_w=11, arm1=13, arm2=13, head_r=5.5, neck=0.5,
              pants=M((98, 68, 46), noise=0.22), boots=M((60, 42, 30), noise=0.15), cloth=M((112, 80, 54), noise=0.22),
              sleeve=M((104, 72, 48), noise=0.22), glove=M((70, 48, 34), noise=0.2), skin=M((120, 86, 58), noise=0.22),
              leg_r=4.4, arm_r=3.6, foot_len=7, hip_w=4.5)

    def walk(self, i):
        d = walk_pose(i, amp=30, arm=36, lean=26, bob=1.5)
        d.update(shL=d['shL'] + 40, shR=d['shR'] + 40, elL=30, elR=30)
        return d

    def attack(self, i, n):
        if i == 0:      # crouch
            return pose(lean=40, drop=8, hipL=45, hipR=45, kneeL=80, kneeR=80, shL=-20, shR=-20, abL=25, abR=25, elL=20, elR=20, hd=-25)
        if i == 1:      # lunge
            return pose(lean=62, drop=3, lift=0, hipL=-20, hipR=-5, kneeL=10, kneeR=30, shL=135, shR=135, abL=14, abR=14, elL=10, elR=10, hd=-45)
        return pose(lean=34, hipL=22, hipR=-14, kneeL=40, kneeR=20, shL=105, shR=40, abL=10, abR=55, elL=20, elR=30, hd=-25)

    def pain(self, i, n):
        return pain_pose(lean=-5, hd=20)

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        T = lambda v: pel + Rt @ np.array(v, float)
        fur = self.FUR
        # wolf head: snout, jaw, ears, eyes, teeth
        f.ell(H([0, 4.5, -1.2]), [2.8, 4.6, 2.5], fur, rot=Rh)
        f.ell(H([0, 8.0, -1.4]), [1.7, 1.6, 1.5], BLACK, rot=Rh)                 # nose
        open_ = 6 if cat == 'A' and i >= 1 else 1.5
        f.ell(H([0, 4.4, -3.4 - open_ * 0.3]), [2.2, 4.0, 1.2], self.FUR2, rot=Rh)    # jaw
        for s in (-1, 1):
            f.cone(H([s * 3.2, -0.5, 4.5]), H([s * 4.4, -1.5, 10.0]), 1.8, 0.1, fur)    # ears
            f.sph(H([s * 2.2, 3.0, 1.4]), 1.1, GLOW(255, 210, 40))                    # eyes
            for k in range(3):
                f.cone(H([s * 1.4, 3.0 + k * 1.6, -2.4]), H([s * 1.4, 3.0 + k * 1.6, -2.4 - 1.1 - open_ * 0.4]), 0.45, 0.05, BONE)
        # mane and shoulder hair
        f.ell(T([0, 1, 20]), [10.5, 6.5, 3.5], self.FUR2, rot=Rt)
        # claws
        for nm in 'LR':
            h = J['hand' + nm]
            fd = J['fore' + nm] / max(1e-6, np.linalg.norm(J['fore' + nm]))
            for k in (-1, 0, 1):
                side = np.cross(fd, np.array([0, 0, 1.0]))
                side = side / max(1e-6, np.linalg.norm(side))
                f.cone(h + side * k * 1.4, h + side * k * 1.6 + fd * 5.5, 0.7, 0.05, BONE)
        # tail
        base = pel + np.array([0, -3, 1])
        f.limb(base, base + Rt @ np.array([0, -8, -2]), 2.2, 1.6, self.FUR2)
        f.limb(base + Rt @ np.array([0, -8, -2]), base + Rt @ np.array([0, -15, 3]), 1.6, 0.4, self.FUR2)


class FireElemental(Humanoid):
    layout = 'WWWWAAAPPDDDDDXXXXXXX'
    CORE = M((70, 16, 8), noise=0.1)
    EMBER = M((255, 120, 20), glow=0.75, noise=0.25)
    sp = dict(leg1=13, leg2=13, torso_len=18, torso_w=6.2, torso_d=4.2, sh_w=8, arm1=9, arm2=10, head_r=4.2,
              pants=M((235, 100, 20), glow=0.6, noise=0.25), boots=M((190, 60, 12), glow=0.4, noise=0.2),
              cloth=M((80, 20, 10), noise=0.1), sleeve=M((240, 110, 24), glow=0.6, noise=0.25), glove=M((255, 180, 50), glow=0.8, noise=0.2),
              skin=M((60, 14, 8), noise=0.1), leg_r=3.0, arm_r=2.5)

    def walk(self, i):
        return walk_pose(i, amp=26, arm=30, lean=6)

    def attack(self, i, n):
        if i == 0:
            return pose(lean=-6, shL=120, shR=120, abL=30, abR=30, elL=40, elR=40, hipL=8, hipR=-8)
        if i == 1:
            return pose(lean=-4, shL=150, shR=150, abL=24, abR=24, elL=20, elR=20)
        return pose(lean=12, shL=80, shR=80, abL=14, abR=14, elL=10, elR=10, hipL=12, hipR=-8, kneeL=20)

    def pain(self, i, n):
        return pain_pose()

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        ember = self.EMBER
        # glowing cracks on the chest
        for k, (x, z) in enumerate(((-2.5, 6), (2.5, 11), (0, 15), (-3, 14), (3, 5))):
            f.box(T([x, 4.0, z]), [0.6, 0.5, 2.4], GLOW(255, 190, 60), rot=Rt)
        f.ell(T([0, 0, 9]), [7.2, 5.0, 3.0], ember, rot=Rt)
        f.sph(H([-1.8, 3.4, 0.8]), 0.9, GLOW(255, 255, 160))
        f.sph(H([1.8, 3.4, 0.8]), 0.9, GLOW(255, 255, 160))
        # flames on head, shoulders and back
        flame(f, H([0, 0, 3.5]), 9, 3.6, GLOW(255, 240, 120), GLOW(255, 130, 20), 6, 3)
        for s in (-1, 1):
            flame(f, J['sh' + ('L' if s > 0 else 'R')] + np.array([0, 0, 1.5]), 6, 2.8, GLOW(255, 230, 100), GLOW(240, 90, 16), 4, 5 + s)
        flame(f, T([0, -3, 14]), 8, 3.6, GLOW(255, 230, 100), GLOW(240, 90, 16), 4, 9)
        # a fireball between the hands
        if cat == 'A':
            c = (J['handL'] + J['handR']) / 2 + Rt @ np.array([0, 4, 3])
            r = 3.5 + (2.5 if i >= 1 else 0)
            f.sph(c, r, GLOW(255, 200, 60))
            f.sph(c, r * 0.55, GLOW(255, 250, 200))


class RobedCaster(Humanoid):
    """shared bits for wizard-like figures: a robe over legs that are not drawn"""
    ROBE = M((100, 44, 124), noise=0.1)
    TRIM = M((230, 190, 60), spec=0.4, noise=0.02)
    robe_h = 30
    robe_r0 = 11
    robe_r1 = 7
    float_z = 0

    def figure(self, idx):
        f = super().figure(idx)
        return f if self.float_z == 0 else f.transformed(np.eye(3), t=(0, 0, self.float_z))

    def robe_for(self, f, ps, J):
        sway = ps.get('hipL', 0) * 0.04
        robe(f, 0.0, self.robe_h, self.robe_r0, self.robe_r1, self.ROBE, self.TRIM, 1.6, sway=sway)


class MadWizard(RobedCaster):
    layout = 'WWWWAAAPDDDDDDDDD'
    ROBE = M((104, 46, 128), noise=0.1)
    sp = dict(leg1=14, leg2=14, legs=False, torso_len=16, torso_w=7.2, torso_d=5.2, sh_w=8, arm1=8.5, arm2=9, head_r=4.6,
              pants=M((70, 30, 90)), boots=M((50, 30, 40)), cloth=M((112, 50, 136), noise=0.1), sleeve=M((118, 54, 142), noise=0.1),
              sleeve2=M((130, 66, 156), noise=0.1), glove=M((230, 190, 150)), skin=M((236, 196, 160), noise=0.05), arm_r=2.9, neck=1.0)
    robe_h = 31
    robe_r0 = 12
    robe_r1 = 7.5

    def walk(self, i):
        d = walk_pose(i, amp=10, arm=0, bob=0.8)
        d.update(tgtR=(-9, 5, 10), tgtL=(7, 8, 13), hipL=0, hipR=0)
        d['lean'] = 3 * math.sin(i * math.pi / 2)
        return d

    def attack(self, i, n):
        if i == 0:
            return dict(pose(), tgtR=(-7, 6, 25), tgtL=(7, 12, 19), lean=-6)
        if i == 1:
            return dict(pose(), tgtR=(-4, 18, 22), tgtL=(5, 17, 20), lean=-2)
        return dict(pose(), tgtR=(-4, 20, 20), tgtL=(6, 8, 14), lean=4)

    def pain(self, i, n):
        return dict(pose(lean=-12, hd=22), tgtR=(-10, 3, 22), tgtL=(10, 3, 22))

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        self.robe_for(f, ps, J)
        hat = M((80, 34, 108), noise=0.08)
        f.ell(H([0, 0, 3.0]), [8.8, 8.8, 1.0], hat, rot=Rh)
        f.cone(H([0, 0, 3.4]), H([2.0, -1, 16]), 5.2, 1.2, hat)
        f.cone(H([2.0, -1, 15.6]), H([6.5, -2.5, 19]), 1.3, 0.1, hat)
        f.cone(H([0, 0, 3.0]), H([0, 0, 4.4]), 5.4, 5.4, M((230, 190, 60), spec=0.4))
        f.sph(H([-2.2, 4.1, 0.6]), 1.25, GLOW(255, 240, 80))
        f.sph(H([2.2, 4.1, 0.6]), 1.25, GLOW(255, 240, 80))
        f.sph(H([-2.2, 4.8, 0.6]), 0.5, BLACK)
        f.sph(H([2.2, 4.8, 0.6]), 0.5, BLACK)
        white = M((236, 234, 228), noise=0.2)
        f.ell(H([0, 3.3, -3.2]), [3.4, 2.4, 3.8], white, rot=Rh)               # beard
        f.ell(H([0, 3.0, -7.0]), [2.2, 1.8, 3.6], white, rot=Rh)
        f.ell(H([-4.0, 0.5, -0.5]), [1.6, 2.2, 2.6], white, rot=Rh)            # hair
        f.ell(H([4.0, 0.5, -0.5]), [1.6, 2.2, 2.6], white, rot=Rh)
        f.ell(H([0, 4.7, -0.5]), [1.1, 1.2, 1.0], M((220, 160, 130)), rot=Rh)    # nose
        # staff in the right hand
        hr = J['handR']
        wood = M((100, 70, 40), noise=0.2)
        top = hr + np.array([0, 0.5, 15])
        f.cone(np.array([hr[0], hr[1], 0.5]), top, 1.1, 0.9, wood)
        f.sph(top + np.array([0, 0, 1.5]), 2.6, GLOW(255, 120, 255) if cat != 'A' else GLOW(255, 240, 160))
        for k in range(4):
            a = k * math.pi / 2
            f.cone(top + np.array([0, 0, 0.3]), top + np.array([math.cos(a) * 3.2, math.sin(a) * 3.2, 4.0]), 0.5, 0.05, wood)
        if cat == 'A' and i >= 1:
            c = top + np.array([0, 9, 1.5])
            f.sph(c, 4.2 + i, GLOW(200, 80, 255))
            f.sph(c, 2.4 + i, GLOW(255, 230, 255))


class Apprentice(RobedCaster):
    layout = 'WWWWAAAPPDDDDDD'
    sp = dict(leg1=13, leg2=15, legs=False, torso_len=17, torso_w=6.4, torso_d=4.6, sh_w=7.4, arm1=9, arm2=10, head_r=4.2,
              pants=M((50, 70, 80)), boots=M((60, 44, 36)), cloth=M((44, 112, 124), noise=0.1), sleeve=M((50, 120, 132), noise=0.1),
              sleeve2=M((50, 120, 132), noise=0.1), glove=M((226, 184, 150)), skin=M((226, 184, 150), noise=0.05), arm_r=2.6)
    ROBE = M((40, 104, 116), noise=0.1)
    TRIM = M((220, 210, 170), spec=0.2, noise=0.05)
    robe_h = 34
    robe_r0 = 11
    robe_r1 = 6.4

    def walk(self, i):
        d = walk_pose(i, amp=10, arm=0, bob=0.8)
        d.update(tgtR=(-6, 8, 12), tgtL=(6, 9, 12), hipL=0, hipR=0, lean=2 * math.sin(i * math.pi / 2))
        return d

    def attack(self, i, n):
        z = (0, 1, 2)[min(i, 2)]
        return dict(pose(lean=-3 + 3 * z), tgtR=(-4, 12 + 3 * z, 19 - z), tgtL=(4, 12 + 3 * z, 19 - z))

    def pain(self, i, n):
        return dict(pose(lean=-14, hd=22), tgtR=(-9, 2, 21), tgtL=(9, 2, 21))

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        self.robe_for(f, ps, J)
        hood = M((36, 94, 106), noise=0.1)
        f.ell(H([0, -0.6, 0.8]), [5.4, 5.8, 5.4], hood, rot=Rh)
        f.cone(H([0, -3, 3.4]), H([0, -6, 8.5]), 3.2, 0.1, hood)
        f.ell(H([0, 3.0, -0.2]), [3.4, 2.2, 3.4], M((226, 184, 150)), rot=Rh)
        f.sph(H([-1.6, 4.4, 0.6]), 0.7, GLOW(180, 255, 250))
        f.sph(H([1.6, 4.4, 0.6]), 0.7, GLOW(180, 255, 250))
        f.cone(T([0, 0, 1.2]), T([0, 0.5, 1.2]), 7.4, 7.4, M((150, 110, 60)), caps=True)    # rope belt
        f.ell(T([0, 5.4, 12]), [2.4, 0.5, 2.6], M((230, 200, 80), spec=0.5), rot=Rt)        # amulet
        if cat == 'A':
            c = (J['handL'] + J['handR']) / 2 + Rt @ np.array([0, 4, 0])
            f.sph(c, 3.0 + i * 1.3, GLOW(80, 255, 220))
            f.sph(c, 1.5 + i * 0.6, GLOW(230, 255, 250))
        else:
            c = (J['handL'] + J['handR']) / 2 + Rt @ np.array([0, 2, 1])
            f.sph(c, 2.2, GLOW(80, 255, 220))


class Minister(RobedCaster):
    layout = 'WWWWAAAPPDDDDDD'
    ROBE = M((126, 20, 36), noise=0.08)
    TRIM = M((236, 196, 70), spec=0.5, noise=0.02)
    sp = dict(leg1=14, leg2=14, legs=False, torso_len=19, torso_w=7.6, torso_d=5.0, sh_w=9, arm1=10, arm2=11, head_r=4.2,
              pants=M((50, 10, 20)), boots=M((40, 10, 20)), cloth=M((140, 24, 40), noise=0.08), sleeve=M((120, 18, 32), noise=0.08),
              sleeve2=M((236, 196, 70), spec=0.4), glove=M((236, 232, 222)), skin=M((236, 232, 222)), arm_r=3.2)
    robe_h = 40
    robe_r0 = 12
    robe_r1 = 7.5
    float_z = 12

    def robe_for(self, f, ps, J):
        # the robe narrows to a trailing point: nothing touches the ground
        f.cone(np.array([0, 0, 0.0]), np.array([0, 0, 30]), 1.0, 11.0, self.ROBE, caps=False)
        f.cone(np.array([0, 0, 30]), np.array([0, 0, 42]), 11.0, 7.5, self.ROBE, caps=False)
        f.cone(np.array([0, 0, 28]), np.array([0, 0, 31]), 11.3, 11.3, self.TRIM, caps=False)
        for k in range(5):
            a = k * 2 * math.pi / 5
            f.cone(np.array([math.cos(a) * 3, math.sin(a) * 3, 8]), np.array([math.cos(a) * 6, math.sin(a) * 6, -3]), 1.0, 0.1, self.ROBE)

    def walk(self, i):
        d = pose(lean=2 * math.sin(i * math.pi / 2), drop=0.8 * math.sin(i * math.pi / 2))
        d.update(tgtR=(-10, 4, 14), tgtL=(10, 4, 14), abL=40, abR=40)
        return d

    def attack(self, i, n):
        z = (0, 1, 2)[min(i, 2)]
        return dict(pose(lean=-5 + 2 * z), tgtR=(-6, 8 + 5 * z, 20 + z * 2), tgtL=(6, 8 + 5 * z, 20 + z * 2))

    def pain(self, i, n):
        return dict(pose(lean=-14, hd=20), tgtR=(-12, 2, 22), tgtL=(12, 2, 22))

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        self.robe_for(f, ps, J)
        gold = self.TRIM
        # mask, mitre and halo
        f.ell(H([0, 0.4, 0.2]), [4.0, 4.2, 4.8], M((240, 236, 226), spec=0.3), rot=Rh)
        f.box(H([-1.8, 4.4, 0.8]), [0.9, 0.3, 0.35], BLACK, rot=Rh)
        f.box(H([1.8, 4.4, 0.8]), [0.9, 0.3, 0.35], BLACK, rot=Rh)
        f.box(H([0, 4.5, -1.8]), [0.8, 0.3, 0.2], M((150, 30, 40)), rot=Rh)
        f.cone(H([0, 0, 3.4]), H([0, -0.5, 15]), 4.2, 0.4, M((126, 20, 36)))
        f.cone(H([0, 0, 3.4]), H([0, 0, 5.4]), 4.5, 4.5, gold)
        f.cone(H([0, -2.4, 0.5]), H([0, -3.0, 0.9]), 7.0, 7.0, GLOW(255, 230, 120), caps=True) if False else \
            f.cyl(H([0, -3.2, 1.0]), 6.8, 0.8, GLOW(255, 224, 110), rot=(90, 0, 0))
        f.cone(T([-3.2, 5.2, 18]), T([3.2, 5.2, 8]), 0.8, 0.8, gold)       # stole
        f.cone(T([3.2, 5.2, 18]), T([-3.2, 5.2, 8]), 0.8, 0.8, gold)
        if cat == 'A':
            c = (J['handL'] + J['handR']) / 2 + Rt @ np.array([0, 4, 0])
            f.sph(c, 3.4 + i * 1.2, GLOW(255, 60, 90))
            f.sph(c, 1.8 + i * 0.5, GLOW(255, 230, 230))


class Golem(Humanoid):
    layout = 'WWWWAAAPPDDDDDDDXXXX'
    ROCK = M((126, 116, 102), noise=0.2)
    sp = dict(leg1=15, leg2=15, torso_len=26, torso_w=13, torso_d=8.5, torso_kind='box', sh_w=17, arm1=14, arm2=14, head_r=5.6,
              pants=M((110, 102, 90), noise=0.22), boots=M((96, 88, 78), noise=0.22), cloth=M((126, 116, 102), noise=0.2),
              sleeve=M((118, 108, 96), noise=0.22), glove=M((104, 96, 84), noise=0.25), skin=M((122, 112, 98), noise=0.22),
              leg_r=6.5, arm_r=5.8, foot_len=10, hip_w=7, head_kind='box', neck=0.5, hip_z=0)

    def walk(self, i):
        d = walk_pose(i, amp=16, arm=14, bob=1.0, lean=4)
        return d

    def attack(self, i, n):
        if i == 0:
            return dict(pose(lean=-8, shL=165, shR=165, abL=18, abR=18, elL=10, elR=10), )
        if i == 1:
            return dict(pose(lean=14, shL=70, shR=70, abL=10, abR=10, elL=0, elR=0))
        return dict(pose(lean=6, shL=40, shR=40))

    def pain(self, i, n):
        return pain_pose(lean=-8)

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        rock = self.ROCK
        # blocky head
        f.box(H([0, 0.5, 0.6]), [5.2, 4.6, 4.4], rock, rot=Rh)
        f.box(H([-2.4, 4.8, 0.6]), [1.6, 0.4, 0.7], GLOW(255, 140, 40), rot=Rh)
        f.box(H([2.4, 4.8, 0.6]), [1.6, 0.4, 0.7], GLOW(255, 140, 40), rot=Rh)
        # shoulder boulders, chest stones, glowing cracks
        for s in (-1, 1):
            f.ell(T([s * 17, 0, 24]), [6.5, 6.0, 5.4], M((112, 104, 92), noise=0.25), rot=Rt)
            f.ell(J['hand' + ('L' if s > 0 else 'R')], 7.2, M((100, 92, 82), noise=0.25))
        f.ell(T([0, 7.5, 16]), [9, 2.2, 7], M((138, 128, 112), noise=0.2), rot=Rt)
        for (x, z, h) in ((-3, 12, 4), (3, 18, 3), (0, 15, 5), (-5, 20, 2.5)):
            f.box(T([x, 9.4, z]), [0.5, 0.4, h * 0.5], GLOW(255, 120, 30), rot=Rt)
        for (x, z) in ((-8, 6), (7, 4), (0, 2)):
            f.ell(T([x, 8.2, z]), [3, 1.8, 2.6], M((92, 100, 70), noise=0.3), rot=Rt)           # moss
        if cat == 'A':
            c = (J['handL'] + J['handR']) / 2 + Rt @ np.array([0, 2, 8 if i == 0 else 3])
            if i < 2:
                f.ell(c, [8, 8, 7], M((108, 98, 88), noise=0.3))
                f.box(c + np.array([2, 5, 3]), [2.2, 0.4, 2], M((70, 62, 54), noise=0.2))


class Mindeater(Humanoid):
    layout = 'WWWWWWAAAAPPDDDDDDDDXXXXXXXXX'
    sp = dict(leg1=14, leg2=14, torso_len=17, torso_w=5.2, torso_d=3.6, sh_w=6.6, arm1=11, arm2=12, head_r=3.6,
              pants=M((64, 54, 76), noise=0.1), boots=M((46, 40, 56)), cloth=M((70, 56, 86), noise=0.12), sleeve=M((120, 130, 112), noise=0.1),
              sleeve2=M((120, 130, 112), noise=0.1), glove=M((140, 150, 126), noise=0.1), skin=M((140, 150, 126), noise=0.1),
              leg_r=2.2, arm_r=1.9, neck=1.5)
    BRAIN = M((236, 150, 170), spec=0.5, shin=14, noise=0.2)

    def walk(self, i):
        return walk_pose(i, n=6, amp=22, arm=16, lean=5, bob=1.0)

    def attack(self, i, n):
        z = min(i, 3)
        return pose(lean=-4, shL=95 + 8 * z, shR=95 + 8 * z, abL=34, abR=34, elL=24, elR=24, hd=-6)

    def pain(self, i, n):
        return pain_pose()

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        # the exposed brain
        f.ell(H([0, -0.8, 5.2]), [6.6, 6.2, 5.2], self.BRAIN, rot=Rh)
        f.ell(H([-3.4, -0.4, 5.6]), [3.4, 5.8, 4.6], M((226, 130, 160), spec=0.5, noise=0.25), rot=Rh)
        f.ell(H([3.4, -0.4, 5.6]), [3.4, 5.8, 4.6], M((226, 130, 160), spec=0.5, noise=0.25), rot=Rh)
        glow = 0.0
        f.sph(H([-1.5, 3.2, 0.6]), 0.9, GLOW(255, 255, 255))
        f.sph(H([1.5, 3.2, 0.6]), 0.9, GLOW(255, 255, 255))
        # tentacles from the jaw
        for k, s in enumerate((-1.6, 0, 1.6)):
            a = H([s, 2.6, -2.6])
            for j in range(4):
                b = a + Rt @ np.array([s * 0.5 + 0.4 * math.sin(j + k), 0.4 + 0.8 * math.cos(j * 1.2 + k), -3.2])
                f.limb(a, b, 0.7 - j * 0.12, 0.6 - j * 0.12, M((200, 110, 140), noise=0.1), joint=True)
                a = b
        # a ragged cloak
        f.cone(T([0, -4, 17]), T([0, -6, 3]), 5.4, 7.0, M((54, 44, 70), noise=0.1), caps=False)
        f.cone(T([0, 3.8, 0.8]), T([0, 3.8, 8]), 3.0, 2.0, M((90, 74, 110), noise=0.1), caps=False)
        if cat == 'A':
            for r_, a_ in ((8.0 + i * 3, 0), (13.0 + i * 3, 1)):
                ring(f, H([0, 0, 5]), r_, 14, 0.7, GLOW(255, 140, 220), 'z', a_ * 0.2)


class Aegis(Humanoid):
    layout = 'WWAAPPDDDDD'
    floatz = 14

    def figure(self, idx):
        cat, i, n = letter_cat(self.layout, idx)
        f = Fig()
        z = self.floatz + (1.5 if (cat == 'W' and i == 1) else 0)
        tilt = (-18 if cat == 'P' else 0)
        steel = M((150, 158, 176), spec=0.7, shin=30, noise=0.03)
        gold = M((226, 184, 70), spec=0.6, shin=26, noise=0.03)
        parts = Fig()
        parts.cyl((0, 0, 0), 13, 3.0, steel, rot=(90, 0, 0))
        parts.cyl((0, 1.8, 0), 11, 1.2, M((92, 100, 124), spec=0.5, noise=0.03), rot=(90, 0, 0))
        parts.ell((0, 3, 0), [4.5, 2.2, 4.5], gold)
        ring(parts, (0, 2.4, 0), 8.4, 12, 0.9, gold, 'y')
        glow = GLOW(120, 230, 255) if cat != 'A' else GLOW(255, 255, 255)
        for a in range(4):
            ang = a * math.pi / 2 + math.pi / 4
            parts.box((math.cos(ang) * 6.2, 2.7, math.sin(ang) * 6.2), [0.7, 0.4, 2.0], glow, rot=(0, -ang * 180 / math.pi + 90, 0))
        if cat == 'D':
            p = (i + 1) / n
            # the shield breaks into pieces that drift apart
            rs = np.random.RandomState(4)
            f = Fig()
            for k in range(8):
                a = k * math.pi / 4
                d = 13 * p * rs.uniform(0.8, 1.6)
                shard = Fig()
                shard.box((math.cos(a) * 5.5, 0, math.sin(a) * 5.5), [4.0, 1.4, 2.4], steel, rot=(0, -a * 180 / math.pi, rs.uniform(-30, 30) * p))
                f.extend(shard.transformed(np.eye(3), t=(math.cos(a) * d, rs.uniform(-3, 3) * p, math.sin(a) * d - 8 * p * p), pivot=(0, 0, 0)))
            f.sph((0, 0, 0), 4 * (1 - p) + 0.5, GLOW(200, 250, 255))
            return f.transformed(np.eye(3), t=(0, 0, self.floatz - 8 * p))
        g = parts.transformed(rot_matrix(rx=tilt), t=(0, 0, z))
        return g


class Hatred(Humanoid):
    layout = 'WWAAPPDDDDD'

    def figure(self, idx):
        cat, i, n = letter_cat(self.layout, idx)
        f = Fig()
        z = 18 + (1.5 if (cat == 'W' and i == 1) else 0)
        bone = M((232, 220, 196), noise=0.12)
        jaw = 5.0 if cat == 'A' else 1.5
        tilt = -20 if cat == 'P' else 0
        s = Fig()
        s.ell((0, 0, 1.5), [5.6, 6.2, 6.0], bone)                                      # cranium
        s.ell((0, 4.2, -2.0), [3.8, 3.0, 2.6], bone)                                   # face
        s.ell((0, 2.0, -5.2 - jaw * 0.3), [3.2, 3.4, 1.3], bone)                       # jaw
        s.sph((-2.4, 5.0, 0.0), 1.7, M((20, 6, 8)))
        s.sph((2.4, 5.0, 0.0), 1.7, M((20, 6, 8)))
        s.sph((-2.4, 5.6, 0.0), 0.9, GLOW(255, 40, 40))
        s.sph((2.4, 5.6, 0.0), 0.9, GLOW(255, 40, 40))
        s.cone((0, 6.8, -2.4), (0, 6.9, -3.6), 0.8, 0.2, M((30, 8, 10)))
        for k in range(5):
            x = (k - 2) * 1.2
            s.cone((x, 6.0, -3.6), (x, 6.0, -5.2 - jaw * 0.4), 0.4, 0.05, M((246, 240, 224)))
        # black flames streaming behind
        dark = GLOW(120, 20, 60) if cat != 'A' else GLOW(255, 60, 40)
        mid = GLOW(190, 40, 40)
        for k in range(7):
            a = k * 2 * math.pi / 7 + 0.3
            base = np.array([math.cos(a) * 3.0, -1.5, 2.0 + math.sin(a) * 3.0])
            s.cone(base, base + np.array([math.cos(a) * 3, -9 - (k % 3) * 2.5, math.sin(a) * 3 + 1]), 2.2, 0.05, dark)
        for k in range(4):
            a = k * 2 * math.pi / 4 + 1.0
            base = np.array([math.cos(a) * 2.0, -1.0, 2.0 + math.sin(a) * 2.0])
            s.cone(base, base + np.array([math.cos(a) * 1.5, -6.5 - (k % 2) * 2, math.sin(a) * 1.5]), 1.3, 0.05, mid)
        if cat == 'D':
            p = (i + 1) / n
            f = Fig()
            rs = np.random.RandomState(8)
            for k in range(7):
                a = rs.uniform(0, 6.28)
                d = 12 * p * rs.uniform(0.5, 1.2)
                f.sph((math.cos(a) * d, math.sin(a) * d * 0.6, 18 + rs.uniform(-6, 6) * p - 12 * p * p), 2.2 * (1 - 0.6 * p), bone)
            f.sph((0, 0, 18 - 10 * p), 6 * (1 - p) + 0.4, GLOW(255, 90, 60))
            return f
        return s.transformed(rot_matrix(rx=tilt), t=(0, 0, z))


class Bore(Humanoid):
    layout = 'WWWWAAAPDDDD'
    SKIN = M((196, 150, 120), noise=0.14)
    BAND = M((150, 96, 84), noise=0.14)

    def figure(self, idx):
        cat, i, n = letter_cat(self.layout, idx)
        f = Fig()
        t = 2 * math.pi * i / 4 if cat == 'W' else 0
        rear, open_, lean = 0.0, 0.0, 0
        if cat == 'A':
            rear = (8, 14, 4)[min(i, 2)]
            open_ = (0.3, 1.0, 0.6)[min(i, 2)]
        if cat == 'P':
            rear = -4
        # body: a chain of segments from the tail (back, -y) up to the head (front, +y)
        pts = []
        for k in range(8):
            u = k / 7.0
            y = -20 + 40 * u
            z = 7 + 2.5 * math.sin(k * 0.9 + t) + (rear * (u ** 2.0)) + 6 * u
            x = 2.5 * math.sin(k * 0.8 - t) * (1 - u)
            pts.append(np.array([x, y, z]))
        for k, p in enumerate(pts):
            u = k / 7.0
            r = 5.5 + 8.0 * math.sin(math.pi * (0.2 + 0.9 * u)) * 0.6 + (2.0 if k % 2 else 0)
            r = min(r, 12.5)
            f.ell(p, [r * 0.95, r * 0.7, r * 0.82], self.BAND if k % 2 else self.SKIN)
        head = pts[-1] + np.array([0, 8, 2.0])
        if cat == 'A':
            head = head + np.array([0, 2 + 1.5 * min(i, 2), 2 + rear * 0.3])
        # maw: a ring of teeth around a dark throat
        f.ell(head, [11.5, 6.5, 11.0], M((176, 120, 100), noise=0.12))
        mouth = head + np.array([0, 4.5, 0])
        f.ell(mouth, [7.5 + open_ * 2, 2.0, 7.5 + open_ * 2], M((60, 10, 18), noise=0.05))
        f.ell(mouth + np.array([0, 0.8, 0]), [4.0, 1.2, 4.0], M((20, 4, 8)))
        for k in range(12):
            a = 2 * math.pi * k / 12
            R_ = 8.0 + open_ * 1.5
            base = mouth + np.array([math.cos(a) * R_, 1.4, math.sin(a) * R_])
            tip = mouth + np.array([math.cos(a) * (R_ - 3.2 - open_), 5.5 + open_ * 2, math.sin(a) * (R_ - 3.2 - open_)])
            f.cone(base, tip, 1.5, 0.1, BONE)
        for s in (-1, 1):
            f.sph(head + np.array([s * 6.5, 3.5, 6.5]), 1.7, GLOW(255, 230, 70))
            f.sph(head + np.array([s * 6.5, 4.7, 6.7]), 0.7, BLACK)
        if cat == 'D':
            p = (i + 1) / n
            f = f.transformed(rot_matrix(ry=88 * ease(p)), pivot=(0, 0, 0))
            f = f.transformed(np.eye(3), t=(0, 0, -f.lowest()))
            blob_pool(f, (0, 0, 0.2), 14 * p + 4, BLOOD, 0.25)
        return f


class Conjurer(Humanoid):
    layout = 'WWWWAAAPPDDDD'
    sp = dict(leg1=13, leg2=15, legs=False, torso_len=19, torso_w=6.8, torso_d=4.8, sh_w=8.5, arm1=10, arm2=11, head_r=4.2,
              pants=M((30, 28, 40)), boots=M((30, 28, 40)), cloth=M((40, 38, 58), noise=0.08), sleeve=M((34, 32, 50), noise=0.08),
              sleeve2=M((52, 76, 90), noise=0.08), glove=M((170, 176, 180)), skin=M((176, 170, 160)), arm_r=2.8)

    def figure(self, idx):
        f = super().figure(idx)
        return f

    def walk(self, i):
        d = pose(lean=2 * math.sin(i * math.pi / 2), drop=0.9 * math.sin(i * math.pi / 2), lift=0)
        d.update(tgtR=(-9, 6, 12), tgtL=(9, 6, 14), abL=40, abR=40)
        return d

    def attack(self, i, n):
        z = (0, 1, 2)[min(i, 2)]
        return dict(pose(lean=-4 + z), tgtR=(-8, 5 + 4 * z, 22 + z), tgtL=(8, 5 + 4 * z, 22 + z))

    def pain(self, i, n):
        return dict(pose(lean=-14, hd=22), tgtR=(-11, 1, 21), tgtL=(11, 1, 21))

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        cape = M((36, 44, 62), noise=0.1)
        trim = M((70, 200, 200), glow=0.6, noise=0)
        # ragged hem: a cloak tapering to wisps, floating
        f.cone(np.array([0, 0, 6]), np.array([0, 0, 33]), 3.0, 11.5, cape, caps=False)
        f.cone(np.array([0, 0, 33]), np.array([0, 0, 36]), 11.5, 8.5, cape, caps=False)
        for k in range(9):
            a = k * 2 * math.pi / 9
            f.cone(np.array([math.cos(a) * 6, math.sin(a) * 6, 14]), np.array([math.cos(a) * 3.5, math.sin(a) * 3.5, 1.5 + (k % 3)]), 1.3, 0.1, cape)
        f.cone(np.array([0, 0, 21]), np.array([0, 0, 22]), 9.2, 9.2, trim, caps=False)
        # horned skull mask and hood
        f.ell(H([0, -0.8, 0.4]), [5.4, 5.6, 5.4], cape, rot=Rh)
        f.ell(H([0, 3.2, -0.6]), [3.6, 2.4, 3.8], BONE, rot=Rh)
        f.sph(H([-1.6, 4.8, 0.4]), 0.8, GLOW(80, 255, 240))
        f.sph(H([1.6, 4.8, 0.4]), 0.8, GLOW(80, 255, 240))
        for s in (-1, 1):
            f.limb(H([s * 3.8, 1.0, 2.8]), H([s * 7.5, 0.4, 5.8]), 1.0, 0.8, BONE)
            f.limb(H([s * 7.5, 0.4, 5.8]), H([s * 6.0, 0.5, 11.5]), 0.8, 0.1, BONE)
        # a staff topped by a floating ring
        hr = J['handR']
        f.cone(np.array([hr[0], hr[1], 2]), hr + np.array([0, 0, 17]), 0.9, 0.8, M((60, 50, 44), noise=0.2))
        ring(f, hr + np.array([0, 0, 20]), 3.2, 8, 0.7, GLOW(80, 255, 240), 'y')
        f.sph(hr + np.array([0, 0, 20]), 1.2, GLOW(200, 255, 250))
        if cat == 'A':
            for k in range(4):
                a = k * math.pi / 2 + i * 0.5
                c = (J['handL'] + J['handR']) / 2 + Rt @ np.array([math.cos(a) * 5, 6, 4 + math.sin(a) * 4])
                f.sph(c, 1.6 + i * 0.3, GLOW(255, 120, 255))
        f2 = f
        self.last = f2


class Mecha(Humanoid):
    layout = 'WWWWAAAPDDDDDDDD'
    PAINT = M((120, 126, 110), spec=0.35, shin=22, noise=0.05)
    sp = dict(leg1=30, leg2=28, torso_len=36, torso_w=18, torso_d=11, torso_kind='box', sh_w=24, arm1=20, arm2=20, head_r=7,
              pants=M((86, 90, 80), spec=0.3, noise=0.05), boots=M((60, 62, 62), spec=0.4, noise=0.04),
              cloth=M((124, 130, 112), spec=0.35, shin=22, noise=0.05), sleeve=M((100, 106, 92), spec=0.3, noise=0.05),
              sleeve2=M((74, 78, 80), spec=0.45, noise=0.04), glove=M((60, 62, 66), spec=0.5), skin=M((90, 96, 90), spec=0.4, noise=0.03),
              leg_r=8.5, arm_r=6.0, foot_len=16, hip_w=11, head_kind='box', neck=1.0)

    def walk(self, i):
        return walk_pose(i, amp=20, arm=10, bob=1.8, lean=2)

    def attack(self, i, n):
        z = (0, 1, 2)[min(i, 2)]
        return pose(lean=-3, hipL=8, hipR=-8, shL=55 + 8 * z, shR=45, abL=6, abR=10, elL=30, elR=60)

    def pain(self, i, n):
        return pain_pose(lean=-8)

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        paint = self.PAINT
        dark = M((50, 52, 58), spec=0.4, noise=0.04)
        # sensor head with a visor
        f.box(H([0, 0.5, 0.8]), [7.6, 6.6, 5.4], paint, rot=Rh)
        f.box(H([0, 6.8, 1.0]), [6.0, 0.5, 1.6], GLOW(255, 70, 50), rot=Rh)
        f.cone(H([3, 0, 5]), H([4, -1, 18]), 0.6, 0.3, dark)
        # cockpit glass, vents, hazard stripes
        f.box(T([0, 11.2, 25]), [10, 0.8, 6], M((80, 190, 210), spec=0.9, shin=40, glow=0.35, noise=0.02), rot=Rt)
        for k in range(4):
            f.box(T([-12 + k * 3.2, 11.2, 8]), [0.8, 0.5, 4.5], dark, rot=Rt)
        for k in range(3):
            f.box(T([8 + k * 3.0, 11.3, 6]), [1.1, 0.5, 1.6], M((246, 190, 40)), rot=Rt)
        # shoulders
        for s in (-1, 1):
            f.ell(T([s * 24, 0, 33]), [9, 9, 8], paint, rot=Rt)
        # left arm: a rocket pod; right arm: a flame nozzle
        hl, hr = J['handL'], J['handR']
        fl = J['foreL'] / max(1e-6, np.linalg.norm(J['foreL']))
        fr = J['foreR'] / max(1e-6, np.linalg.norm(J['foreR']))
        for dx in (-3.2, 3.2):
            for dz in (-3.2, 3.2):
                off = Rt @ np.array([dx, 0, dz])
                f.cone(hl + off - fl * 8, hl + off + fl * 9, 2.4, 2.4, dark)
                f.cone(hl + off + fl * 8.6, hl + off + fl * 9.4, 2.0, 2.0, GLOW(255, 120, 50))
        f.cone(hr - fr * 6, hr + fr * 14, 4.2, 3.0, dark)
        f.cone(hr + fr * 13.5, hr + fr * 16, 3.6, 3.6, M((140, 60, 30), spec=0.4))
        if cat == 'A':
            f.sph(hr + fr * 20, 5 + 2 * (i % 2), GLOW(255, 190, 60))
            (f.sph(hl + fl * 12, 3.5, GLOW(255, 230, 170)) if i == 1 else None)
        # back pack with heat-seeker tubes
        f.box(T([0, -11.5, 28]), [13, 4.5, 9], dark, rot=Rt)
        for sx in (-8, -2.7, 2.7, 8):
            f.cone(T([sx, -11.5, 31]), T([sx, -11.5, 42]), 2.2, 2.2, M((170, 60, 44), spec=0.3))
            f.cone(T([sx, -11.5, 42]), T([sx, -11.5, 46]), 2.2, 0.2, M((220, 220, 210), spec=0.3))

    def figure(self, idx):
        f = super().figure(idx)
        cat, i, n = letter_cat(self.layout, idx)
        if cat == 'D':
            p = (i + 1) / n
            rs = np.random.RandomState(11)
            for k in range(8):
                a = rs.uniform(0, 6.28)
                d = 34 * ease(p) * rs.uniform(0.3, 1.0)
                f.box((math.cos(a) * d, math.sin(a) * d, 2 + 14 * p * (1 - p) * rs.uniform(1, 4)), rs.uniform(2, 5, 3), M((90, 94, 96), spec=0.4))
            flame(f, (0, 0, 6), 18 * (1 - p * 0.5), 12, GLOW(255, 240, 130), GLOW(255, 120, 30), 6, 3)
        return f


class Beetle(Humanoid):
    layout = 'WWWWAAAPPDDDDDDDDDD'
    SHELL = M((34, 70, 92), spec=0.9, shin=38, noise=0.05)
    SHELL2 = M((24, 108, 96), spec=0.9, shin=38, noise=0.05)
    DARK = M((22, 26, 34), spec=0.7, shin=30, noise=0.04)

    def figure(self, idx):
        cat, i, n = letter_cat(self.layout, idx)
        f = Fig()
        t = 2 * math.pi * i / 4 if cat == 'W' else 0
        rise = 0
        spit = 0
        if cat == 'A':
            rise = (14, 26, 20)[min(i, 2)]
            spit = (0, 1, 0.5)[min(i, 2)]
        if cat == 'P':
            rise = -6
        # body
        body = Fig()
        body.ell((0, 0, 0), [46, 62, 26], self.SHELL)
        body.ell((0, -8, 7), [43, 56, 22], self.SHELL2)                       # wing cases
        body.box((0, -6, 29), [0.8, 50, 1.5], self.DARK)                       # the seam between them
        body.ell((0, 52, 4), [26, 22, 17], self.DARK)                           # thorax
        for s in (-1, 1):
            body.ell((s * 32, 30, 6), [14, 22, 10], M((30, 36, 50), spec=0.7, shin=30, noise=0.05))
        # head with mandibles, eyes and antennae
        hd = Fig()
        hd.ell((0, 0, 0), [17, 16, 13], self.DARK)
        for s in (-1, 1):
            hd.sph((s * 11, 8, 6), 5, GLOW(255, 190, 40))
            hd.limb((s * 8, 13, -4), (s * 15, 24, -7), 4.0, 3.0, M((70, 56, 40), spec=0.6))
            hd.limb((s * 15, 24, -7), (s * 6, 34, -6), 3.0, 0.8, M((70, 56, 40), spec=0.6), joint=False)
            hd.limb((s * 5, 11, 9), (s * 14, 24, 28 + 4 * spit), 1.3, 0.5, DARKL)
            hd.limb((s * 14, 24, 28 + 4 * spit), (s * 22, 40, 24), 0.8, 0.3, DARKL, joint=False)
        if spit:
            hd.sph((0, 21, -6), 6 * spit, GLOW(120, 255, 70))
        body = body.transformed(rot_matrix(rx=-rise * 0.5), pivot=(0, 0, 0))
        hd = hd.transformed(rot_matrix(rx=-rise * 0.8), t=(0, 62, 6), pivot=(0, 0, 0))
        body = body.transformed(np.eye(3), t=(0, 0, 36))
        hd = hd.transformed(np.eye(3), t=(0, 0, 36 + rise * 0.6))
        f.extend(body)
        f.extend(hd)
        # six legs
        leg = M((26, 34, 44), spec=0.7, shin=30, noise=0.04)
        for k, y in enumerate((38, 6, -26)):
            for s in (-1, 1):
                ph = t + (math.pi if (k + (s > 0)) % 2 else 0)
                swing = 10 * math.sin(ph) if cat == 'W' else 0
                lift = 8 * max(0, math.cos(ph)) if cat == 'W' else 0
                yy = y + swing
                hip = np.array([s * 36, y * 0.8, 38])
                knee = np.array([s * 70, yy * 1.05, 60 + lift])
                foot = np.array([s * 86, yy * 1.2 + 4, 0.5 + lift])
                if cat == 'A' and k == 0:
                    knee = knee + np.array([0, 10, 12 + rise * 0.4])
                    foot = foot + np.array([0, 22, 8 + rise * 0.8])
                f.limb(hip, knee, 5.0, 4.0, leg)
                f.limb(knee, foot, 4.0, 2.0, leg)
        if cat == 'D':
            p = (i + 1) / n
            ang = 180 * ease(clamp01(p * 1.3))
            f = f.transformed(rot_matrix(ry=ang), pivot=(0, 0, 0))
            f = f.transformed(np.eye(3), t=(0, 0, -f.lowest()))
            if p > 0.8:
                f.sph((0, 0, 4), 18, M((90, 130, 40), noise=0.2))
        return f


DARKL = M((26, 34, 44), spec=0.7, shin=30, noise=0.04)


class Heart(Humanoid):
    layout = 'WW'

    def figure(self, idx):
        f = Fig()
        pulse = 1.0 if idx == 0 else 1.14
        steel = M((120, 126, 140), spec=0.7, shin=30, noise=0.03)
        dst = M((60, 64, 76), spec=0.5, noise=0.03)
        f.cyl((0, 0, 3), 22, 6, dst)
        f.cyl((0, 0, 9), 15, 6, steel)
        for k in range(6):                                # a cage of ribs
            a = k * math.pi / 3
            p0 = np.array([math.cos(a) * 18, math.sin(a) * 18, 10])
            p1 = np.array([math.cos(a) * 21, math.sin(a) * 21, 32])
            p2 = np.array([math.cos(a) * 12, math.sin(a) * 12, 54])
            f.limb(p0, p1, 1.3, 1.3, steel)
            f.limb(p1, p2, 1.3, 1.0, steel, joint=False)
        f.cyl((0, 0, 56), 12, 2.4, steel)
        flesh = M((200 if idx == 0 else 240, 36, 52), spec=0.6, shin=14, noise=0.15, glow=0.0 if idx == 0 else 0.25)
        c = np.array([0, 0, 33.0])
        f.ell(c + np.array([-4, 0, 0]), np.array([10.5, 9.5, 12]) * pulse, flesh)
        f.ell(c + np.array([4.5, 0, 1]), np.array([9.5, 9, 11]) * pulse, flesh)
        f.ell(c + np.array([0, 0, -7]), np.array([9, 8.5, 9]) * pulse, flesh)
        for dx, dz, col in ((-5, 12, (150, 20, 60)), (4, 13, (60, 60, 150)), (0, 14, (170, 40, 40))):
            f.limb(c + np.array([dx, 0, dz - 3]), c + np.array([dx * 1.4, 0, dz + 11]), 2.4, 2.0, M(col, spec=0.5, noise=0.1))
        for k in range(5):
            a = k * 1.3 + 0.4
            f.limb(c + np.array([math.cos(a) * 6, math.sin(a) * 6, -12]), c + np.array([math.cos(a) * 14, math.sin(a) * 14, -26]), 1.4, 0.9, M((120, 20, 60), noise=0.1))
        f.cyl((0, 0, 9.5), 9, 2, GLOW(255, 70, 90) if idx else M((120, 30, 40), glow=0.3))
        return f


class Core(Humanoid):
    layout = 'WW'

    def figure(self, idx):
        f = Fig()
        steel = M((120, 128, 142), spec=0.7, shin=30, noise=0.03)
        dst = M((56, 60, 72), spec=0.5, noise=0.03)
        glow = GLOW(110, 230, 255) if idx == 0 else GLOW(210, 250, 255)
        f.cyl((0, 0, 3), 30, 6, dst)
        f.cyl((0, 0, 7), 24, 3, steel)
        f.cyl((0, 0, 40), 9 + 1.5 * idx, 46, glow)
        f.sph((0, 0, 40), 13 + 1.5 * idx, GLOW(160, 240, 255) if idx else GLOW(70, 200, 240))
        for k in range(3):
            a = k * 2 * math.pi / 3 + math.pi / 6
            c0 = np.array([math.cos(a) * 21, math.sin(a) * 21, 0])
            f.box(c0 + np.array([0, 0, 38]), [5, 5, 34], steel, rot=(0, 0, a * 180 / math.pi))
            f.box(c0 + np.array([0, 0, 38]) + np.array([math.cos(a) * 1.5, math.sin(a) * 1.5, 0]), [3, 2.6, 28], dst, rot=(0, 0, a * 180 / math.pi))
        for z, r in ((24, 17), (40, 19), (56, 17)):
            ring(f, (0, 0, z), r, 16, 1.6, M((226, 190, 70), spec=0.6), 'z', idx * 0.2 + z * 0.01)
        f.cyl((0, 0, 70), 14, 3, steel)
        for k in range(4):
            a = k * math.pi / 2 + math.pi / 4
            f.cone(np.array([math.cos(a) * 9, math.sin(a) * 9, 71]), np.array([math.cos(a) * 12, math.sin(a) * 12, 80]), 2.0, 0.1, steel)
        return f


MONSTERS.update({'WWLF': Werewolf(), 'FELM': FireElemental(), 'MWIZ': MadWizard(), 'APPR': Apprentice(), 'MINI': Minister(),
                 'GLEM': Golem(), 'MNDE': Mindeater(), 'AEGS': Aegis(), 'HTRD': Hatred(), 'BORE': Bore(), 'CNJR': Conjurer(),
                 'MECH': Mecha(), 'BEET': Beetle(), 'HART': Heart(), 'CORE': Core()})
