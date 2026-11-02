#!/usr/bin/env python3
"""
make_vehicles.py - makes the placeholder vehicle models (a motorcycle, a mecha and a tank), each with an idle model, an
explosion and two burning-wreck frames, as MagicaVoxel .vox files, their BGVOXEL definition and editor icons.

    python make_vehicles.py OUTDIR

writes   OUTDIR/data/voxels/VXMOTO.vox VXMOTOB VXMOTOC VXMOTOD   VXMECH.vox VXMECHB VXMECHC VXMECHD
         OUTDIR/data/BGVOXELV.txt
         OUTDIR/sprites/props/VMTOA0.png VMCHA0.png

One cell is one art pixel (0.4 map units): the motorcycle is 150 cells (60 units) long, the mecha 240 cells (96 units)
tall. They are blocky stand-ins: replace the .vox files with real models (tools/obj_to_vox.py) and keep the BGVOXEL lines.
Needs numpy, scipy and Pillow.
"""
import os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_voxel_props as mvp

mvp.PAL.update({
    11: (170, 28, 24),     # red paint
    12: (28, 26, 28),      # leather, rubber black
    13: (74, 92, 62),      # olive armour
    14: (214, 172, 36),    # yellow stripe
    15: (96, 100, 108),    # steel
    16: (50, 54, 60),      # dark steel
})


class Canvas:
    def __init__(self, L, W, H):
        mvp.L, mvp.W, mvp.H = L, W, H
        mvp.X0, mvp.Y0 = -L // 2, -W // 2
        self.L, self.W, self.H = L, W, H
        self.g = np.zeros((L, W, H), dtype=np.uint8)
        self.xs = np.arange(L)[:, None, None] + mvp.X0
        self.ys = np.arange(W)[None, :, None] + mvp.Y0
        self.zs = np.arange(H)[None, None, :]

    def box(self, x0, x1, y0, y1, z0, z1, c):
        m = (self.xs >= x0) & (self.xs <= x1) & (self.ys >= y0) & (self.ys <= y1) & (self.zs >= z0) & (self.zs <= z1)
        self.g[m] = c

    def ball(self, cx, cy, cz, rx, ry, rz, c):
        m = ((self.xs - cx) / rx) ** 2 + ((self.ys - cy) / ry) ** 2 + ((self.zs - cz) / rz) ** 2 <= 1
        self.g[m] = c

    def wheel(self, cx, cz, r, y0, y1, tyre=12, hub=15):
        m = ((self.xs - cx) ** 2 + (self.zs - cz) ** 2 <= r * r) & (self.ys >= y0) & (self.ys <= y1)
        self.g[m] = tyre
        h = ((self.xs - cx) ** 2 + (self.zs - cz) ** 2 <= (r * 0.5) ** 2) & (self.ys >= y0) & (self.ys <= y1)
        self.g[h] = hub

    def bar(self, p, q, r, c):
        """a thick line from p to q (x, y, z)"""
        p, q = np.array(p, float), np.array(q, float)
        n = int(np.linalg.norm(q - p)) * 2 + 1
        for t in np.linspace(0, 1, n):
            x, y, z = p + (q - p) * t
            self.ball(x, y, z, r, r, r, c)

    def mirror_y(self, other_box_fn):
        pass


def moto():
    c = Canvas(160, 64, 100)
    # wheels
    c.wheel(58, 22, 22, -5, 5)
    c.wheel(-58, 22, 22, -6, 6)
    # forks and handlebars
    c.bar((58, 5, 22), (40, 5, 70), 2, 4)
    c.bar((58, -5, 22), (40, -5, 70), 2, 4)
    c.box(36, 41, -27, 27, 68, 72, 4)              # bars
    c.box(34, 43, 22, 27, 66, 74, 12)              # grips
    c.box(34, 43, -27, -22, 66, 74, 12)
    # frame, engine, tank, seat
    c.box(-16, 22, -10, 10, 24, 40, 16)
    c.box(-8, 18, -11, 11, 26, 36, 15)             # cylinders
    c.ball(14, 0, 52, 24, 13, 11, 11)              # fuel tank
    c.box(-48, -12, -9, 9, 42, 48, 12)             # seat
    c.box(-62, -40, -8, 8, 36, 44, 11)             # tail
    c.bar((-12, 0, 30), (-58, 0, 22), 2, 16)       # swing arm
    # exhaust
    c.box(-66, -14, 10, 14, 17, 22, 4)
    c.box(-70, -64, 8, 16, 15, 24, 15)
    # fairing and screen
    c.box(36, 48, -10, 10, 56, 70, 11)
    c.box(40, 46, -8, 8, 70, 86, 3)
    # lights
    c.box(48, 51, -6, 6, 58, 66, 244)
    c.box(-71, -69, -4, 4, 38, 42, 243)
    # fenders
    c.box(44, 70, -7, 7, 40, 42, 11)
    c.box(-70, -48, -8, 8, 40, 42, 11)
    return c


def mech():
    c = Canvas(112, 176, 256)
    # feet and legs
    for s in (-1, 1):
        y = lambda a, b: (min(s * a, s * b), max(s * a, s * b))
        c.box(-30, 40, *y(20, 48), 0, 14, 15)                   # foot
        c.box(34, 46, *y(24, 44), 4, 12, 16)                    # toe
        c.box(-14, 16, *y(26, 42), 14, 82, 13)                  # shin
        c.box(-18, 22, *y(24, 44), 40, 56, 14)                  # shin stripe
        c.ball(0, s * 34, 84, 14, 12, 12, 16)                   # knee
        c.box(-20, 22, *y(24, 46), 90, 128, 13)                 # thigh
        c.box(-22, 24, *y(20, 48), 122, 138, 16)                # hip
        # shoulders and arms
        c.box(-18, 22, *y(46, 64), 170, 206, 13)
        c.box(-12, 16, *y(62, 76), 128, 184, 15)                # upper arm
        c.box(-14, 18, *y(60, 78), 120, 138, 16)                # elbow
    # hips and torso
    c.box(-26, 28, -46, 46, 128, 150, 16)
    c.box(-34, 36, -48, 48, 150, 214, 13)
    c.box(-40, 42, -40, 40, 156, 196, 13)
    c.box(30, 46, -30, 30, 196, 222, 13)                         # cockpit nose
    c.box(36, 48, -22, 22, 192, 218, 3)                          # canopy glass
    c.box(-34, -30, -30, 30, 160, 206, 15)                       # back pack
    c.box(-46, -34, -22, 22, 170, 200, 16)
    c.box(-44, -40, -16, 16, 206, 226, 15)                       # exhausts
    c.box(36, 40, -48, 48, 160, 170, 14)                         # chest stripe
    # head sensor and antenna
    c.box(-4, 12, -10, 10, 214, 226, 16)
    c.box(0, 4, 8, 11, 226, 250, 15)
    # right arm: cannon
    c.box(-6, 80, 60, 74, 128, 146, 16)
    c.box(80, 92, 62, 72, 132, 142, 15)
    c.box(-6, 40, 58, 76, 146, 156, 13)
    # left arm: rocket pod
    c.box(-6, 46, -78, -56, 140, 186, 15)
    for i in range(2):
        for j in range(2):
            c.box(46, 50, -74 + 9 * i, -68 + 9 * i, 150 + 14 * j, 158 + 14 * j, 243)
    # lights in the nose
    c.box(46, 48, -18, -12, 200, 204, 244)
    c.box(46, 48, 12, 18, 200, 204, 244)
    return c


def tank():
    """the hull: 260 cells (104 units) long, 190 wide, the deck 58 cells (23 units) up. x is forward."""
    c = Canvas(260, 190, 100)
    for s in (-1, 1):
        y0, y1 = (min(s * 58, s * 92), max(s * 58, s * 92))
        o0, o1 = (min(s * 86, s * 92), max(s * 86, s * 92))
        c.box(-104, 104, y0, y1, 0, 36, 12)                          # the track belt, rounded at the ends
        c.wheel(104, 18, 18, y0, y1, 12, 12)
        c.wheel(-104, 18, 18, y0, y1, 12, 12)
        for x in (-80, -48, -16, 16, 48, 80):                        # road wheels
            c.wheel(x, 17, 14, o0, o1, 16, 15)
        c.wheel(112, 21, 18, o0, o1, 16, 15)                         # sprocket and idler
        c.wheel(-112, 21, 18, o0, o1, 16, 15)
        c.box(-122, 122, y0 - (2 if s > 0 else -2), y1, 38, 44, 13)  # fender over the track
        c.box(-122, 122, min(s * 90, s * 94), max(s * 90, s * 94), 44, 48, 13)
        for x in (-90, -30, 30, 90):                                 # stowage on the fender
            c.box(x - 12, x + 12, min(s * 66, s * 84), max(s * 66, s * 84), 44, 54, 16)
    # hull body and the sloped front
    c.box(-100, 100, -58, 58, 18, 56, 13)
    for i in range(36):
        c.box(100 + i, 100 + i, -58, 58, 18, 56 - int(i * 0.9), 13)
    c.box(98, 112, -56, 56, 44, 47, 14)                              # stripe on the glacis
    c.box(-122, -100, -50, 50, 18, 50, 13)
    for i in range(12):
        c.box(-122 - i, -122 - i, -50, 50, 18, 50 - i * 2, 13)
    # engine deck with vents
    c.box(-100, -28, -52, 52, 56, 60, 16)
    for x in (-92, -76, -60, -44):
        c.box(x, x + 8, -40, 40, 60, 61, 4)
    # turret ring and the driver's hatch
    c.ball(0, 0, 56, 56, 56, 5, 16)
    c.box(70, 104, -24, 24, 56, 66, 13)
    c.box(102, 106, -18, 18, 58, 63, 3)
    c.box(70, 76, -24, 24, 66, 69, 16)
    # lights and a tow hook
    for s in (-1, 1):
        c.box(112, 116, min(s * 40, s * 48), max(s * 40, s * 48), 40, 46, 244)
        c.box(-124, -121, min(s * 40, s * 48), max(s * 40, s * 48), 36, 40, 243)
    c.box(110, 118, -6, 6, 20, 26, 16)
    return c


def turret():
    """the turret, centred on its ring: 320 cells long so the barrel (to 150 cells = 60 units) fits"""
    c = Canvas(320, 120, 70)
    c.ball(0, 0, 0, 62, 52, 40, 13)                                  # rounded body
    c.box(-70, -50, -34, 34, 0, 34, 13)                              # bustle at the back
    c.box(-72, -56, -26, 26, 24, 30, 16)
    c.box(40, 66, -24, 24, 8, 38, 16)                                # mantlet
    c.box(66, 150, -5, 5, 20, 30, 16)                                # the barrel
    c.box(140, 152, -8, 8, 18, 32, 15)                               # muzzle brake
    c.box(40, 90, 12, 16, 22, 26, 12)                                # coaxial machine gun
    c.ball(-16, 20, 38, 14, 14, 7, 16)                               # commander's cupola
    c.box(-22, -10, 14, 26, 42, 44, 3)
    c.bar((-44, -30, 36), (-60, -34, 66), 1, 12)                     # antenna
    c.box(0, 6, -50, -46, 14, 28, 14)                                # yellow marking
    return c


def explosion(c, seed, rad, cz):
    rng = np.random.default_rng(seed)
    g = np.zeros_like(c.g)
    for _ in range(40):
        cx = rng.integers(-c.L // 3, c.L // 3)
        cy = rng.integers(-c.W // 3, c.W // 3)
        z = rng.integers(cz - rad // 2, cz + rad // 2)
        r = rng.integers(rad // 4, rad // 2)
        m = ((c.xs - cx) ** 2 + (c.ys - cy) ** 2 + (c.zs - z) ** 2) <= r * r
        g[m] = np.where(rng.random(g.shape)[m] < 0.5, 241, 242)
    core = ((c.xs) ** 2 + (c.ys) ** 2 + (c.zs - cz) ** 2) <= (rad // 3) ** 2
    g[core] = 244
    out = c.g.copy()
    keep = rng.random(out.shape) < 0.5
    out = np.where(keep, 6, 0).astype(np.uint8) * (out > 0)
    g[(g == 0)] = out[(g == 0)]
    return mvp.shell(g)


def wreck(c, seed, flames):
    rng = np.random.default_rng(seed)
    g = c.g.copy()
    burn = rng.random(g.shape) < 0.55
    g[g > 0] = np.where(burn[g > 0], 6, 10)
    holes = rng.random(g.shape) < 0.06
    g[holes] = 0
    pos = np.argwhere(g > 0)
    top = pos[pos[:, 2] > c.H * 0.35]
    for _ in range(flames):
        x, y, z = top[rng.integers(len(top))]
        hgt = int(rng.integers(c.H // 6, c.H // 3))
        wd = int(rng.integers(5, 10))
        for dz in range(hgt):
            t = dz / hgt
            r = max(1, int(round(wd * (1 - 0.8 * t) / 2)))
            zz = z + dz
            if zz >= c.H:
                break
            sub = g[max(x - r, 0):x + r + 1, max(y - r, 0):y + r + 1, zz]
            col = 241 if t < 0.55 else 242
            if t > 0.85:
                col = 243
            sub[(sub == 0) | (rng.random(sub.shape) < 0.5)] = col
    return mvp.shell(g)


def build(name, canvas, rad, cz, outdir, flames):
    vdir = os.path.join(outdir, 'data', 'voxels')
    base = mvp.shell(canvas.g.copy())
    frames = {name: base,
              name + 'B': explosion(canvas, 11, rad, cz),
              name + 'C': wreck(canvas, 12, flames),
              name + 'D': wreck(canvas, 13, flames)}
    for lump, g in frames.items():
        mvp.L, mvp.W, mvp.H = canvas.L, canvas.W, canvas.H
        mvp.X0, mvp.Y0 = -canvas.L // 2, -canvas.W // 2
        m = mvp.write_vox(os.path.join(vdir, lump + '.vox'), g)
        print(lump, len(m), 'models', int((g > 0).sum()), 'cells')
    return base


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'out'
    os.makedirs(os.path.join(out, 'data', 'voxels'), exist_ok=True)
    os.makedirs(os.path.join(out, 'sprites', 'props'), exist_ok=True)
    cm = moto()
    base = build('VXMOTO', cm, 80, 40, out, 30)
    mvp.L, mvp.W, mvp.H = cm.L, cm.W, cm.H
    mvp.X0, mvp.Y0 = -cm.L // 2, -cm.W // 2
    mvp.icon(base, os.path.join(out, 'sprites', 'props', 'VMTOA0.png'))
    cc = mech()
    base = build('VXMECH', cc, 200, 120, out, 50)
    mvp.L, mvp.W, mvp.H = cc.L, cc.W, cc.H
    mvp.X0, mvp.Y0 = -cc.L // 2, -cc.W // 2
    mvp.icon(base, os.path.join(out, 'sprites', 'props', 'VMCHA0.png'))
    ct = tank()
    base = build('VXTANK', ct, 200, 40, out, 40)
    mvp.L, mvp.W, mvp.H = ct.L, ct.W, ct.H
    mvp.X0, mvp.Y0 = -ct.L // 2, -ct.W // 2
    mvp.icon(base, os.path.join(out, 'sprites', 'props', 'VTNKA0.png'))
    ctu = turret()
    base = mvp.shell(ctu.g.copy())
    mvp.L, mvp.W, mvp.H = ctu.L, ctu.W, ctu.H
    mvp.X0, mvp.Y0 = -ctu.L // 2, -ctu.W // 2
    mvp.write_vox(os.path.join(out, 'data', 'voxels', 'VXTURR.vox'), base)
    mvp.icon(base, os.path.join(out, 'sprites', 'props', 'VTURA0.png'))
    with open(os.path.join(out, 'data', 'BGVOXELV.txt'), 'w') as f:
        f.write("""# Vehicles (BEGOTTEN_ENGINE.md, 0.14.0): voxel props with `vehicle moto` or `vehicle mecha`.
# Get in and out with Use. Motorcycle: throttle, brake, steer with the mouse or turn keys (and the side keys);
# the jump key is the handbrake. Mecha: walks and strafes like the player, primary fire is the cannon, alt fire the rockets.
prop  moto  21201 sprite VMTO radius 22 height 32  health 150 mass 400  vehicle moto
model moto  A VXMOTO
model moto  B VXMOTOB light 300 1.0  1.0 0.55 0.2
model moto  C VXMOTOC light 200 0.8  1.0 0.5  0.15
model moto  D VXMOTOD light 200 0.75 1.0 0.55 0.2
prop  mecha 21202 sprite VMCH radius 32 height 100 health 500 mass 1500 vehicle mecha
model mecha A VXMECH
model mecha B VXMECHB light 420 1.0  1.0 0.55 0.2
model mecha C VXMECHC light 320 0.85 1.0 0.5  0.15
model mecha D VXMECHD light 320 0.8  1.0 0.55 0.2
# Tank (0.15.0): two seats. The hull is `vehicle tank` (driver); it makes its own turret, a bodiless `vehicle turret`
# prop that turns on top of it (gunner). Wheels style drives like a heavy car; tracks style has one key per track.
prop  tank  21203 sprite VTNK radius 40 height 48  health 900 mass 5000 vehicle tank
model tank  A VXTANK
model tank  B VXTANKB light 500 1.0  1.0 0.55 0.2
model tank  C VXTANKC light 360 0.85 1.0 0.5  0.15
model tank  D VXTANKD light 360 0.8  1.0 0.55 0.2
prop  tankturret 21204 sprite VTUR radius 24 height 20 health 1 mass 1 vehicle turret
model tankturret A VXTURR
""")


if __name__ == '__main__':
    main()
