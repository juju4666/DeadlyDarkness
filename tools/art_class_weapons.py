"""art_class_weapons.py - the soldier and the witch in third person, standing ready with each weapon and using it.

Sprite families (BGCLASS: `pose stand S1PI 1 4 weapon pistol`, `pose shoot S2PI 3 3 weapon pistol`):
    <class letter> <kind digit> <weapon code>      class S soldier, W witch (X swordsman: stand with the sword only, and her stab: kinds 5-9)
    kind 1 = stand ready (1 frame), kind 2 = shoot (3 frames: A aim / wind-up, B the shot, C follow-through),
    kind 3 = run (6 frames), kind 4 = jump (3 frames), both holding the weapon
Every weapon of BGWEAPON has its own pictures; the muzzle flash brightness comes from the game state, the flash itself is drawn here.
"""
import math
import numpy as np
from art_rig import biped, pose
from art_monsters2 import GLOW, ring
import art_guns

CODES = (('fist', 'FI'), ('chainsaw', 'CS'), ('pistol', 'PI'), ('dualpistols', 'DP'), ('shotgun', 'SG'), ('supershotgun', 'SS'),
         ('chaingun', 'CG'), ('chainfire', 'CF'), ('missile', 'FB'), ('plasma', 'DB'), ('tornado', 'TN'), ('arclightning', 'AL'),
         ('snap', 'SN'), ('bfg', 'TC'), ('hellfire', 'HF'))
WEAPON_OF = {code: w for w, code in CODES}


def families():
    """family name -> (class letter, weapon code, 'stand' | 'shoot')"""
    out = {}
    for c in 'SW':
        for _, code in CODES:
            out['%s1%s' % (c, code)] = (c, code, 'stand')
            out['%s2%s' % (c, code)] = (c, code, 'shoot')
            out['%s3%s' % (c, code)] = (c, code, 'run')
            out['%s4%s' % (c, code)] = (c, code, 'jump')
    out['X1FI'] = ('X', 'FI', 'stand')
    # the swordsman's stab: 5 sword drawn back (1 frame), 6 the same walking (6), 7 the same in the air (3), 8 the lunge, 9 the lunge in the air
    for d, kind in (('5', 'cstand'), ('6', 'crun'), ('7', 'cjump'), ('8', 'lunge'), ('9', 'lungeair')):
        out['X%sFI' % d] = ('X', 'FI', kind)
    return out


FAMILIES = families()


# ------------------------------------------------------------------------------------------------ helpers
def W(J, c):
    return J['pelvis'] + J['Rt'] @ np.array(c, float)


def body(cm, d):
    f, J = biped(cm.sp, d)
    cm.decorate(f, J, d, 'S', 0, 1)
    return f, J


def gun_fig(cm, name, d, tgt, pitch, yaw=0.0, two=True, flash=None, tip=15.0, big=3.6, col=(255, 210, 90)):
    keep = cm.gun
    cm.gun = name
    try:
        cm.hold(d, tgt, pitch=pitch, yaw=yaw, two=two)
    finally:
        cm.gun = keep
    f, J = body(cm, d)
    t, Rg = d['_gun']
    f.extend(art_guns.gun(name).transformed(J['Rt'] @ Rg, t=J['pelvis'] + J['Rt'] @ t))
    if flash:
        c = J['pelvis'] + J['Rt'] @ (t + Rg @ np.array([0, tip + 2.5, 2.0]))
        f.sph(c, big, GLOW(*col))
        f.sph(c, big * 0.55, GLOW(255, 250, 215))
        for k in range(5):
            a = k * 2 * math.pi / 5 + 0.4
            d_ = J['Rt'] @ (Rg @ np.array([math.cos(a) * 0.6, 1.0, math.sin(a) * 0.6]))
            f.cone(c, c + d_ * big * 1.5, big * 0.28, 0.05, GLOW(*col))
    return f


def hands_fig(cm, d, r=None, l=None, extra=None):
    if r is not None:
        d['tgtR'] = tuple(r)
    if l is not None:
        d['tgtL'] = tuple(l)
    f, J = body(cm, d)
    if extra:
        extra(f, J)
    return f


def orb(f, J, c, r, outer=(240, 110, 30), mid=(255, 190, 70), core=(255, 250, 215)):
    w = W(J, c)
    f.sph(w, r, GLOW(*outer))
    f.sph(w, r * 0.72, GLOW(*mid))
    f.sph(w, r * 0.4, GLOW(*core))
    for k in range(6):
        a = k * math.pi / 3
        base = w + J['Rt'] @ np.array([math.cos(a) * r * 0.7, 0, math.sin(a) * r * 0.7])
        tip = base + J['Rt'] @ np.array([math.cos(a) * r * 0.5, 0.2 * r, math.sin(a) * r * 0.5 + 0.4 * r])
        f.cone(base, tip, r * 0.3, 0.05, GLOW(*outer))


def jet(f, J, c, length, r0, seed=3):
    rs = np.random.RandomState(seed)
    ramp = [(255, 250, 200), (255, 226, 110), (252, 170, 50), (240, 110, 30), (200, 60, 24)]
    n = 12
    for k in range(n):
        t = k / (n - 1.0)
        p = W(J, (c[0] + rs.uniform(-1, 1) * t * 2.0, c[1] + t * length, c[2] + t * 2 + rs.uniform(-1, 1) * t * 1.6))
        f.sph(p, r0 * (0.7 + 1.7 * t), GLOW(*ramp[min(4, int(t * 5))]))


def bolt(f, a, b, seed, segs=5, r=0.4, spread=1.3, col=(200, 228, 255)):
    rs = np.random.RandomState(seed)
    a, b = np.array(a, float), np.array(b, float)
    pts = [a]
    for s in range(1, segs):
        t = s / float(segs)
        pts.append(a + (b - a) * t + rs.uniform(-spread, spread, 3) * (1 - abs(0.5 - t) * 0.8))
    pts.append(b)
    for p, q in zip(pts[:-1], pts[1:]):
        f.cone(p, q, r, r * 0.8, GLOW(*col))


def whirl(f, J, c, height, radius, turns, n, phase, seed=5):
    rs = np.random.RandomState(seed)
    ramp = [(235, 250, 255), (190, 235, 250), (150, 210, 240), (120, 180, 225)]
    base = W(J, c)
    for k in range(n):
        t = k / float(max(1, n - 1))
        a = phase + t * turns * 2 * math.pi
        rr = 0.5 + t * radius
        p = base + np.array([math.cos(a) * rr, math.sin(a) * rr * 0.7, t * height])
        f.sph(p, 0.7 + t * 0.8 + rs.uniform(0, 0.25), GLOW(*ramp[min(3, int(t * 4))]))


def spark(f, J, c, r=1.6):
    w = W(J, c)
    f.sph(w, r, GLOW(255, 250, 200))
    for k in range(6):
        a = k * math.pi / 3 + 0.3
        f.cone(w, w + np.array([math.cos(a) * r * 3.2, math.sin(a) * r * 1.2, math.sin(a + 1.0) * r * 3.2]), r * 0.4, 0.05, GLOW(255, 235, 120))


# ------------------------------------------------------------------------------------------------ weapons
def _p(d0):
    """the body pose: the plain standing one, or (running, jumping) the class's own legs and arms without what it holds"""
    if not d0:
        return pose()
    d = dict(d0)
    for k in ('_gun', 'tgtR', 'tgtL'):
        d.pop(k, None)
    return d


# third person shoot poses with a lead-up: weapon -> (frames, tics a frame): lead-up (3), the cast, after
LEAD_POSE = {'bfg': (5, 12), 'missile': (5, 6)}
CLAP_FRAMES = 5


def z3(i):
    return (0, 1, 2)[min(i, 2)]


def w_fist(cm, shoot, i, d0=None):
    """the knife"""
    if not shoot:
        return gun_fig(cm, 'knife', _p(d0), (-3.6, 7, 11), pitch=-35, two=False)
    z = z3(i)
    if z == 0:
        return gun_fig(cm, 'knife', pose(lean=-6, hipL=10, hipR=-8, twist=-25), (-5, -2, 22), pitch=100, two=False)
    if z == 1:
        return gun_fig(cm, 'knife', pose(lean=14, hipL=18, hipR=-14, kneeL=20, twist=30), (1, 12, 14), pitch=-8, yaw=20, two=False)
    return gun_fig(cm, 'knife', pose(lean=10, hipL=12, hipR=-8, twist=40), (4, 10, 4), pitch=-50, yaw=40, two=False)


def w_chainsaw(cm, shoot, i, d0=None):
    if not shoot:
        return gun_fig(cm, 'chainsaw', _p(d0), (-3.4, 8, 10.5), pitch=4)
    z = i % 3
    j = (0.5, -0.5, 0.0)[z]
    return gun_fig(cm, 'chainsaw', pose(lean=4, hipL=6, hipR=-4), (-3.4 + j, 9, 11.0 + (0.6, -0.6, 0.2)[z]), pitch=8 + 3 * j)


def w_pistol(cm, shoot, i, d0=None):
    if not shoot:
        return gun_fig(cm, 'revolver', _p(d0), (-3.4, 7, 11.5), pitch=-10, two=False)
    z = z3(i)
    if z == 1:
        return gun_fig(cm, 'revolver', pose(lean=-3), (-3.4, 8, 15), pitch=14, two=False, flash=True, tip=15)
    return gun_fig(cm, 'revolver', _p(d0), (-3.4, 9.5, 14.5), pitch=(3, 0, 5)[z], two=False)


def w_dualpistols(cm, shoot, i, d0=None):
    if not shoot:
        return gun_fig(cm, 'revolver', _p(d0), (-3.0, 8, 12.5), pitch=-4, two=True)
    z = z3(i)
    d = pose(lean=(0, -5, -2)[z], hipL=8, hipR=-6)
    return gun_fig(cm, 'revolver', d, (-3.0, (10, 8, 9.5)[z], (14.5, 15.5, 15)[z]), pitch=(2, 16, 6)[z], two=True, flash=(z == 1), tip=15)


def w_shotgun(cm, shoot, i, d0=None):
    if not shoot:
        return gun_fig(cm, 'tripleshotgun', _p(d0), (-3.4, 9, 13.5), pitch=-6, two=True)
    z = z3(i)
    d = pose(lean=(-2, -8, -3)[z])
    return gun_fig(cm, 'tripleshotgun', d, (-3.4, 8 - 1.5 * z, 14.5), pitch=(2, 10, 4)[z], two=True, flash=(z == 1), tip=31)


def w_supershotgun(cm, shoot, i, d0=None):
    if not shoot:
        return gun_fig(cm, 'tripleshotgun', pose(hipL=6, hipR=-4), (-3.4, 8.5, 12.5), pitch=-10, two=True)
    z = z3(i)
    if z == 2:      # broken open to reload: the barrels tipped down
        return gun_fig(cm, 'tripleshotgun', pose(lean=-4, hipL=6, hipR=-4), (-3.4, 7.5, 11.0), pitch=-38, two=True)
    d = pose(lean=(-2, -13, -6)[z], hipL=8, hipR=-6)
    return gun_fig(cm, 'tripleshotgun', d, (-3.4, (9, 6.5)[z], 14.5), pitch=(2, 20)[z], two=True, flash=(z == 1), tip=31, big=5.2)


def w_chaingun(cm, shoot, i, d0=None):
    if not shoot:
        return gun_fig(cm, 'chaingun', _p(d0), (-3.4, 9.5, 12.5), pitch=-2, two=True)
    z = z3(i)
    return gun_fig(cm, 'chaingun', pose(lean=(0, -2, -1)[z]), (-3.4, 9.5 - 0.5 * z, 13.5), pitch=(0, 3, 2)[z], two=True,
                   flash=(z > 0), tip=26, big=2.8)


def w_chainfire(cm, shoot, i, d0=None):
    d = pose(hipL=12, hipR=-10, kneeL=14, kneeR=8, lean=5)
    if not shoot:
        return gun_fig(cm, 'chaingun', d, (-3.0, 8.5, 12.5), pitch=0, two=True)
    z = z3(i)
    d['lean'] = (5, 3, 4)[z]
    return gun_fig(cm, 'chaingun', d, (-3.0, 8.5 - 0.6 * (z == 1), 12.8), pitch=(0, 2, 1)[z], two=True, flash=True, tip=26, big=(3.2, 4.0, 3.4)[z])


def w_hellfire(cm, shoot, i, d0=None):
    if not shoot:
        return gun_fig(cm, 'alienlauncher', _p(d0), (-3.4, 8, 14), pitch=10, two=True)
    z = z3(i)
    d = pose(lean=(0, -8, -3)[z])
    return gun_fig(cm, 'alienlauncher', d, (-3.2, (9.5, 8, 9)[z], 15.5), pitch=(6, 12, 8)[z], two=True, flash=(z == 1), tip=24,
                   big=4.6, col=(240, 80, 180))


def w_missile(cm, shoot, i, d0=None):
    """the fireball: a ball of fire in the right hand, growing while the arms are drawn back (0-2), flung forward (3), after (4)"""
    if not shoot:
        return hands_fig(cm, _p(d0), r=(-3.2, 7.5, 12), extra=lambda f, J: orb(f, J, (-3.2, 8.6, 13.0), 1.4))
    if i <= 2:
        r = (1.6, 2.2, 2.8)[i]
        return hands_fig(cm, pose(lean=-(2 + i)), r=(-3.4, 5 - i * 0.7, 14.5), l=(3, 8 - i, 14),
                         extra=lambda f, J: orb(f, J, (-3.4, 6.2 - i * 0.7, 15.5), r))
    if i == 3:
        return hands_fig(cm, pose(lean=6, hipL=10, hipR=-8), r=(-2.5, 13, 15.5), l=(3.5, 5, 13),
                         extra=lambda f, J: orb(f, J, (-2.5, 16.5, 16), 4.4))
    return hands_fig(cm, pose(lean=4, hipL=8, hipR=-6), r=(-2.5, 11.5, 14.5), l=(3.5, 5, 13),
                     extra=lambda f, J: [spark(f, J, (-2.5, 14, 15.5), 1.0), spark(f, J, (-1.2, 15, 17), 0.7)])


def w_plasma(cm, shoot, i, d0=None):
    """devil's breath: both palms forward, a stream of flame"""
    if not shoot:
        return hands_fig(cm, _p(d0), r=(-2.8, 7.5, 12), l=(2.8, 7.5, 12),
                         extra=lambda f, J: f.sph(W(J, (0, 8.3, 12.6)), 1.2, GLOW(255, 190, 70)))
    z = z3(i)
    d = pose(lean=(-2, 3, 2)[z], hipL=6, hipR=-5)
    if z == 0:
        return hands_fig(cm, d, r=(-2.4, 10, 14.5), l=(2.4, 10, 14.5), extra=lambda f, J: orb(f, J, (0, 11, 15), 1.6))
    return hands_fig(cm, d, r=(-2.4, 11, 14.5), l=(2.4, 11, 14.5),
                     extra=lambda f, J: jet(f, J, (0, 12, 14.5), (20, 14)[z - 1], 1.2, seed=3 + z))


def w_tornado(cm, shoot, i, d0=None):
    if not shoot:
        return hands_fig(cm, _p(d0), r=(-3.5, 6, 12.5), l=(3.5, 6, 12.5), extra=lambda f, J: whirl(f, J, (0, 6.5, 11.5), 4, 1.8, 2, 8, 0.0))
    z = z3(i)
    if z == 0:
        return hands_fig(cm, pose(lean=-2), r=(-5, 6.5, 13), l=(5, 6.5, 13), extra=lambda f, J: whirl(f, J, (0, 7, 11), 7, 2.6, 2.2, 14, 0.5))
    if z == 1:
        return hands_fig(cm, pose(lean=-3, hipL=8, hipR=-6), r=(-6.5, 6.5, 14), l=(6.5, 6.5, 14),
                         extra=lambda f, J: whirl(f, J, (0, 7, 10), 11, 4.2, 2.6, 20, 1.8))
    return hands_fig(cm, pose(lean=7, hipL=12, hipR=-10), r=(-3, 12.5, 14.5), l=(3, 12.5, 14.5),
                     extra=lambda f, J: whirl(f, J, (0, 16, 12.5), 4, 2.6, 2.0, 12, 2.6))


def w_arclightning(cm, shoot, i, d0=None):
    col = (190, 225, 255)
    if not shoot:
        return hands_fig(cm, _p(d0), r=(-3.4, 7, 12.5), l=(3.4, 7, 12.5),
                         extra=lambda f, J: bolt(f, W(J, (-3.4, 8, 13.5)), W(J, (3.4, 8, 13.5)), 4, segs=4, r=0.3))
    z = z3(i)
    d = pose(lean=(0, 4, 2)[z], hipL=8, hipR=-6)

    def fx(f, J):
        for sgn in (-1, 1):
            h = W(J, (sgn * 4.0, 10.5, 14.0))
            if z == 0:
                bolt(f, h, h + np.array([sgn * 2.5, 3.5, 2.5]), 10 + sgn, segs=3, r=0.35)
            else:
                n = (5, 3)[z - 1]
                for k in range(n):
                    ang = math.radians(-50 + 100 * k / max(1, n - 1)) * 0.9
                    ln = (20, 11)[z - 1]
                    bolt(f, h, W(J, (math.sin(ang) * ln, 10.5 + math.cos(ang) * ln, 14.0 + 3 * (1 - abs(ang)))), 30 + k + (sgn + 1) * 7, segs=5, r=0.5)
    return hands_fig(cm, d, r=(-4.0, 10.5, 14.0), l=(4.0, 10.5, 14.0), extra=fx)


def w_snap(cm, shoot, i, d0=None):
    two = cm.__class__.__name__ == 'Witch'
    if not shoot:
        return hands_fig(cm, _p(d0), r=(-3.4, 6.5, 12), l=(3.4, 6.5, 12) if two else None)
    z = z3(i)
    rz = (15, 16, 14)[z]
    ry = (6, 7, 6.5)[z]

    def fx(f, J):
        if z == 1:
            spark(f, J, (-3.2, 8, 16.6), 1.5)
            if two:
                spark(f, J, (3.2, 8, 16.6), 1.5)
    return hands_fig(cm, pose(lean=(-1, 2, 0)[z]), r=(-3.2, ry, rz), l=(3.2, ry, rz) if two else None, extra=fx)


def w_bfg(cm, shoot, i, d0=None):
    """thunderclap: hands clasped as in prayer (idle); drawn slowly apart with sparks between them (0-2); the clap and its ring (3); after (4)"""
    if not shoot:
        return hands_fig(cm, _p(d0), r=(-0.9, 7.5, 13.0), l=(0.9, 7.5, 13.0))
    if i <= 2:
        gap = (3.5, 6.5, 9.5)[i]

        def fx(f, J):
            spark(f, J, (0, 7.5, 14.0), 1.0 + 0.5 * i)
            if i >= 1:
                spark(f, J, (-gap * 0.5, 7.5, 15.5), 0.9)
                spark(f, J, (gap * 0.5, 7.5, 12.5), 0.9)
            if i == 2:
                spark(f, J, (0, 8.0, 16.5), 1.1)
        return hands_fig(cm, pose(lean=-(1 + i)), r=(-gap, 6.5, 14.0), l=(gap, 6.5, 14.0), extra=fx)

    def ringfx(R, n, r, core):
        def fx(f, J):
            c = W(J, (0, 9.5, 14.5))
            ring(f, c, R, n, r, GLOW(235, 245, 255), axis='z')
            ring(f, c, R * 0.6, n // 2, r * 0.9, GLOW(160, 200, 255), axis='z')
            if core:
                f.sph(c, 1.8, GLOW(255, 255, 255))
        return fx
    if i == 3:
        return hands_fig(cm, pose(lean=5, hipL=8, hipR=-6), r=(-0.9, 9.5, 14.5), l=(0.9, 9.5, 14.5), extra=ringfx(6, 14, 1.0, True))
    return hands_fig(cm, pose(lean=3), r=(-2.4, 8.5, 14), l=(2.4, 8.5, 14), extra=ringfx(11, 18, 0.7, False))


BUILD = {'fist': w_fist, 'chainsaw': w_chainsaw, 'pistol': w_pistol, 'dualpistols': w_dualpistols, 'shotgun': w_shotgun,
         'supershotgun': w_supershotgun, 'chaingun': w_chaingun, 'chainfire': w_chainfire, 'missile': w_missile, 'plasma': w_plasma,
         'tornado': w_tornado, 'arclightning': w_arclightning, 'snap': w_snap, 'bfg': w_bfg, 'hellfire': w_hellfire}


def figure(cm, fam, k):
    cls, code, kind = FAMILIES[fam]
    if cls == 'X':          # the swordsman waiting with the sword drawn (her attack and parry have their own sets)
        if kind == 'cstand':
            return cm.build(cm.charge_stand())
        if kind == 'crun':
            return cm.build(cm.charge_run(k))
        if kind == 'cjump':
            return cm.build(cm.charge_jump(k))
        if kind == 'lunge':
            return cm.build(cm.stab_ground())
        if kind == 'lungeair':
            return cm.build(cm.stab_air())
        return cm.build(cm.stand())
    if kind == 'run':
        return BUILD[WEAPON_OF[code]](cm, False, 0, cm.run(k))
    if kind == 'jump':
        return BUILD[WEAPON_OF[code]](cm, False, 0, cm.jump(k))
    return BUILD[WEAPON_OF[code]](cm, kind == 'shoot', k)
