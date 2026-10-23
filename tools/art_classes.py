"""art_classes.py - the three player classes (soldier, witch, swordsman) in every pose the engine uses.
The parts that the player colour setting recolours (palette ramp 112-127) use SWAP, the colour of that ramp."""
import math
import numpy as np
from art_render import Fig, mat, rot_matrix
from art_rig import biped, pose, walk_pose, apply_fall, ease, gibs, blob_pool
from art_monsters import (Humanoid, M, BLOOD, BLOOD2, FLESH, BONE, STEEL, DSTEEL, BLACK, death_pose, pain_pose, letter_cat)
from art_monsters2 import GLOW, flame, ring
import art_guns

SWAP = M((44, 112, 128), noise=0.05)             # palette ramp 7: the player-colour ramp
SWAP2 = M((36, 96, 112), noise=0.05)
SWAP_S = M((56, 130, 148), spec=0.35, shin=22, noise=0.04)


def aim_frame(pitch=0.0, yaw=0.0):
    """rotation in the torso frame for a gun that points forward and up by `pitch` degrees"""
    d = np.array([math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)), math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)), math.sin(math.radians(pitch))])
    up = np.array([-math.sin(math.radians(yaw)) * math.sin(math.radians(pitch)), -math.cos(math.radians(yaw)) * math.sin(math.radians(pitch)), math.cos(math.radians(pitch))])
    x = np.cross(d, up)
    return np.stack([x, d, up], axis=1)


def face(f, H, skin, eye, lip, eyes_y=4.5, rot=None, lash=True, brow=(30, 24, 22)):
    """eyes, brows and lips on the front of a head (H maps head-frame points to the world)"""
    for sx in (-1, 1):
        f.ell(H([sx * 1.65, 4.35, 0.5]), [0.75, 0.35, 0.5], M((245, 245, 245), noise=0.0), rot=rot)
        f.sph(H([sx * 1.65, 4.6, 0.5]), 0.38, M(eye, noise=0.0))
        f.box(H([sx * 1.65, 4.4, 1.35]), [0.95, 0.25, 0.18], M(brow, noise=0.0), rot=rot)
        if lash:
            f.box(H([sx * 1.65, 4.55, 0.95]), [0.9, 0.18, 0.1], M((20, 16, 18), noise=0.0), rot=rot)
    f.ell(H([0, 4.7, -1.55]), [1.1, 0.4, 0.38], M(lip, noise=0.0), rot=rot)
    f.ell(H([0, 4.85, -0.35]), [0.45, 0.4, 0.55], M(skin, noise=0.0), rot=rot)


class ClassModel(Humanoid):
    gun = 'pistol'
    two_hand = False

    def hold(self, ps, tgtR, pitch=0.0, yaw=0.0, two=None):
        """put a gun in the right hand (and, for two-handed guns, the left hand on its support point)"""
        two = self.two_hand if two is None else two
        Rg = aim_frame(pitch, yaw)
        ps['tgtR'] = tuple(tgtR)
        ps['_gun'] = (np.asarray(tgtR, float), Rg)
        if two:
            sup = np.array(art_guns.GUNS[self.gun][1], float)
            ps['tgtL'] = tuple(np.asarray(tgtR, float) + Rg @ sup)
        return ps

    def decorate_gun(self, f, J, ps, flash=False):
        if '_gun' not in ps or self.gun is None:
            return
        t, Rg = ps['_gun']
        g = art_guns.gun(self.gun)
        g = g.transformed(J['Rt'] @ Rg, t=J['pelvis'] + J['Rt'] @ t)
        f.extend(g)
        if flash:
            tip = {'pistol': 15, 'revolver': 15, 'shotgun': 31, 'tripleshotgun': 31, 'wand': 15}.get(self.gun, 14)
            c = J['pelvis'] + J['Rt'] @ (t + Rg @ np.array([0, tip + 2.5, 2.0]))
            if self.gun == 'wand':
                f.sph(c + J['Rt'] @ (Rg @ np.array([0, 5, 0])), 5.0, GLOW(255, 120, 255))
                f.sph(c + J['Rt'] @ (Rg @ np.array([0, 5, 0])), 2.7, GLOW(255, 245, 255))
            else:
                f.sph(c, 3.6, GLOW(255, 210, 90))
                f.sph(c, 2.0, GLOW(255, 250, 200))

    # frames --------------------------------------------------------------------------------------------
    def run(self, i):
        d = walk_pose(i, n=6, amp=36, arm=34, lean=8, bob=1.6)
        d.update(elL=40, elR=40)
        return self.run_arms(d, i)

    def run_arms(self, d, i):
        return d

    def shoot(self, i):
        return self.stand()

    def jump(self, i):
        return self.stand()

    def stand(self):
        return pose()

    def build(self, ps, flash=False, extra=None):
        f, J = biped(self.sp, ps)
        self.decorate(f, J, ps, 'S', 0, 1)
        self.decorate_gun(f, J, ps, flash)
        if extra:
            extra(f, J)
        fall = ps.get('fall', 0)
        if fall:
            f = apply_fall(f, fall, ps.get('fall_roll', 0))
            ys = [p['c'][1] for p in f.prims]
            f = f.transformed(np.eye(3), t=(0, -(min(ys) + max(ys)) / 2, 0))
        return f

    def dying(self, k, n, gib=False):
        p = (k + 1) / n
        ps = death_pose(p, back=-1 if gib else 1)
        f = self.build(ps)
        if gib:
            gibs(f, p, 28, [BLOOD, BLOOD2, FLESH], rng_seed=5)
        elif k + 1 == n:
            blob_pool(f, (0, 0, 0.2), 12, BLOOD, 0.25)
        return f

    def figure(self, name, frame):
        """name = sprite family (PLAY, SRUN ...), frame = letter index"""
        raise NotImplementedError

    def play_frame(self, k):
        """the body sprite (stock PLAY layout): A stand, B-D run, E-F shoot (F with the flash), G pain, H-N death, O-W gibbed"""
        if k == 0:
            return self.build(self.stand())
        if k <= 3:
            return self.build(self.run(k + 1))
        if k == 4:
            return self.build(self.shoot(0))
        if k == 5:
            return self.build(self.shoot(1), flash=True)
        if k == 6:
            return self.build(self.pain())
        if k <= 13:
            return self.dying(k - 7, 7)
        return self.dying(k - 14, 9, gib=True)


class Soldier(ClassModel):
    gun = 'revolver'
    sp = dict(leg1=13, leg2=13, torso_len=18.5, torso_w=6.6, torso_d=4.4, torso_kind='box', sh_w=8.2, arm1=9, arm2=9, head_r=4.4,
              pants=M((54, 58, 62), noise=0.06), boots=M((30, 30, 34), spec=0.3, noise=0.03), cloth=SWAP, sleeve=M((60, 64, 68), noise=0.06),
              sleeve2=M((60, 64, 68), noise=0.06), glove=M((40, 40, 44)), skin=M((98, 62, 42), noise=0.05), leg_r=3.1, arm_r=2.5)

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        skin = M((98, 62, 42), noise=0.04)
        f.ell(H([0, -0.8, 1.9]), [5.2, 5.2, 3.9], SWAP_S, rot=Rh)                       # helmet, open at the front
        f.box(H([0, 3.0, 4.2]), [2.8, 0.7, 0.6], M((20, 30, 40), spec=0.9, shin=40, noise=0.02), rot=Rh)    # visor pushed up
        f.ell(H([0, 3.4, -0.2]), [3.5, 1.9, 3.7], skin, rot=Rh)                          # face
        f.ell(H([0, -3.4, -1.0]), [4.3, 2.2, 3.3], M((22, 18, 18), noise=0.2), rot=Rh)   # short curls at the nape
        face(f, H, (98, 62, 42), (50, 30, 20), (150, 60, 66), rot=Rh)
        for s in (-1, 1):
            f.ell(T([s * 8.2, 0, 17.4]), [3.5, 3.5, 2.7], SWAP_S, rot=Rt)                 # shoulder pads
            f.box(T([s * 3.3, 5.0, 8]), [1.7, 0.8, 2.3], M((46, 50, 56)), rot=Rt)       # ammo pouches
        f.box(T([0, 5.0, 12.5]), [4.6, 0.8, 3.4], SWAP_S, rot=Rt)                      # chest plate
        f.box(T([0, 0, 0.6]), [6.8, 4.8, 1.0], M((90, 70, 40), spec=0.3), rot=Rt)      # belt
        f.box(T([0, -6.0, 11]), [4.6, 2.2, 5.6], M((50, 54, 60)), rot=Rt)                # backpack

    def stand(self):
        d = pose()
        return self.hold(d, (-3.4, 9, 14.5), pitch=4, two=True)

    def run_arms(self, d, i):
        return self.hold(d, (-3.4, 8, 12.5 + (0.8 if i % 2 else 0)), pitch=0, two=False)

    def shoot(self, i):
        d = pose(lean=-3 if i == 1 else 0)
        z = (0, 1, 0)[min(i, 2)]
        return self.hold(d, (-3.2, 10 - 1.5 * z, 15), pitch=2 + 6 * z, two=True)

    def jump(self, i):
        k = (0, 1, 2)[min(i, 2)]
        d = pose(hipL=(30, 10, 40)[k], hipR=(-10, 14, -25)[k], kneeL=(70, 50, 30)[k], kneeR=(40, 70, 20)[k], lift=(2, 5, 0)[k], lean=(8, 2, -4)[k],
                 shL=(100, 110, 60)[k], abL=(40, 50, 50)[k], elL=30)
        return self.hold(d, (-3.4, 8, (12, 15, 10)[k]), pitch=(10, 18, -6)[k], two=False)

    def pain(self, i=0, n=1):
        d = pain_pose(shL=60, shR=70)
        return self.hold(d, (-6, 4, 18), pitch=40, two=False)

    def figure(self, name, k, n=1):
        if name in ('PLAY', 'PLYC'):
            if k == 0:
                return self.build(self.stand())
            if k <= 3:
                return self.build(self.run(k + 1))
            if k == 4:
                return self.build(self.shoot(0))
            if k == 5:
                return self.build(self.shoot(1), flash=True)
            if k == 6:
                return self.build(self.pain())
            if k <= 13:
                return self.dying(k - 7, 7)
            return self.dying(k - 14, 9, gib=True)
        if name == 'SRUN':
            return self.build(self.run(k))
        if name == 'SSHT':
            return self.build(self.shoot(k), flash=(k == 1))
        if name == 'SJMP':
            return self.build(self.jump(k))
        self.gun, keep = 'tripleshotgun', self.gun
        try:
            if name == 'SRFL':
                return self.build(self.hold(pose(), (-3.4, 9, 13.5), pitch=-6, two=True))
            if name == 'SRFS':
                z = (0, 1, 2)[min(k, 2)]
                d = pose(lean=(-2, -8, -3)[z])
                return self.build(self.hold(d, (-3.4, 8 - 1.5 * z, 14.5), pitch=(2, 10, 4)[z], two=True), flash=(k == 1))
        finally:
            self.gun = keep
        raise KeyError(name)


class Witch(ClassModel):
    gun = None
    sp = dict(leg1=13, leg2=13, torso_len=17, torso_w=5.8, torso_d=4.0, sh_w=7.2, arm1=8.8, arm2=9, head_r=4.3,
              pants=M((50, 36, 60), noise=0.06), boots=M((44, 30, 34), spec=0.3, noise=0.04), cloth=SWAP, sleeve=M((52, 36, 70), noise=0.08),
              sleeve2=M((52, 36, 70), noise=0.08), glove=M((236, 200, 176)), skin=M((240, 203, 176), noise=0.04), leg_r=2.7, arm_r=2.2)

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        hair = M((232, 198, 108), noise=0.14)
        hat = M((54, 34, 76), noise=0.08)
        f.ell(H([0, -1.4, -0.2]), [5.2, 5.0, 5.2], hair, rot=Rh)                          # blonde hair
        f.ell(H([0, -2.8, -7.0]), [4.5, 2.8, 8.4], hair, rot=Rh)                          # long, down the back
        for s in (-1, 1):
            f.ell(H([s * 4.4, 1.2, -3.2]), [1.5, 1.6, 5.0], hair, rot=Rh)                 # locks framing the face
        f.ell(H([0, 3.6, 0.2]), [3.4, 1.5, 3.6], M((244, 214, 192), noise=0.03), rot=Rh)   # face
        face(f, H, (244, 214, 192), (70, 130, 200), (200, 70, 84), rot=Rh, brow=(190, 150, 80))
        f.ell(H([0, 0, 3.4]), [8.2, 8.2, 0.9], hat, rot=Rh)
        f.cone(H([0, 0, 3.6]), H([-1.4, -2.4, 14.5]), 4.6, 0.5, hat)
        f.cone(H([0, 0, 3.4]), H([0, 0, 4.7]), 4.9, 4.9, M((232, 190, 70), spec=0.4))
        # the dress: a bodice and a skirt that flares and sways
        sway = (ps.get('hipL', 0)) * 0.12
        f.cone(T([0, 0, 1.5]), T([0, 0, 9.5]), 5.8, 4.8, SWAP, caps=False)
        f.cone(np.array([sway * 0.5, 0, pel[2] - 15]), pel + np.array([0, 0, 2.5]), 9.5, 4.8, SWAP, caps=False)
        f.ell(np.array([sway * 0.5, 0, pel[2] - 14.8]), [9.4, 9.4, 0.7], SWAP2)
        f.cone(np.array([sway * 0.5, 0, pel[2] - 14.2]), np.array([sway * 0.5, 0, pel[2] - 12.4]), 9.6, 9.5, M((232, 190, 70), spec=0.4), caps=False)
        # cloak down the back
        f.box(T([0, -4.8, 8]), [6.0, 0.7, 10], M((54, 34, 76), noise=0.1), rot=Rt)
        f.ell(T([0, -3.6, 18]), [6.6, 2.2, 2.0], M((54, 34, 76), noise=0.1), rot=Rt)

    # bare-handed casting: both hands, with the fire between them
    def cast(self, ps, r, l, orb=None, flame=False):
        ps['tgtR'] = tuple(r)
        ps['tgtL'] = tuple(l)
        ps['_orb'] = (orb, flame) if orb else None
        return ps

    def decorate_gun(self, f, J, ps, flash=False):
        o = ps.get('_orb')
        if not o:
            return
        (c, rad), flame_ = o
        w = J['pelvis'] + J['Rt'] @ np.array(c, float)
        f.sph(w, rad, GLOW(240, 110, 30))
        f.sph(w, rad * 0.72, GLOW(255, 190, 70))
        f.sph(w, rad * 0.4, GLOW(255, 250, 215))
        for k in range(6):
            a = k * math.pi / 3
            base = w + J['Rt'] @ np.array([math.cos(a) * rad * 0.7, 0, math.sin(a) * rad * 0.7])
            tip = base + J['Rt'] @ np.array([math.cos(a) * rad * 0.5, 1.2 * rad if flame_ else 0.2 * rad, math.sin(a) * rad * 0.5 + 0.4 * rad])
            f.cone(base, tip, rad * 0.34, 0.05, GLOW(244, 120, 28))
        if flame_:
            for k in range(7):
                t = (k + 1) / 7.0
                p = w + J['Rt'] @ np.array([0, 3 + 9 * t * 1.5, 0.4 * t])
                f.sph(p, rad * (0.6 + 0.5 * t), GLOW(*((255, 220, 120), (252, 170, 50), (240, 110, 30), (200, 60, 24))[min(3, int(t * 4))]))

    def stand(self):
        return self.cast(pose(), (-3.0, 7.5, 12.5), (3.0, 7.5, 12.5), ((0, 8.5, 13.2), 1.7))

    def run(self, i):
        d = walk_pose(i, n=6, amp=34, arm=30, lean=6, bob=1.6)
        d.update(elL=40, elR=40)
        return d

    def shoot(self, i):
        z = (0, 1, 2)[min(i, 2)]
        d = pose(lean=(-3, 6, 2)[z], hipL=10, hipR=-8, kneeL=10)
        if z == 0:       # hands drawn in to the chest, the fire gathering
            return self.cast(d, (-2.6, 5.0, 13.5), (2.6, 5.0, 13.5), ((0, 6.0, 14.2), 2.6))
        if z == 1:       # both palms thrust out: the fireball leaves
            return self.cast(d, (-2.8, 12.5, 15.0), (2.8, 12.5, 15.0), ((0, 15.5, 15.8), 4.4))
        return self.cast(d, (-2.8, 11.0, 14.0), (2.8, 11.0, 14.0), None)

    def jump(self, i):
        k = (0, 1, 2)[min(i, 2)]
        d = pose(hipL=(20, 6, 36)[k], hipR=(-8, 10, -22)[k], kneeL=(60, 40, 24)[k], kneeR=(40, 56, 18)[k], lift=(2, 5, 0)[k], lean=(6, 0, -4)[k],
                 shL=(90, 130, 60)[k], shR=(90, 130, 60)[k], abL=(40, 70, 50)[k], abR=(40, 70, 50)[k], elL=20, elR=20)
        return d

    def pain(self, i=0, n=1):
        return pain_pose()

    def figure(self, name, k, n=1):
        if name == 'WPLY':
            return self.play_frame(k)
        if name == 'WRUN':
            return self.build(self.run(k))
        if name == 'WSHT':
            return self.build(self.shoot(k), flash=(k == 1))
        if name == 'WJMP':
            return self.build(self.jump(k))
        raise KeyError(name)


class Swordsman(ClassModel):
    gun = 'sword'
    sp = dict(leg1=13, leg2=13, torso_len=18.5, torso_w=6.4, torso_d=4.4, sh_w=7.8, arm1=9, arm2=9, head_r=4.4,
              pants=M((72, 60, 54), noise=0.1), boots=M((56, 40, 30), spec=0.3, noise=0.1), cloth=M((150, 150, 160), spec=0.5, shin=26, noise=0.05),
              sleeve=M((72, 60, 54), noise=0.1), sleeve2=M((150, 154, 166), spec=0.6, noise=0.04), glove=M((70, 50, 36), noise=0.1),
              skin=M((222, 182, 148), noise=0.05), leg_r=3.0, arm_r=2.4)

    def decorate(self, f, J, ps, cat, i, n):
        Rt, Rh = J['Rt'], J['Rh']
        pel = J['pelvis']
        T = lambda v: pel + Rt @ np.array(v, float)
        H = lambda v: J['head'] + Rh @ np.array(v, float)
        hair = M((22, 20, 26), spec=0.3, shin=20, noise=0.08)
        f.ell(H([0, -0.5, 0.9]), [4.9, 5.0, 4.5], hair, rot=Rh)                          # black hair
        f.ell(H([0, 3.3, -0.2]), [3.5, 1.8, 3.7], M((222, 182, 148), noise=0.04), rot=Rh)  # face
        f.ell(H([0, 3.9, 3.0]), [3.4, 1.0, 0.9], hair, rot=Rh)                           # fringe
        # the ponytail: tied high at the back, swinging with the stride
        sw = ps.get('hipL', 0) * 0.1
        f.cone(H([0, -4.0, 2.2]), H([sw, -6.4, -3.5]), 1.5, 1.0, hair)
        f.cone(H([sw, -6.4, -3.5]), H([sw * 1.5, -6.8, -10.0]), 1.0, 0.25, hair)
        face(f, H, (222, 182, 148), (24, 18, 18), (176, 70, 78), rot=Rh, brow=(20, 16, 18))
        f.box(H([0, 3.9, 2.2]), [3.4, 0.35, 0.4], M((200, 40, 40), noise=0.02), rot=Rh)      # red headband
        for s in (-1, 1):
            f.ell(T([s * 8.8, 0, 17.2]), [3.8, 3.8, 2.6], M((170, 172, 184), spec=0.6, shin=26, noise=0.04), rot=Rt)
        f.box(T([0, 4.8, 11]), [4.6, 0.7, 5.0], M((190, 192, 204), spec=0.6, shin=28, noise=0.04), rot=Rt)
        f.box(T([0, 0, 1.0]), [7.0, 4.8, 1.3], SWAP, rot=Rt)                              # sash
        # the cape (swap colour): hangs from the shoulders, lifts when running
        lift = max(0.0, ps.get('lean', 0)) * 2.2 + abs(ps.get('hipL', 0)) * 0.4
        Rc = Rt @ rot_matrix(rx=lift * 0.9)
        top = pel + Rt @ np.array([0, -3.8, 17])
        f.box(top + Rc @ np.array([0, -0.5, -9]), [6.4, 0.7, 10], SWAP, rot=Rc)
        f.ell(top + Rt @ np.array([0, 0, 0.6]), [7.2, 2.4, 1.8], SWAP2, rot=Rt)

    def stand(self):
        return self.hold(pose(), (-3.4, 6, 8), pitch=-50, two=False)

    def run(self, i):
        d = walk_pose(i, n=6, amp=36, arm=34, lean=8, bob=1.6)
        d.update(elL=40)
        return self.hold(d, (-4.0, 4, 9 + (0.8 if i % 2 else 0)), pitch=-70, two=False)

    def shoot(self, i):
        z = (0, 1, 2)[min(i, 2)]
        if z == 0:    # wind-up: the sword high behind the shoulder
            d = pose(lean=-6, hipL=10, hipR=-8, twist=-25)
            return self.hold(d, (-5, -2, 24), pitch=100, two=False)
        if z == 1:    # the cut
            d = pose(lean=14, hipL=18, hipR=-14, kneeL=20, twist=30)
            return self.hold(d, (1, 12, 14), pitch=-8, yaw=20, two=False)
        d = pose(lean=10, hipL=12, hipR=-8, twist=40)
        return self.hold(d, (4, 10, 4), pitch=-50, yaw=40, two=False)

    def parry(self, i):
        """A: the guard goes up; B: held (the blade across the body in both hands, rising from her right hip to above her left
        shoulder); C: the outward movement after a parried blow (arms and sword thrown out to the front).  A missed parry never shows C."""
        z = (0, 1, 2)[min(i, 2)]
        if z == 2:
            d = pose(lean=4, hipL=10, hipR=-8, twist=8, kneeL=10)
            return self.hold(d, (-3.0, 15, 16.5), pitch=74, yaw=12, two=True)
        d = pose(lean=(-2, -4)[z], hipL=8, hipR=-6, twist=(-6, -14)[z], kneeL=(6, 12)[z])
        return self.hold(d, (-4.5, 10 + z, (11, 12.5)[z]), pitch=(62, 40)[z], yaw=(40, 68)[z], two=True)

    def jump(self, i):
        k = (0, 1, 2)[min(i, 2)]
        d = pose(hipL=(30, 10, 40)[k], hipR=(-10, 14, -25)[k], kneeL=(70, 50, 30)[k], kneeR=(40, 70, 20)[k], lift=(2, 5, 0)[k], lean=(8, 2, -4)[k],
                 shL=(100, 130, 60)[k], abL=(40, 60, 50)[k], elL=30)
        return self.hold(d, (-4, 6, (11, 20, 8)[k]), pitch=(-60, 60, -80)[k], two=False)

    # ---- the stab: the sword drawn back beside the body, pointing forward (still, walking, in the air), then the lunge ----
    DRAWN = (-5.0, 5.5, 9.0)

    def drawn(self, d):
        """the right hand back at the hip, the blade pointing straight ahead beside her; the shoulders turned a little to the sword"""
        d['twist'] = d.get('twist', 0) - 12
        return self.hold(d, self.DRAWN, pitch=-4, yaw=12, two=False)

    def charge_stand(self):
        return self.drawn(pose(lean=4, hipL=7, hipR=-7, kneeL=10, kneeR=6, shL=-18))

    def charge_run(self, i):
        d = walk_pose(i, n=6, amp=36, arm=34, lean=10, bob=1.6)
        d.update(elL=40)
        return self.drawn(d)

    def charge_jump(self, i):
        k = (0, 1, 2)[min(i, 2)]
        d = pose(hipL=(30, 10, 40)[k], hipR=(-10, 14, -25)[k], kneeL=(70, 50, 30)[k], kneeR=(40, 70, 20)[k], lift=(2, 5, 0)[k],
                 lean=(8, 4, 0)[k], shL=(100, 130, 60)[k], abL=(40, 60, 50)[k], elL=30)
        return self.drawn(d)

    def _grounded(self, d, lift=0.0):
        """lower (or raise) the pelvis so the lowest foot is `lift` above the floor"""
        f, J = biped(self.sp, d)
        d['drop'] = d.get('drop', 0) + min(J['footL'][2], J['footR'][2]) - lift
        return d

    def _thrust(self, d, two=True):
        """both hands out in front, the blade horizontal in the world whatever the lean"""
        L = math.radians(d['lean'])
        S = np.array([-self.sp['sh_w'], 0.0, self.sp['torso_len'] - 1.8])
        T = S + 17.0 * np.array([0.0, math.cos(L), math.sin(L)])
        T[0] = -3.2
        return self.hold(d, tuple(T), pitch=d['lean'] - 3, two=two)

    def stab_ground(self):
        """the lunge from the ground: leaning right into it, one foot ahead and one trailing, both still on the floor (dragged along by the blade)"""
        d = pose(lean=34, hipL=-34, hipR=26, kneeL=14, kneeR=18, shL=-30, abL=30, elL=20)
        return self._thrust(self._grounded(d), two=True)

    def stab_air(self):
        """the lunge in the air: leaning forward, legs flexed up behind her (a jump, not a dive)"""
        d = pose(lean=26, hipL=-22, hipR=8, kneeL=78, kneeR=60, lift=0, shL=-30, abL=30, elL=20)
        return self._thrust(self._grounded(d, lift=4.0), two=True)

    def pain(self, i=0, n=1):
        return self.hold(pain_pose(), (-6, 4, 14), pitch=-30, two=False)

    def figure(self, name, k, n=1):
        if name == 'XPLY':
            return self.play_frame(k)
        if name == 'XRUN':
            return self.build(self.run(k))
        if name == 'XSHT':
            return self.build(self.shoot(k))
        if name == 'XJMP':
            return self.build(self.jump(k))
        if name == 'XPAR':
            return self.build(self.parry(k))
        if name == 'X5FI':
            return self.build(self.charge_stand())
        if name == 'X6FI':
            return self.build(self.charge_run(k))
        if name == 'X7FI':
            return self.build(self.charge_jump(k))
        if name == 'X8FI':
            return self.build(self.stab_ground())
        if name == 'X9FI':
            return self.build(self.stab_air())
        raise KeyError(name)


CLASSES = {'S': Soldier(), 'W': Witch(), 'X': Swordsman()}
