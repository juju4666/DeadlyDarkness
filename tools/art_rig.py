"""art_rig.py - a posable two-armed, two-legged rig built from art_render primitives, plus pose helpers."""
import math
import numpy as np
from art_render import Fig, mat, rot_matrix, frame_along

D2R = math.pi / 180.0


def clamp01(x):
    return max(0.0, min(1.0, x))


def ease(p):
    return p * p * (3 - 2 * p)


def pose(**kw):
    d = dict(lean=0, hipL=0, hipR=0, kneeL=0, kneeR=0, shL=6, shR=6, abL=8, abR=8, elL=14, elR=14, hd=0, hyaw=0, drop=0,
             fall=0, fall_roll=0, twist=0, lift=0, spread=0)
    d.update(kw)
    return d


def walk_pose(i, n=4, amp=28, arm=22, bob=1.2, lean=0, **kw):
    ph = 2 * math.pi * i / n
    hl = amp * math.sin(ph)
    d = pose(hipL=hl, hipR=-hl, kneeL=8 + 34 * max(0, math.cos(ph)), kneeR=8 + 34 * max(0, -math.cos(ph)),
             shL=6 - arm * math.sin(ph), shR=6 + arm * math.sin(ph), drop=bob * abs(math.cos(ph)), lean=lean)
    d.update(kw)
    return d


def biped(sp, ps):
    """sp: dimensions + materials, ps: pose.  Returns (Fig, joints)."""
    f = Fig()
    g = lambda k, d=None: sp.get(k, d)
    hip_z = g('leg1') + g('leg2') - ps['drop'] + ps['lift']
    hw = g('hip_w', 3.5)
    shw = g('sh_w', 7)
    pelvis = np.array([0.0, 0, hip_z])
    Rt = rot_matrix(rx=-ps['lean'], rz=ps['twist'])
    Rh = Rt @ rot_matrix(rx=ps['hd'], rz=ps['hyaw'])
    tl = g('torso_len')
    pants, boots, cloth, skin = g('pants'), g('boots'), g('cloth'), g('skin')
    J = {'Rt': Rt, 'Rh': Rh, 'pelvis': pelvis}
    # legs
    for side, nm in ((1, 'L'), (-1, 'R')):
        a = ps['hip' + nm] * D2R
        kb = ps['knee' + nm] * D2R
        sp_ = ps['spread'] * D2R * side
        hip = pelvis + np.array([side * hw, 0, 0])
        knee = hip + np.array([math.sin(sp_) * g('leg1'), math.sin(a) * g('leg1'), -math.cos(a) * g('leg1')])
        b = a - kb
        foot = knee + np.array([math.sin(sp_) * 0.3 * g('leg2'), math.sin(b) * g('leg2'), -math.cos(b) * g('leg2')])
        if g('legs', True):
            f.limb(hip, knee, g('leg_r', 3.2), g('leg_r', 3.2) * 0.85, pants)
            f.limb(knee, foot, g('leg_r', 3.2) * 0.85, g('leg_r', 3.2) * 0.7, boots)
            fl = g('foot_len', 5.0)
            f.box(foot + np.array([0, fl * 0.32, -g('leg_r', 3.2) * 0.3]), [g('leg_r', 3.2) * 0.8, fl * 0.62, g('leg_r', 3.2) * 0.45], boots,
                  rot=(-ps['hip' + nm] * 0.2 + 0, 0, 0))
        J['foot' + nm] = foot
        J['knee' + nm] = knee
    # torso
    tc = pelvis + Rt @ np.array([0, 0, tl * 0.5])
    if g('torso_kind', 'ell') == 'box':
        f.box(tc, [g('torso_w', 6.5), g('torso_d', 4), tl * 0.5], cloth, rot=Rt)
    else:
        f.ell(tc, [g('torso_w', 6.5), g('torso_d', 4.2), tl * 0.55], cloth, rot=Rt)
    f.box(pelvis + Rt @ np.array([0, 0, 0.5]), [g('torso_w', 6.5) * 0.9, g('torso_d', 4) * 0.9, 2.2], pants, rot=Rt)
    neck = pelvis + Rt @ np.array([0, 0, tl + g('neck', 1.5)])
    hr = g('head_r', 4.4)
    head = neck + Rh @ np.array([0, 0, hr * 0.7])
    if g('neck_r', 0) > 0:
        f.cone(pelvis + Rt @ np.array([0, 0, tl - 1]), neck + Rh @ np.array([0, 0, 1]), g('neck_r') * 1.2, g('neck_r'), skin, caps=False)
    if g('head_kind', 'ell') == 'ell':
        f.ell(head, [hr * g('head_w', 0.92), hr * g('head_d', 1.0), hr * g('head_h', 1.08)], skin, rot=Rh)
    J.update(head=head, neck=neck, hr=hr)
    # arms
    for side, nm in ((1, 'L'), (-1, 'R')):
        sh = pelvis + Rt @ np.array([side * shw, 0, tl - 1.8])
        s1 = ps.get('sh' + nm, 6) * D2R
        ab = ps.get('ab' + nm, 8) * D2R
        el = ps.get('el' + nm, 14) * D2R
        tgt = ps.get('tgt' + nm)
        if tgt is not None:
            # two-bone IK in the torso frame: the hand goes to tgt (relative to the pelvis), the elbow stays low and out
            S = np.array([side * shw, 0, tl - 1.8])
            T = np.asarray(tgt, float)
            l1, l2 = g('arm1'), g('arm2')
            v = T - S
            dist = max(1e-3, min(np.linalg.norm(v), (l1 + l2) * 0.995))
            dn = v / max(np.linalg.norm(v), 1e-6)
            a_ = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist)
            h_ = math.sqrt(max(0.0, l1 * l1 - a_ * a_))
            pole = np.array(ps.get('pole' + nm, (side * 0.6, -0.3, -1.0)), float)
            perp = pole - dn * (pole @ dn)
            perp /= max(np.linalg.norm(perp), 1e-6)
            el_l = S + dn * a_ + perp * h_
            ha_l = S + dn * dist
            sh = pelvis + Rt @ S
            elbow = pelvis + Rt @ el_l
            hand = pelvis + Rt @ ha_l
            ar = g('arm_r', 2.5)
            if g('arms', True):
                f.limb(sh, elbow, ar * 1.1, ar, g('sleeve', cloth))
                f.limb(elbow, hand, ar, ar * 0.8, g('sleeve2', g('sleeve', cloth)))
                f.sph(hand, ar * 0.95, g('glove', skin))
            J['sh' + nm], J['elbow' + nm], J['hand' + nm] = sh, elbow, hand
            J['fore' + nm] = Rt @ (ha_l - el_l)
            continue
        d1 = np.array([side * math.sin(ab), math.sin(s1) * math.cos(ab), -math.cos(s1) * math.cos(ab)])
        d1 /= np.linalg.norm(d1)
        elbow = sh + Rt @ (d1 * g('arm1'))
        s2 = s1 + el
        d2 = np.array([side * math.sin(ab) * 0.8, math.sin(s2) * math.cos(ab), -math.cos(s2) * math.cos(ab)])
        d2 /= np.linalg.norm(d2)
        hand = elbow + Rt @ (d2 * g('arm2'))
        ar = g('arm_r', 2.5)
        if g('arms', True):
            f.limb(sh, elbow, ar * 1.1, ar, g('sleeve', cloth))
            f.limb(elbow, hand, ar, ar * 0.8, g('sleeve2', g('sleeve', cloth)))
            f.sph(hand, ar * 0.95, g('glove', skin))
        J['sh' + nm] = sh
        J['elbow' + nm] = elbow
        J['hand' + nm] = hand
        J['fore' + nm] = Rt @ d2
    return f, J


def apply_fall(f, ang, roll=0, pivot=(0, 0, 0)):
    """tip the whole figure over backwards (ang > 0) or forwards (ang < 0), then put it back on the floor"""
    if abs(ang) < 1e-3 and abs(roll) < 1e-3:
        return f
    R = rot_matrix(rx=ang, ry=roll)
    g = f.transformed(R, pivot=pivot)
    z = g.lowest()
    return g.transformed(np.eye(3), t=(0, 0, -z), pivot=(0, 0, 0))


def held(f, a, b, m_body, r=1.0, **kw):
    f.limb(a, b, r, r, m_body, joint=False)


def blob_pool(f, c, radius, m, h=0.5):
    f.ell(c, [radius, radius * 0.8, h], m)


def gibs(f, p, spread, mats, rng_seed=1, count=9, ground=True):
    """chunks of flesh flying outward; p = progress 0..1.  Deterministic."""
    rs = np.random.RandomState(rng_seed)
    for i in range(count):
        ang = rs.uniform(0, 2 * math.pi)
        dist = spread * rs.uniform(0.3, 1.0) * ease(p)
        vz = rs.uniform(10, 30)
        z = max(0.8, vz * p * (1.0 - p) * 2.2 + 1.0) if p < 1 else 1.0
        r = rs.uniform(1.6, 3.4)
        m = mats[i % len(mats)]
        f.ell((math.cos(ang) * dist, math.sin(ang) * dist * 0.8, z), [r, r * 0.9, r * 0.7], m)
    if ground and p > 0.35:
        blob_pool(f, (0, 0, 0.3), spread * 0.8 * min(1, (p - 0.3) * 1.4), mats[0], 0.3)
