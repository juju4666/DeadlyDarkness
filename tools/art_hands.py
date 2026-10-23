"""art_hands.py - bare hands as primitives (palm, four fingers, thumb), for casting, snapping and gripping"""
import math
import numpy as np
from art_render import Fig, rot_matrix


def _unit(v):
    v = np.asarray(v, float)
    return v / max(1e-9, np.linalg.norm(v))


def hand(f, base, fwd, up, skin, curl=0.0, spread=0.0, thumb=0.0, size=1.0, left=False, finger_curl=None):
    """palm centre `base`; fingers point along `fwd`, the back of the hand faces `up`.
    curl: 0 straight .. 1 fist; spread: fan the fingers; thumb: 0 tucked in .. 1 out; left: mirror the thumb side"""
    fwd = _unit(fwd)
    up = _unit(up - fwd * (np.asarray(up, float) @ fwd))
    side = np.cross(up, fwd)            # points to the hand's own left when looking along the fingers
    s = size
    f.ell(base, (2.1 * s, 2.7 * s, 1.0 * s), skin, rot=np.stack([side, fwd, up], axis=1))
    sgn = 1 if left else -1
    offs = (-1.5, -0.5, 0.5, 1.5)
    lens = (2.2, 2.5, 2.3, 1.8)
    for i, (o, ln) in enumerate(zip(offs, lens)):
        c = curl if finger_curl is None else finger_curl[i]
        p = base + fwd * 2.4 * s + side * o * 0.95 * s * (1 + spread * 0.5) + side * spread * o * 0.4
        d = _unit(fwd + side * o * 0.12 * spread * 2)
        for seg in range(3):
            ang = c * (0.55 + 0.35 * seg) * 1.15
            d = _unit(d * math.cos(ang) - up * math.sin(ang))
            q = p + d * ln * s * (0.42 if seg == 0 else 0.36)
            r = 0.55 * s * (1 - 0.12 * seg)
            f.limb(p, q, r, r * 0.92, skin)
            p = q
    # thumb
    tb = base + side * (-sgn) * 1.9 * s + fwd * 0.2 * s
    td = _unit(fwd * (0.9 - 0.5 * (1 - thumb)) + side * (-sgn) * (0.15 + 0.9 * thumb) - up * 0.15)
    t1 = tb + td * 1.7 * s
    f.limb(tb, t1, 0.65 * s, 0.6 * s, skin)
    td2 = _unit(td + fwd * 0.3 - up * 0.2 * (1 - thumb))
    f.limb(t1, t1 + td2 * 1.5 * s, 0.6 * s, 0.5 * s, skin)
