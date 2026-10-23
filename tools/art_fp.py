"""art_fp.py - first-person weapons.  Every weapon is drawn on one fixed canvas: the grip (or the hands) sits at a fixed place on the
320x200 screen, so a weapon's frames line up with each other and the muzzle flash lands on the muzzle.

Scene coordinates are written the way the player sees them (x to the right, y away from the player, z up) and mirrored into the
renderer's frame by P().  Every lump's hot spot comes from where the origin pixel falls on the screen (see lump_offsets)."""
import math
import numpy as np
from PIL import Image
from art_render import Fig, mat, rot_matrix, render
from art_monsters import M
from art_monsters2 import GLOW
import art_guns
import art_hands
import art_fx as X

PITCH = 12.0
ELEV = 24.0
FACE = 168.0
BG = 2.5                   # hi-res pixels per screen unit (320x200 space)
# hi-res pixels per scene unit and the camera yaw, per weapon.  Doom keeps its guns in the bottom fifth to quarter of the screen,
# so these are small; the view is 168 units high (the status bar covers the rest).
PPU_OF = {'PISG': 4.9, 'SHTG': 4.2, 'SHT2': 4.2, 'CHGG': 4.4, 'SAWG': 4.6, 'MISG': 8.4, 'PLSG': 8.4, 'BFGG': 8.4, 'SNPG': 9.0, 'PUNG': 6.6,
          'HLFG': 5.0, 'ARCG': 8.4, 'TRNG': 8.4}
FACE_OF = {'PISG': 172.0, 'SHTG': 166.0, 'SHT2': 166.0, 'CHGG': 168.0, 'SAWG': 170.0, 'HLFG': 164.0, 'PUNG': 180.0}
PPU = 4.0

def P(x, y, z):
    return np.array([-x, y, z], float)


def Pv(v):
    return P(*v)


# ---------------------------------------------------------------------------------------------- families
# first-person family -> (base weapon, class): class N = whoever has no hands of their own (the swordsman), S soldier, W witch
BASES = {'TRNG': 'TRNG', 'HLFG': 'HLFG', 'ARCG': 'ARCG', 'PISG': 'PISG', 'SHTG': 'SHTG', 'SHT2': 'SHT2', 'CHGG': 'CHGG', 'MISG': 'MISG', 'SAWG': 'SAWG', 'PLSG': 'PLSG', 'BFGG': 'BFGG',
         'PUNG': 'PUNG', 'SNPG': 'SNPG'}
FAMILIES = {}
for b in BASES:
    FAMILIES[b] = (b, 'X' if b == 'PUNG' else 'N')
for stock, (s_name, w_name) in {'PISG': ('SPIS', 'WPIS'), 'SHTG': ('SSHG', 'WSHG'), 'SHT2': ('SSH2', 'WSH2'), 'CHGG': ('SCHG', 'WCHG'),
                                'MISG': ('SMSG', 'WMSG'), 'SAWG': ('SSAW', 'WSAW'), 'PLSG': ('SPLG', 'WPLG'), 'BFGG': ('SBFG', 'WBFG'),
                                'PUNG': ('SKNF', 'WPUN'), 'SNPG': ('SSNP', 'WSNP'), 'HLFG': ('SHLG', 'WHLG'), 'ARCG': ('SARC', 'WARC'), 'TRNG': ('STRN', 'WTRN')}.items():
    FAMILIES[s_name] = (stock, 'S')
    FAMILIES[w_name] = (stock, 'W')
FAMILIES['XPUN'] = ('PUNG', 'X')

FLASHES = {'PISF': 'gold', 'SHTF': 'gold', 'CHGF': 'gold', 'MISF': 'fire', 'PLSF': 'fire', 'BFGF': 'plasma', 'HLFF': 'magic'}
FLASH_OF = {'PISF': 'PISG', 'SHTF': 'SHTG', 'CHGF': 'CHGG', 'MISF': 'MISG', 'PLSF': 'PLSG', 'BFGF': 'BFGG', 'HLFF': 'HLFG'}
NFRAMES = {'TRNG': 4, 'HLFG': 2, 'ARCG': 3, 'PISG': 5, 'SHTG': 4, 'SHT2': 10, 'CHGG': 2, 'MISG': 6, 'SAWG': 4, 'PLSG': 2, 'BFGG': 9, 'PUNG': 7, 'SNPG': 4}

# where the origin (the right hand's grip, or the middle of the hands) sits on the 320x200 screen
ANCHOR = {'PISG': (172, 166), 'SHTG': (182, 170), 'SHT2': (182, 170), 'CHGG': (178, 170), 'SAWG': (172, 170), 'MISG': (160, 158),
          'PLSG': (160, 158), 'BFGG': (160, 158), 'PUNG': (196, 168), 'SNPG': (172, 156), 'HLFG': (186, 170), 'ARCG': (160, 158), 'TRNG': (160, 158)}

SKIN = {'N': (222, 178, 140), 'S': (98, 62, 42), 'W': (240, 203, 176), 'X': (222, 178, 140)}
SLEEVE = {'N': (150, 152, 164), 'S': (44, 112, 128), 'W': (74, 48, 100), 'X': (150, 152, 164)}
GLOVE = {'N': (60, 52, 46), 'S': (44, 44, 48), 'W': None, 'X': (60, 52, 46)}        # None: bare hands


# ---------------------------------------------------------------------------------------------- scene helpers
class Scene:
    def __init__(self, cls):
        self.f = Fig()
        self.cls = cls
        self.skin = M(SKIN[cls], noise=0.06)
        self.slv = M(SLEEVE[cls], noise=0.08)
        g = GLOVE[cls]
        self.glove = M(g, noise=0.04) if g else self.skin
        self.marks = []                  # muzzle points, scene frame
        self.pre = []                    # figs drawn before the pitch (transform shared with the marks)

    def arm(self, wrist, back, r=3.2, bare=0.3):
        """a forearm that comes from (wrist + back) to the wrist; the sleeve covers the upper part"""
        wrist = np.asarray(wrist, float)
        back = np.asarray(back, float)
        mid = wrist + back * bare
        self.f.limb(P(*(wrist + back)), P(*mid), r * 1.1, r, self.slv)
        self.f.limb(P(*mid), P(*wrist), r, r * 0.78, self.skin)

    def hand(self, base, fwd, up, left=False, curl=0.2, spread=0.1, thumb=0.5, size=1.55, finger_curl=None, skin=None):
        art_hands.hand(self.f, P(*base), Pv(fwd), Pv(up), skin or self.skin, curl=curl, spread=spread, thumb=thumb, size=size,
                       left=not left, finger_curl=finger_curl)

    def gloved(self, c, r=3.0):
        self.f.sph(P(*c), r, self.glove)


def pitched(f):
    return f.transformed(rot_matrix(rx=PITCH))


def finish(sc):
    return pitched(sc.f)


def project(points, base, size=0.8):
    """pixel offsets (dx, dy) from the render origin of scene points, after the pitch"""
    out = []
    for p in points:
        m = Fig()
        m.sph(P(*p), size, M((255, 255, 255), flat=True, noise=0))
        m = pitched(m)
        img, ox, oy = render(m, FACE_OF.get(base, 180.0), ppu=PPU_OF[base], ss=1, elev=ELEV, outline=0)
        out.append((img.width / 2.0 - ox, img.height / 2.0 - oy))
    return out


# ---------------------------------------------------------------------------------------------- guns held
def place_gun(gname, sc_, kick, rot_extra=None, t_extra=(0, 0, 0), **kw):
    """the model, scaled, kicked (rx tilt and a slide back) and optionally moved; returns (Fig, transform for points)"""
    g = art_guns.gun(gname, **kw)
    for p in g.prims:
        p['c'] = p['c'] * sc_
        p['s'] = p['s'] * sc_
    R = rot_matrix(rx=-kick * 14)
    t = np.array([0, -kick * 4, kick * 3.0])

    def xf(pt):
        pt = np.asarray(pt, float) * sc_
        pt = R @ pt + t
        if rot_extra is not None:
            pt = rot_extra @ pt
        return pt + np.array(t_extra, float)
    g = g.transformed(R, t=t)
    if rot_extra is not None:
        g = g.transformed(rot_extra)
    if any(t_extra):
        g = g.transformed(np.eye(3), t=t_extra)
    # the gun model is in the renderer's frame already (symmetric about x = 0), so no mirroring is needed
    return g, xf


def holding_gun(sc, gname, scale, kick, two_hands, support=None, **kw):
    g, xf = place_gun(gname, scale, kick, **kw)
    sc.f.extend(g)
    hand = np.array([0.0, 0.5, 0.0])
    sc.arm(hand, np.array([5, -14, -11.0]))
    sc.gloved(hand + np.array([0, 0.6, 0.2]), 3.1)
    if two_hands:
        sup = np.array(support if support is not None else art_guns.GUNS[gname][1], float) * scale
        sup = sup + np.array([0, -kick * 4, kick * 3.0])
        sc.arm(sup, np.array([-6, -12, -9.0]))
        sc.gloved(sup + np.array([0, 0.2, -0.4]), 3.0)
    return xf


# ---------------------------------------------------------------------------------------------- per weapon frames
def recoil_of(base, fi):
    prof = {'PISG': [0, 1, 0.6, 0.25, 0.1], 'SHTG': [0, 1, 0.45, 0.15], 'SHT2': [0, 1, 0.8, 0.2, 0.2, 0.2, 0.1, 0, 0, 0],
            'CHGG': [0, 0.7], 'SAWG': [0, 0.3, 0, 0.3]}.get(base)
    if prof is None:
        return 0
    return prof[min(fi, len(prof) - 1)]


def shells_at(sc, pts, tilt=0.0):
    for (x, y, z) in pts:
        sc.f.cone(P(x, y, z), P(x, y + 0.6 * math.cos(tilt) + 2.6, z + 2.6 * math.sin(tilt) + 0.5), 0.95, 0.95, M((226, 70, 44), spec=0.3))
        sc.f.cone(P(x, y, z), P(x, y + 0.1, z + 0.2), 0.96, 0.96, M((222, 176, 70), spec=0.6))


def scene_PISG(cls, fi):
    sc = Scene(cls)
    kick = recoil_of('PISG', fi)
    xf = holding_gun(sc, 'revolver', 2.7, kick, two_hands=(cls == 'W'), support=(0, 3, -1.5))
    muzzle = xf((0, 15.6, 2.9))
    return sc, [muzzle], kick


def scene_SHTG(cls, fi):
    sc = Scene(cls)
    kick = recoil_of('SHTG', fi)
    rot = {0: 0, 1: 0, 2: 45, 3: 90}.get(fi, 0)
    xf = holding_gun(sc, 'tripleshotgun', 1.55, kick, two_hands=True, support=(0, 13, -0.8), spin=rot)
    return sc, [xf((0, 31.6, 4.25))], kick


# SHT2: A ready, B all three fire, C recoil, D break open, E open + shells fly out, F empty + reaching with shells,
# G shells going in, H closing, I J flashes
SHT2_POSE = {0: (0, 0.0, 0, 0), 1: (1.0, 0.0, 0, 0), 2: (0.8, 0.0, 0, 0), 3: (0.4, 22.0, 0, 12), 4: (0.2, 40.0, 1, 24), 5: (0.2, 40.0, 2, 24),
             6: (0.1, 34.0, 3, 20), 7: (0.0, 14.0, 0, 6), 8: (0.0, 0.0, 0, 0), 9: (0.0, 0.0, 0, 0)}


def scene_SHT2(cls, fi):
    sc = Scene(cls)
    kick, op, stage, lift = SHT2_POSE.get(fi, (0, 0, 0, 0))
    sup_pt = (0, 13, -0.8)
    xf0 = None
    g, xf = place_gun('tripleshotgun', 1.55, kick, open_=op, rot_extra=rot_matrix(rx=-lift) if lift else None)
    sc.f.extend(g)
    hand = np.array([0.0, 0.5, 0.0])
    sc.arm(hand, np.array([5, -14, -11.0]))
    sc.gloved(hand + np.array([0, 0.6, 0.2]), 3.1)
    s = 1.55
    if stage == 0:                                         # left hand under the barrels
        sup = np.array(sup_pt, float) * s + np.array([0, -kick * 4, kick * 3.0])
        sc.arm(sup, np.array([-6, -12, -9.0]))
        sc.gloved(sup + np.array([0, 0.2, -0.4]), 3.0)
    elif stage == 1:                                        # steadying the broken gun; shells flying out of the breech
        sup = np.array([-5.0, 2.0, -3.0]) * 1.0
        sc.arm(sup, np.array([2, -12, -9.0]))
        sc.gloved(sup, 3.0)
        shells_at(sc, [(-3, 9, 15), (0, 5, 20), (3.5, 11, 12)], tilt=0.9)
    elif stage == 2:                                        # the left hand brings three shells up from below
        sup = np.array([-6.0, 5.0, -2.0])
        sc.arm(sup, np.array([-4, -12, -9.0]))
        sc.hand(sup + np.array([0, 2, 1]), (0.3, 1, 0.4), (0, 0, -1), curl=0.75, spread=0.0, thumb=0.2, left=True)
        shells_at(sc, [(-7, 7.5, 1.0), (-5.5, 7.5, 2.4), (-4, 7.5, 1.0)], tilt=1.2)
    elif stage == 3:                                        # pushing the shells in
        sup = np.array([-3.0, 5.0, 3.0])
        sc.arm(sup, np.array([-5, -12, -9.0]))
        sc.hand(sup + np.array([0, 1.5, 0.5]), (0.2, 1, 0.1), (0, 0, -1), curl=0.5, spread=0.0, thumb=0.3, left=True)
        shells_at(sc, [(-2.2, 8.5, 4.2), (0, 8.5, 5.2), (2.2, 8.5, 4.2)], tilt=0.5)
    return sc, [xf((0, 31.6, 4.25)), xf((1.33, 31.6, 1.9 + 0.0)), xf((-1.33, 31.6, 1.9))], kick


def scene_CHGG(cls, fi):
    sc = Scene(cls)
    kick = recoil_of('CHGG', fi)
    xf = holding_gun(sc, 'chaingun', 1.5, kick, two_hands=True)
    return sc, [xf((0, 26, 1.0))], kick


def scene_SAWG(cls, fi):
    sc = Scene(cls)
    kick = recoil_of('SAWG', fi) * 0.5
    g, xf = place_gun('chainsaw', 1.5, 0, t_extra=(0, 0, 0.8 * (fi % 2)))
    sc.f.extend(g)
    hand = np.array([0.0, 0.5, 0.0])
    sc.arm(hand, np.array([5, -14, -11.0]))
    sc.gloved(hand + np.array([0, 0.6, 0.2]), 3.1)
    sup = np.array([0.0, 9 * 1.5, 3])
    sc.arm(sup, np.array([6, -12, -8.0]))
    sc.gloved(sup, 3.0)
    return sc, [], 0


def fire_orb(sc, c, r, big=False, seed=1):
    """a ball of fire made of glowing spheres, with tongues of flame"""
    c = np.asarray(c, float)
    sc.f.sph(P(*c), r, GLOW(240, 110, 30))
    sc.f.sph(P(*(c + np.array([0, -0.45 * r, 0.1 * r]))), r * 0.72, GLOW(255, 190, 70))
    sc.f.sph(P(*(c + np.array([0, -0.85 * r, 0.15 * r]))), r * 0.4, GLOW(255, 250, 215))
    rs = np.random.RandomState(seed)
    for k in range(7 if big else 5):
        a = rs.uniform(0, 2 * math.pi)
        base = c + np.array([math.cos(a) * r * 0.7, rs.uniform(-0.3, 0.3) * r, math.sin(a) * r * 0.5 + r * 0.2])
        tip = base + np.array([math.cos(a) * r * 0.2, rs.uniform(-0.2, 0.4) * r, r * rs.uniform(0.7, 1.4)])
        sc.f.cone(P(*base), P(*tip), r * 0.38, 0.05, GLOW(244, 120, 28))


def scene_MISG(cls, fi):
    """fireball: 0 cupped hands holding a small ball of fire (ready); 1-3 the ball growing as the hands draw apart;
    4 both palms push it forward (the cast); 5 after"""
    sc = Scene(cls)
    if fi <= 3:
        spread = (0.0, 0.5, 1.2, 2.0)[fi]
        for sgn in (1, -1):
            sc.arm((sgn * (5.5 + spread), 3.5, -2.5), (sgn * 6, -14, -9.0))
            sc.hand((sgn * (4.6 + spread), 5.5, -2.0), (-sgn * 0.45, 1, 0.5), (0, 0, -1), left=(sgn < 0), curl=0.55, spread=0.1, thumb=0.4)
        fire_orb(sc, (0, 7.5, 1.8), (3.4, 2.0, 3.2, 4.4)[fi], big=fi == 3, seed=2 + fi)
        mz = (0, 9.5, 2.0)
    else:
        for sgn in (1, -1):
            sc.arm((sgn * 4.2, 4.5, 0.8), (sgn * 4, -14, -9.0))
            sc.hand((sgn * 3.6, 6.5, 1.8), (-sgn * 0.15, 0.7, 1), (0, -1, 0.3), left=(sgn < 0), curl=0.05, spread=0.3, thumb=0.8)
        if fi == 4:
            fire_orb(sc, (0, 13.5, 4.0), 5.4, big=True, seed=4)
            mz = (0, 15.0, 4.0)
        else:
            fire_orb(sc, (0, 16.5, 4.5), 4.6, big=True, seed=5)
            mz = (0, 18.0, 4.5)
    return sc, [mz], 1.0 if fi == 4 else 0.0


def scene_PLSG(cls, fi):
    """devil's breath: open palms held out; on the shot a jet of fire pours from them"""
    sc = Scene(cls)
    for sgn in (1, -1):
        sc.arm((sgn * 4.4, 3.0, 0.5), (sgn * 5, -14, -9.0))
        sc.hand((sgn * 3.8, 5.0, 1.2), (-sgn * 0.1, 0.55, 1), (0, -1, 0.35), left=(sgn < 0), curl=0.1, spread=0.55, thumb=0.9)
    if fi == 0:
        for sgn in (1, -1):
            sc.f.cone(P(sgn * 3.4, 6.6, 3.6), P(sgn * 3.4, 7.4, 7.5), 1.2, 0.05, GLOW(244, 120, 28))
        sc.f.cone(P(0, 6.5, 3), P(0, 7.5, 8.5), 1.4, 0.05, GLOW(255, 190, 70))
        mz = (0, 8.0, 5.0)
    else:
        rs = np.random.RandomState(9)
        ramp = [(255, 250, 200), (255, 226, 110), (252, 170, 50), (240, 110, 30), (200, 60, 24)]
        for k in range(16):
            t = k / 15.0
            c = ramp[min(4, int(t * 5))]
            y = 7.5 + t * 26
            r = 1.3 + t * 5.0
            sc.f.sph(P(rs.uniform(-1, 1) * t * 3.0, y, 3.0 + t * 3 + rs.uniform(-1, 1) * t * 2.5), r, GLOW(*c))
        for k in range(5):
            a = rs.uniform(0, 6.28)
            sc.f.cone(P(math.cos(a) * 3, 20 + k * 3, 5 + math.sin(a) * 3), P(math.cos(a) * 8, 30 + k * 2, 7 + math.sin(a) * 6), 1.6, 0.05, GLOW(244, 120, 28))
        mz = (0, 22.0, 5.0)
    return sc, [mz], 1.0 if fi else 0.0


def scene_BFGG(cls, fi):
    """thunderclap: 0 hands clasped as in prayer (ready); 1-6 the hands drawn slowly apart with sparks between them;
    7 the clap with the shock ring; 8 after, the rings rolling out"""
    sc = Scene(cls)
    gap = {0: 0.9, 1: 2.0, 2: 3.4, 3: 4.8, 4: 6.2, 5: 7.6, 6: 9.0, 7: 0.9, 8: 2.0}.get(fi, 0.9)
    for sgn in (1, -1):
        sc.arm((sgn * (gap + 3), 4.0, 1.5), (sgn * 8, -14, -9.0))
        sc.hand((sgn * gap, 7.0, 2.5), (-sgn * 0.12, 1, 0.45), (sgn, 0, 0.2), left=(sgn < 0), curl=0.1, spread=0.12, thumb=0.7)
    if 1 <= fi <= 6:
        rs = np.random.RandomState(40 + fi)
        n = 2 + 2 * fi
        for k in range(n):             # sparks in the air between the palms
            x = rs.uniform(-gap * 0.8, gap * 0.8)
            sc.f.sph(P(x, 8.0 + rs.uniform(-1, 3.5), 3.0 + rs.uniform(-1.5, 2.5)), 0.4 + 0.07 * fi + rs.uniform(0, 0.3), GLOW(255, 240, 150))
        sc.f.sph(P(0, 8.5, 3.2), 0.6 + 0.3 * fi, GLOW(180, 220, 255))
    if fi >= 7:
        from art_monsters2 import ring
        for k, rr in enumerate((6.0, 10.0) if fi == 7 else (8.0, 13.0, 18.0)):
            ring(sc.f, P(0, 9 + k * 1.5, 3.0), rr, 72, 1.1 - 0.1 * k, GLOW(130 + 40 * k, 210, 255), axis='y')
    mz = (0, 9.5, 3.0)
    return sc, [mz], 1.0 if fi == 7 else 0.0


def scene_HLFG(cls, fi):
    sc = Scene(cls)
    kick = 0.0 if fi == 0 else 0.6
    xf = holding_gun(sc, 'alienlauncher', 1.6, kick, two_hands=True)
    return sc, [xf((0, 30.5, 2.2))], kick


def bolt(sc, a, b, seed, segs=7, r=0.8, spread=2.2):
    """a jagged lightning bolt of glowing cones from a to b (scene frame)"""
    rs = np.random.RandomState(seed)
    a, b = np.asarray(a, float), np.asarray(b, float)
    pts = [a]
    for k in range(1, segs):
        t = k / segs
        pts.append(a + (b - a) * t + rs.uniform(-spread, spread, 3) * (1 - abs(t - 0.5) * 0.6))
    pts.append(b)
    for p, q in zip(pts[:-1], pts[1:]):
        sc.f.cone(P(*p), P(*q), r, r * 0.8, GLOW(130, 215, 255))
        sc.f.cone(P(*p), P(*q), r * 0.4, r * 0.35, GLOW(245, 252, 255))


def scene_ARCG(cls, fi):
    """arc lightning: palms forward, lightning crawling over the fingers (A), a fan of bolts flung out (B), the last crackle (C)"""
    sc = Scene(cls)
    for sgn in (1, -1):
        sc.arm((sgn * 4.4, 3.0, 0.5), (sgn * 5, -14, -9.0))
        sc.hand((sgn * 3.8, 5.0, 1.2), (-sgn * 0.1, 0.55, 1), (0, -1, 0.35), left=(sgn < 0), curl=0.1, spread=0.55, thumb=0.9)
    if fi == 0:
        for k, sgn in enumerate((1, -1)):
            bolt(sc, (sgn * 3.8, 6.5, 6.5), (sgn * 1.0, 6.5, 8.5), 3 + k, segs=4, r=0.45, spread=0.7)
        bolt(sc, (3.0, 6.5, 7.5), (-3.0, 6.5, 7.5), 9, segs=5, r=0.5, spread=1.0)
    elif fi == 1:
        for k, ang in enumerate(range(-50, 51, 20)):
            tx = math.sin(math.radians(ang)) * 24
            bolt(sc, (math.sin(math.radians(ang)) * 3.0, 8, 6.0), (tx, 8 + math.cos(math.radians(ang)) * 24, 8 + 7 * (1 - abs(ang) / 60.0)), 20 + k, segs=7, r=0.7, spread=2.4)
    else:
        for k, ang in enumerate((-30, 0, 30)):
            bolt(sc, (math.sin(math.radians(ang)) * 3.0, 8, 6.0), (math.sin(math.radians(ang)) * 11, 8 + math.cos(math.radians(ang)) * 11, 8), 40 + k, segs=4, r=0.45, spread=1.2)
    return sc, [], 1.0 if fi == 1 else 0.0


def whirl(sc, cx, base_z, height, radius, turns, n, phase, seed=5):
    """a spiral of wind between the hands: pale cyan-white spheres winding up and widening"""
    rs = np.random.RandomState(seed)
    ramp = [(235, 250, 255), (190, 235, 250), (150, 210, 240), (120, 180, 225)]
    for k in range(n):
        t = k / float(max(1, n - 1))
        a = phase + t * turns * 2 * math.pi
        r = 0.6 + t * radius
        c = ramp[min(3, int(t * 4))]
        sc.f.sph(P(cx + math.cos(a) * r, 8.0 + math.sin(a) * r * 0.7, base_z + t * height), 0.9 + t * 0.9 + rs.uniform(0, 0.3), GLOW(*c))


def scene_TRNG(cls, fi):
    """tornado: bare hands; A cupped and ready, B hands apart with a small whirl between them, C the whirl grown, D hands thrown forward"""
    sc = Scene(cls)
    apart = {0: 2.6, 1: 3.6, 2: 4.6, 3: 3.0}[fi]
    for sgn in (1, -1):
        sc.arm((sgn * apart + sgn * 0.6, 3.0, 0.5), (sgn * 5, -14, -9.0))
        fwd = (-sgn * 0.1, 0.55, 1) if fi != 3 else (-sgn * 0.05, 1, 0.4)
        sc.hand((sgn * apart, 5.0 if fi != 3 else 7.0, 1.2), fwd, (0, -1, 0.35), left=(sgn < 0), curl=0.3, spread=0.45, thumb=0.8)
    if fi == 1:
        whirl(sc, 0, 2.5, 6, 2.2, 2.2, 14, 0.0)
    elif fi == 2:
        whirl(sc, 0, 2.5, 10, 3.6, 2.6, 20, 1.2)
    elif fi == 3:
        whirl(sc, 0, 4.0, 5, 3.0, 2.0, 12, 2.4)
        for k in range(3):
            sc.f.sph(P(0, 11 + k * 3.0, 4.5), 2.2 - k * 0.5, GLOW(210, 240, 255))
    return sc, [], 1.0 if fi >= 2 else 0.0


def scene_SNPG(cls, fi):
    """the snap: soldier / swordsman use one hand, the witch both.  A ready, B thumb pressed on the middle finger, C the snap, D open hand"""
    sc = Scene(cls)
    two = cls == 'W'
    # (curl of each finger, thumb out, hand pitch)
    pose = {0: ((0.3, 0.35, 0.4, 0.45), 0.5, 0.0), 1: ((0.55, 0.9, 0.75, 0.75), 0.12, 0.0),
            2: ((0.5, 1.0, 0.65, 0.65), 0.6, 0.1), 3: ((0.0, 0.0, 0.0, 0.0), 0.95, 0.0)}.get(fi, ((0.3,) * 4, 0.4, 0))
    curls, thumb, tilt = pose
    hands = [(1, 4.0, 'R')] + ([(-1, -4.0, 'L')] if two else [])
    for sgn, x0, _ in hands:
        x = x0 + (1.0 * sgn if two else 0)
        sc.arm((x, 3.0, -1.0), (sgn * 4 + (2 if not two else 0), -14, -10.0))
        sc.hand((x, 5.0, 0.5), (-sgn * 0.1, 0.45, 1), (0, -1, 0.25), left=(sgn < 0), size=1.7,
                finger_curl=curls if not two or sgn > 0 else tuple(reversed(curls)), spread=0.15, thumb=thumb)
    if fi == 2:
        for (sgn, x0, _) in hands:
            x = x0 + (1.0 * sgn if two else 0)
            sc.f.sph(P(x, 8.5, 6.5), 1.7, GLOW(255, 250, 200))
            for k in range(6):
                a = k * math.pi / 3 + 0.3
                sc.f.cone(P(x, 8.5, 6.5), P(x + math.cos(a) * 4.5, 8.5, 6.5 + math.sin(a) * 4.5), 0.55, 0.05, GLOW(255, 235, 120))
    mz = (4.0, 8.5, 6.5)
    return sc, [mz], 0.0


# ---- the blade: PUNG A ready, B wind-up, C slash, D guard (parry), E drawn back (only the tip of the sword shows), F thrust, G riposte wind-up
BLADE = {  # fi: (rx, ry, rz, tx, ty, tz, left hand on the hilt)
    0: (22, 0, -16, 8, -1, 5, False), 1: (62, 0, -40, 13, -6, 9, False), 2: (6, 0, 78, -3, 4, 5, False),
    3: (-4, 0, 30, 1, 5, 3, True), 4: (20, 0, -20, 22, -8, -16, False), 5: (8, 0, -4, 3, 7, 2, True),
    6: (-4, 0, -30, -12, -9, -2, False)}
# 7 (H) is 3 mirrored.  8, 9 (I, J): the sword swinging slowly back from the guard to the ready position
BLADE[8] = tuple(a + (b - a) * 0.45 for a, b in zip(BLADE[3][:6], BLADE[0][:6])) + (True,)
BLADE[9] = tuple(a + (b - a) * 0.8 for a, b in zip(BLADE[3][:6], BLADE[0][:6])) + (False,)
# 10-12 (K, L, M): the bat-away swing, low at the right hip, through the front, up high; 13-15 (N, O, P) are the same mirrored
BLADE[10] = (10, 0, -72, 13, -3, 2, False)
BLADE[11] = (30, 0, 22, 3, 6, 4, False)
BLADE[12] = (80, 0, 56, -5, 9, 10, False)
MIRRORED = {7: 3, 13: 10, 14: 11, 15: 12}


def scene_PUNG(cls, fi):
    """slot 1.  Swordsman: sword.  Soldier: combat knife.  Witch: bare fists (hands up to guard on D)"""
    sc = Scene(cls)
    pose = BLADE.get(fi, BLADE[0])
    rx, ry, rz, tx, ty, tz, left = pose
    if cls == 'W':
        return scene_fists(sc, fi)
    gname, scale = ('knife', 1.9) if cls == 'S' else ('sword', 1.35)
    g = art_guns.gun(gname)
    for p in g.prims:
        p['c'] = p['c'] * scale
        p['s'] = p['s'] * scale
    # the grip is at the model's origin; the blade runs along +y.  Mirror x of the placement into the renderer's frame
    R = rot_matrix(rx=rx, ry=ry, rz=-rz)
    g = g.transformed(R, t=P(tx, ty, tz))
    sc.f.extend(g)
    hand = np.array([tx, ty, tz])
    sc.arm(hand + np.array([0, 0, 0]), np.array([6 + (tx * 0.2), -14, -10.0]))
    sc.gloved(hand + np.array([0, 0, 0]), 3.0)
    if left:
        d = R @ np.array([0, -1.0, 0])
        lh = hand + np.array([-d[0], d[1], d[2]]) * 3.5 + np.array([0, 0, -0.3])
        sc.arm(lh, np.array([-8, -13, -9.0]))
        sc.gloved(lh, 2.9)
    return sc, [], 0


def scene_fists(sc, fi):
    """the witch's bare hands: A guard, B right fist cocked, C punch, D both forearms crossed to block, E-G like B, F, B"""
    st = {0: ((5, 3, -1), (-5, 3, -2)), 1: ((8, -2, 1), (-4.5, 4, -1)), 2: ((2, 12, 3), (-6, 3, -2)),
          3: ((2.5, 7, 4), (-2.5, 7, 4)), 4: ((11, -5, -4), (-6, 3, -2)), 5: ((1, 14, 3), (-6, 3, -2)), 6: ((10, -4, -1), (-5, 3, -2))}[min(fi, 6)]
    (r, l) = st
    for sgn, c in ((1, r), (-1, l)):
        sc.arm(c, (sgn * 6, -14, -10.0))
        fw = (-sgn * 0.35, 1, 0.3) if fi != 3 else (-sgn * 0.5, 0.3, 1)
        sc.hand(c, fw, (0, -0.4, 1), left=(sgn < 0), curl=1.0, spread=0.0, thumb=0.1, size=1.6)
    return sc, [], 0


SCENES = {'TRNG': scene_TRNG, 'HLFG': scene_HLFG, 'ARCG': scene_ARCG, 'PISG': scene_PISG, 'SHTG': scene_SHTG, 'SHT2': scene_SHT2, 'CHGG': scene_CHGG, 'MISG': scene_MISG, 'SAWG': scene_SAWG,
          'PLSG': scene_PLSG, 'BFGG': scene_BFGG, 'PUNG': scene_PUNG, 'SNPG': scene_SNPG}


# ---------------------------------------------------------------------------------------------- lumps
def lump_offsets(base, ox, oy, mirror=False):
    x0, y0 = ANCHOR[base]
    if mirror:
        x0 = 320 - x0
    return int(round(ox - (x0 - 1) * BG)), int(round(oy + (32 - y0) * BG))


def alpha_clean(img):
    a = np.asarray(img).copy()
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    return Image.fromarray(a, 'RGBA')


def weapon_lump(fam, fi):
    """(RGBA, leftoffset, topoffset) for first-person family `fam`, frame index fi"""
    base, cls = FAMILIES[fam]
    if base == 'SHT2' and fi >= 8:                         # the two flash frames live in the weapon's own family
        return flash_lump('SHTF', 0 if fi == 8 else 1, base='SHT2')
    mirror = False
    if base == 'PUNG' and fi in MIRRORED:
        fi, mirror = MIRRORED[fi], True
    sc, _, _ = SCENES[base](cls, fi)
    img, ox, oy = render(finish(sc), FACE_OF.get(base, 180.0), ppu=PPU_OF[base], ss=2, elev=ELEV, outline=1.5)
    img = alpha_clean(img)
    if mirror:       # the same picture turned round the middle of the screen
        img = img.transpose(Image.FLIP_LEFT_RIGHT)
        ox = img.width - 1 - ox
        lo, to = lump_offsets(base, ox, oy, mirror=True)
        return img, lo, to
    lo, to = lump_offsets(base, ox, oy)
    return img, lo, to


FLASH_SIZE = {'PISF': 24, 'SHTF': 26, 'CHGF': 22, 'MISF': 28, 'PLSF': 16, 'BFGF': 22, 'HLFF': 30}


def flash_lump(fam, fi, base=None):
    """a muzzle flash centred on the muzzle of the firing frame"""
    wb = base or FLASH_OF[fam]
    cls = 'N'
    frame_for_fire = {'HLFG': 1, 'PISG': 1, 'SHTG': 1, 'SHT2': 1, 'CHGG': 1, 'MISG': 4, 'PLSG': 1, 'BFGG': 7}[wb]
    sc, marks, kick = SCENES[wb](cls, frame_for_fire)
    pts = project([(m[0], m[1], m[2]) for m in marks], wb)   # marks are in the gun frame (already mirrored as needed)
    # the stars: one per muzzle
    size = FLASH_SIZE.get(fam, 60) * BG * (1.0 - 0.14 * min(fi, 3) if wb != 'SHT2' else (1.0 if fi == 0 else 0.75))
    if wb == 'SHT2':
        size *= 0.62
    pal = FLASHES.get(fam, 'gold')
    stars = []
    for (dx, dy) in pts:
        stars.append((dx, dy, int(size)))
    if wb == 'SHT2' and len(stars) == 3:
        pass
    # canvas holding all stars
    xs0 = min(s[0] - s[2] / 2 for s in stars)
    ys0 = min(s[1] - s[2] / 2 for s in stars)
    xs1 = max(s[0] + s[2] / 2 for s in stars)
    ys1 = max(s[1] + s[2] / 2 for s in stars)
    W, H = int(math.ceil(xs1 - xs0)), int(math.ceil(ys1 - ys0))
    canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for k, (dx, dy, sz) in enumerate(stars):
        st = X.flash(sz, sz, pal, fi + k)
        canvas.alpha_composite(st, (int(round(dx - sz / 2 - xs0)), int(round(dy - sz / 2 - ys0))))
    ox, oy = -xs0, -ys0
    lo, to = lump_offsets(wb, ox, oy)
    return alpha_clean(canvas), lo, to


def weapon_picture(*a, **k):                       # old entry point, kept so stale callers fail loudly
    raise RuntimeError('use weapon_lump')
