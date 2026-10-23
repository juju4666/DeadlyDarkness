"""art_guns.py - the weapons as small 3D models.  Gun frame: the right hand's grip is the origin, the barrel points along +y,
z is up.  Each builder returns a Fig; GUNS[name] = (builder, support point for the left hand)"""
import math
import numpy as np
from art_render import Fig, mat, rot_matrix

M = mat
GUNMETAL = M((70, 74, 84), spec=0.55, shin=28, noise=0.03)
STEELM = M((150, 156, 168), spec=0.7, shin=34, noise=0.03)
DARKM = M((38, 40, 46), spec=0.45, shin=22, noise=0.03)
WOOD = M((120, 78, 44), spec=0.25, noise=0.2)
ORANGE = M((232, 120, 32), spec=0.4, noise=0.05)
BRASS = M((222, 176, 70), spec=0.6, shin=30, noise=0.03)
GLOWB = lambda r, g, b: M((r, g, b), glow=1.0, noise=0, flat=True)


def tube(f, y0, y1, r, m, z=0.0, x=0.0):
    f.cone(np.array([x, y0, z]), np.array([x, y1, z]), r, r, m)


def pistol():
    f = Fig()
    f.box((0, 4.5, 2.4), (1.0, 5.0, 1.3), GUNMETAL)                 # slide
    f.cone((0, 8.5, 2.2), (0, 11.5, 2.2), 0.8, 0.8, DARKM)
    f.box((0, 0.2, -1.8), (1.0, 1.4, 3.4), DARKM, rot=(-12, 0, 0))       # grip
    f.box((0, 3.4, 0.6), (0.7, 2.2, 0.5), GUNMETAL)
    f.box((0, 4.6, 3.8), (0.35, 0.5, 0.3), STEELM)
    return f


def shotgun():
    f = Fig()
    tube(f, 0, 30, 1.0, GUNMETAL, z=2.4)
    tube(f, 0, 27, 1.3, DARKM, z=0.9)                                # magazine tube
    f.box((0, 14, 0.8), (1.4, 4.0, 1.6), WOOD)                       # pump
    f.box((0, 1.0, 1.6), (1.4, 3.2, 2.0), GUNMETAL)                  # receiver
    f.box((0, -7, 0.2), (1.1, 6.5, 1.9), WOOD, rot=(8, 0, 0))        # stock
    f.box((0, -13, -1.0), (1.2, 0.8, 2.6), DARKM, rot=(8, 0, 0))
    f.box((0, 29.5, 3.7), (0.3, 0.4, 0.4), STEELM)
    return f


def supershotgun():
    f = Fig()
    for x in (-0.95, 0.95):
        tube(f, 0, 24, 0.95, GUNMETAL, z=2.2, x=x)
    f.box((0, 14, 0.6), (1.9, 5.0, 1.0), WOOD)
    f.box((0, 0.5, 1.4), (2.0, 2.6, 2.2), GUNMETAL)
    f.box((0, -7, 0.1), (1.2, 6.5, 2.0), WOOD, rot=(8, 0, 0))
    f.box((0, -13.5, -1.0), (1.3, 0.8, 2.8), DARKM, rot=(8, 0, 0))
    f.box((0, 4.5, 3.0), (0.5, 1.4, 0.4), BRASS)
    return f


def chaingun():
    f = Fig()
    for k in range(6):
        a = k * math.pi / 3
        tube(f, 2, 24, 0.75, GUNMETAL, z=2.4 + math.sin(a) * 2.0, x=math.cos(a) * 2.0)
    f.cone((0, 22, 2.4), (0, 24.5, 2.4), 3.2, 3.2, DARKM)
    f.cone((0, 7, 2.4), (0, 8.4, 2.4), 3.4, 3.4, STEELM)
    f.box((0, -1, 2.4), (3.2, 4.8, 3.2), GUNMETAL)                      # body
    f.box((3.6, -2, -0.5), (1.6, 2.4, 3.2), BRASS)                      # ammo box
    f.box((0, -5, -0.4), (0.9, 1.0, 3.4), DARKM, rot=(-14, 0, 0))       # rear grip
    f.box((0, 4, 5.8), (0.5, 2.2, 0.8), DARKM)                          # carry handle
    return f


def rocketlauncher():
    f = Fig()
    tube(f, -9, 24, 2.9, M((96, 104, 84), spec=0.35, noise=0.06), z=3.4)
    f.cone((0, 24, 3.4), (0, 28, 3.4), 2.9, 4.0, M((74, 78, 66), spec=0.4))
    f.cone((0, -9, 3.4), (0, -13, 3.4), 2.9, 4.2, DARKM)
    f.cone((0, 9, 3.4), (0, 10.4, 3.4), 3.1, 3.1, ORANGE)
    f.box((0, 0, -0.8), (0.9, 1.0, 3.4), DARKM)
    f.box((0, 10, 0.4), (0.8, 0.9, 2.4), DARKM)
    f.box((0, 14, 7.0), (0.3, 0.3, 0.8), STEELM)
    f.cone((0, 25, 3.4), (0, 28.5, 3.4), 1.2, 0.2, ORANGE)               # the rocket's nose
    return f


def plasmagun():
    f = Fig()
    f.box((0, 4, 2.6), (2.4, 9, 2.4), M((58, 76, 120), spec=0.5, shin=26, noise=0.04))
    f.box((0, 14, 3.0), (1.5, 6.0, 1.7), DARKM)
    for k in range(5):
        f.cone((0, 2.5 + k * 3.8, 3.0), (0, 3.6 + k * 3.8, 3.0), 2.4, 2.4, GLOWB(90, 200, 255))
    f.cone((0, 20, 3.0), (0, 24, 3.0), 2.2, 1.1, STEELM)
    f.box((0, -1.5, -0.8), (0.9, 1.0, 3.4), DARKM, rot=(-10, 0, 0))
    f.box((0, -8, 3.0), (2.0, 3.0, 2.6), GUNMETAL)
    return f


def bfg():
    f = Fig()
    f.box((0, 4, 3.4), (4.4, 10, 3.6), M((62, 100, 62), spec=0.45, shin=24, noise=0.05))
    f.ell((0, 15, 3.4), (5.2, 5.0, 5.2), M((54, 84, 54), spec=0.45, shin=24, noise=0.05))
    f.cone((0, 19, 3.4), (0, 26, 3.4), 4.6, 5.8, GUNMETAL)
    f.cone((0, 26, 3.4), (0, 26.8, 3.4), 3.6, 3.6, GLOWB(120, 255, 120))
    for k in range(3):
        f.cone((0, 5 + k * 5, 7.0), (0, 6.5 + k * 5, 7.0), 2.0, 2.0, GLOWB(140, 255, 140))
    f.box((0, -2, -1.2), (0.9, 1.0, 3.6), DARKM, rot=(-10, 0, 0))
    f.box((0, -9, 3.4), (3.6, 4.0, 3.2), GUNMETAL)
    f.box((0, 4, -0.4), (1.0, 2.5, 3.0), DARKM)
    return f


def chainsaw():
    f = Fig()
    f.box((0, 3, 2.4), (3.0, 6, 3.2), ORANGE)
    f.box((0, 4, 6.0), (1.4, 3, 0.6), DARKM)
    f.box((0, -2.5, -1.0), (0.9, 1.0, 3.8), DARKM, rot=(-14, 0, 0))            # rear handle
    f.limb((0, -5, 4.5), (0, -9, 1.5), 0.7, 0.7, DARKM)
    f.box((0, 17, 2.2), (0.7, 11.5, 2.4), STEELM)                        # the bar
    f.cone((0, 17, 4.6), (0, 27, 4.6), 0.4, 0.4, DARKM) if False else None
    for k in range(10):
        f.box((0, 7.5 + k * 2.2, 4.8), (0.9, 0.5, 0.5), DARKM)
        f.box((0, 7.5 + k * 2.2, -0.4), (0.9, 0.5, 0.5), DARKM)
    f.ell((0, 28.5, 2.2), (0.9, 1.2, 2.5), STEELM)
    return f


def knife():
    f = Fig()
    f.box((0, 0, 0), (0.9, 2.4, 0.9), M((50, 40, 34), noise=0.1))
    f.box((0, 3.0, 0), (1.9, 0.5, 0.5), STEELM)
    f.box((0, 9.5, 0), (0.35, 6.5, 1.5), STEELM)
    f.cone((0, 16, 0), (0, 19, 0), 0.35, 0.05, STEELM) if False else None
    return f


def sword():
    f = Fig()
    f.cone((0, -2.5, 0), (0, 3.0, 0), 0.8, 0.8, M((90, 50, 40), noise=0.1))
    f.sph((0, -3.0, 0), 1.2, BRASS)
    f.box((0, 3.4, 0), (4.8, 0.7, 0.7), BRASS)
    f.box((0, 17, 0), (0.4, 13.5, 1.4), STEELM)
    f.box((0, 17, 0), (0.15, 13.2, 1.5), M((220, 226, 236), spec=0.9, shin=40, noise=0.02))
    f.cone((0, 30.5, 0), (0, 34, 0), 0.5, 0.05, STEELM)
    return f


def wand():
    f = Fig()
    f.cone((0, -3, 0), (0, 12, 0), 0.7, 0.45, M((70, 40, 90), noise=0.15))
    f.sph((0, 13.5, 0), 1.8, GLOWB(255, 140, 255))
    f.sph((0, 13.5, 0), 0.9, GLOWB(255, 240, 255))
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        f.cone((0, 12, 0), (math.cos(a) * 2.2, 14.5, math.sin(a) * 2.2), 0.4, 0.05, BRASS)
    return f


def revolver():
    f = Fig()
    f.cone((0, 4.5, 2.9), (0, 15, 2.9), 0.85, 0.75, GUNMETAL)         # barrel
    f.box((0, 9.5, 1.7), (0.6, 5.0, 0.55), GUNMETAL)                   # underlug
    f.cyl((0, 2.5, 2.4), 1.9, 5.0, STEELM, rot=(90, 0, 0))            # cylinder
    for k in range(6):
        a = k * math.pi / 3
        f.sph((math.cos(a) * 1.25, 4.9, 2.4 + math.sin(a) * 1.25), 0.38, DARKM)
    f.box((0, -1.5, 3.2), (0.8, 2.6, 1.3), GUNMETAL)                   # frame
    f.box((0, -4.2, 4.1), (0.35, 0.8, 0.9), STEELM, rot=(25, 0, 0))     # hammer
    f.box((0, 14.5, 3.9), (0.25, 0.4, 0.4), STEELM)                    # sight
    f.box((0, -2.2, -1.8), (1.0, 1.7, 3.6), WOOD, rot=(-18, 0, 0))     # grip
    f.box((0, 0.6, 0.5), (0.4, 1.6, 0.35), DARKM)                      # guard
    return f


def tripleshotgun(spin=0.0, open_=0.0, shells=False):
    """three barrels around a hub that rotates; break action hinged under the barrels.
    spin: cluster rotation (deg).  open_: barrels hinged down (deg)"""
    f = Fig()
    f.box((0, 0.5, 0.8), (1.5, 3.4, 2.0), GUNMETAL)                    # receiver
    f.box((0, -7.5, 0.2), (1.2, 6.5, 2.0), WOOD, rot=(8, 0, 0))        # stock
    f.box((0, -13.5, -1.0), (1.3, 0.8, 2.8), DARKM, rot=(8, 0, 0))
    f.box((0, 2.0, -1.0), (0.35, 1.8, 0.5), DARKM)                     # guard
    f.box((0, -1.0, -3.0), (1.0, 1.2, 2.4), DARKM, rot=(-14, 0, 0))    # grip
    f.sph((0, 3.5, 1.4), 1.0, STEELM)                                   # hinge
    b = Fig()
    for k in range(3):
        a = math.radians(90 + spin + 120 * k)
        x, z = math.cos(a) * 1.55, 2.7 + math.sin(a) * 1.55
        b.cone((x, 4, z), (x, 31, z), 1.0, 1.0, GUNMETAL)
        b.cone((x, 31, z), (x, 31.4, z), 0.55, 0.55, DARKM)
        if shells and open_ > 0:
            b.cone((x, 4.2, z), (x, 7.2, z), 0.75, 0.75, ORANGE)
    b.cone((0, 4, 2.7), (0, 29, 2.7), 0.6, 0.6, DARKM)                 # hub rod
    for y in (5, 17, 29):
        b.cone((0, y - 0.5, 2.7), (0, y + 0.5, 2.7), 2.6, 2.6, STEELM)
    b.box((0, 12, 4.7), (0.25, 10, 0.25), STEELM)                      # rail
    b.box((0, 30.2, 5.0), (0.25, 0.4, 0.3), STEELM)
    if open_:
        b = b.transformed(rot_matrix(-open_, 0, 0), pivot=(0, 3.5, 1.4))
    f.extend(b)
    return f


def alienlauncher():
    """slot 8: an alien launcher grown rather than built - a ribbed chitin body, three curled prongs round a glowing core"""
    f = Fig()
    chitin = M((96, 74, 116), spec=0.55, shin=30, noise=0.1)
    dark = M((50, 36, 62), spec=0.4, shin=22, noise=0.08)
    flesh = M((150, 70, 96), spec=0.3, noise=0.12)
    pink = GLOWB(255, 80, 170)
    f.ell((0, 7, 2.2), (2.9, 15, 2.7), chitin)                          # body
    for y in range(-4, 20, 4):
        f.cone((0, y, 2.2), (0, y + 1.3, 2.2), 3.1 - abs(y - 8) * 0.03, 3.1 - abs(y - 8) * 0.03, dark)   # ribs
    for k in range(3):                                                  # the prongs round the muzzle
        a = math.radians(90 + 120 * k)
        x0, z0 = math.cos(a) * 2.4, 2.2 + math.sin(a) * 2.4
        f.cone((x0, 17, z0), (x0 * 1.5, 24, 2.2 + (z0 - 2.2) * 1.5), 1.1, 0.8, dark)
        f.cone((x0 * 1.5, 24, 2.2 + (z0 - 2.2) * 1.5), (x0 * 0.7, 30, 2.2 + (z0 - 2.2) * 0.7), 0.8, 0.35, chitin)
    f.sph((0, 21, 2.2), 1.5, pink)                                      # the glowing core
    f.sph((0, 3.5, 5.3), 1.5, pink)                                     # an eye on the back
    f.ell((0, -8, 1.6), (2.4, 4.5, 2.4), chitin)                        # rear bulb
    f.ell((0, 1.0, -1.8), (1.3, 2.0, 3.4), flesh, rot=(-14, 0, 0))      # the grip, soft and veined
    f.ell((0, 8, 6.0), (0.6, 7, 1.0), dark)                             # dorsal fin
    return f


GUNS = {'pistol': (pistol, (0, 6, 1.0)), 'shotgun': (shotgun, (0, 15, 0.3)), 'supershotgun': (supershotgun, (0, 15, 0.2)),
        'chaingun': (chaingun, (0, 12, 0.5)), 'rocketlauncher': (rocketlauncher, (0, 12, 1.0)), 'plasmagun': (plasmagun, (0, 12, 0.6)),
        'bfg': (bfg, (0, 12, 0.5)), 'chainsaw': (chainsaw, (0, 9, 0.6)), 'knife': (knife, (0, 2, 0)), 'sword': (sword, (0, 2, 0)),
        'wand': (wand, (0, 2, 0)), 'revolver': (revolver, (0, 6, 1.0)),
        'tripleshotgun': (tripleshotgun, (0, 15, 0.3)), 'alienlauncher': (alienlauncher, (0, 10, 0.2))}
GUNS['pistol'] = GUNS['revolver']
GUNS['shotgun'] = GUNS['tripleshotgun']
GUNS['supershotgun'] = GUNS['tripleshotgun']


def gun(name, rot=None, t=(0, 0, 0), scale=1.0, **kw):
    f = GUNS[name][0](**kw)
    if scale != 1.0:
        for p in f.prims:
            p['c'] = p['c'] * scale
            p['s'] = p['s'] * scale
    if rot is not None:
        f = f.transformed(rot, t=t)
    elif any(t):
        f = f.transformed(np.eye(3), t=t)
    return f
