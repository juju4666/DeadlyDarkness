"""Turns the witch photos plus their measurements (measure.py) into the 192 x 176 status face pictures WCH*.png.
Every photo is scaled so that the distance between the eyes and the mouth (measured in 3D, so a turned head gives the
same number) is D pixels, and placed so the middle of the eyes and mouth sits at (96, CY) of the box, horizontally
centred on the cheeks. The features keep their size and place from face to face. The body that the photo shows
below the head fills the rest of the box.
usage: layout.py <photos folder> <landmarks.json> <out folder> [D=49.1] [CY=106]
(D 68 was the first set, 57.8 the second, 49.1 the third: 15% smaller each time. CY 93 -> 106 shows more hair, less clothes.)"""
import sys, os, json
import numpy as np
from PIL import Image

BW, BH = 192, 176

# lump -> photo. Pain levels 0-2 use the healthy photos, 3-4 the hurt ones.
HEALTHY = dict(L='Idle_Left_Healthy', C='Idle_Healthy', R='Idle_Right_Healthy', TR='Damage_Right_Healthy',
               TL='Damage_Left_Hurt',          # no healthy "damage left" photo in the set: the hurt one is used
               OUCH='Shock_Healthy', EVL='Grin_Healthy', KILL='Damage_Hurt')
HURT = dict(L='Idle_Left_Hurt', C='Idle_Hurt', R='Idle_Right_Hurt', TR='Damage_Right_Hurt', TL='Damage_Left_Hurt',
            OUCH='Shock_Front_Hurt', EVL='Grin_Front_Hurt', KILL='Damage_Front_Hurt')


def lumps():
    out = {}
    for n in range(5):
        set_ = HEALTHY if n < 3 else HURT
        for k, side in enumerate('LCR'):
            out['WCHST%d%d' % (n, k)] = set_[side]
        out['WCHTR%d0' % n] = set_['TR']
        out['WCHTL%d0' % n] = set_['TL']
        out['WCHOUCH%d' % n] = set_['OUCH']
        out['WCHEVL%d' % n] = set_['EVL']
        out['WCHKILL%d' % n] = set_['KILL']
    out['WCHGOD0'] = 'God_Mode'
    out['WCHDEAD0'] = 'Dead'
    return out


def render(src, d, D, cy):
    e = np.array(d['eyes'])
    m = np.array(d['mouth'])
    c = (e + m) / 2
    cx = (d['cheekL'][0] + d['cheekR'][0]) / 2
    s = D / d['d3']
    nw, nh = max(1, round(src.width * s)), max(1, round(src.height * s))
    im = src.convert('RGBa').resize((nw, nh), Image.LANCZOS).convert('RGBA')
    ox, oy = round(BW / 2 - cx * s), round(cy - c[1] * s)
    canvas = Image.new('RGBA', (BW, BH), (0, 0, 0, 0))
    canvas.paste(im, (ox, oy), im)
    return canvas


if __name__ == '__main__':
    photos, lm, outdir = sys.argv[1:4]
    D = float(sys.argv[4]) if len(sys.argv) > 4 else 49.1
    CY = float(sys.argv[5]) if len(sys.argv) > 5 else 106
    L = json.load(open(lm))
    os.makedirs(outdir, exist_ok=True)
    cache = {}
    for lump, name in lumps().items():
        if name not in cache:
            cache[name] = render(Image.open(os.path.join(photos, name + '.png')).convert('RGBA'), L[name], D, CY)
        cache[name].save(os.path.join(outdir, lump + '.png'))
    print('wrote %d pictures to %s (D %.1f, CY %.0f)' % (len(lumps()), outdir, D, CY))
