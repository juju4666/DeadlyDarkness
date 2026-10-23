import functools
import art_fx as X
import art_fp as FP
import art_apply
from art_render import render, rot_matrix
from PIL import Image

# family -> (kind, palette)
BALLS = {'BAL1': ('orb', 'fire'), 'BAL2': ('orb', 'fire'), 'BAL7': ('bolt', 'green'), 'BRBL': ('orb', 'gold'), 'BTAC': ('orb', 'acid'),
         'DRNB': ('bolt', 'drain'), 'EFBL': ('orb', 'fire'), 'HFBL': ('orb', 'drain'), 'MCFL': ('orb', 'fire'), 'WZBL': ('orb', 'magic'),
         'FATB': ('orb', 'fire'), 'APLS': ('orb', 'green'), 'PLSS': ('orb', 'plasma'), 'BFS1': ('orb', 'green'), 'SBLT': ('bolt', 'plasma'),
         'MANF': ('orb', 'fire')}
BOOMS = {'BAL1': 'fire', 'BAL2': 'fire', 'BAL7': 'green', 'BRBL': 'gold', 'BTAC': 'acid', 'DRNB': 'drain', 'EFBL': 'fire', 'HFBL': 'drain',
         'MCFL': 'fire', 'WZBL': 'magic', 'FATB': 'fire', 'APLS': 'green', 'PLSS': 'plasma', 'BFS1': 'green', 'SBLT': 'plasma', 'MANF': 'fire',
         'TRKT': 'fire', 'MISL': 'fire', 'MCRK': 'fire', 'GRCK': 'dust', 'MCSK': 'fire', 'WZSK': 'magic',
         'BEXP': 'fire', 'FBXP': 'fire', 'BFE1': 'green', 'BFE2': 'green', 'PLSE': 'plasma', 'APBX': 'green'}
BOLT_RGB = {'plasma': (90, 190, 255), 'green': (110, 255, 110), 'drain': (230, 50, 90)}
ROCKETS = {'TRKT': dict(flame_pal=(255, 160, 50)), 'MISL': dict(), 'MCRK': dict(body=(110, 116, 100), flame_pal=(255, 120, 40))}
SEEKERS = {'MCSK': (255, 120, 60), 'WZSK': (230, 90, 255)}
SPHERES = {'PINS': 'magic'}
COUNTS = {}


def cur():
    return art_apply.CUR


def player_shots(fam, fi, rot, k, w, h, lo, to):
    """the player's own projectiles (see begotten.c): FBAL fireball, DBRT devil's breath, TRNP / TRNF tornado, ARCB arc bolt,
    HLFR hellfire rocket, CLAP thunderclap ring"""
    if fam == 'FBAL':
        if fi < 2:
            return _keep(X.orb(w, h, 'fire', fi, seed=71), lo, to)
        return _keep(X.boom(w, h, 'fire', (fi - 1) / 3.0, seed=72), lo, to)
    if fam == 'DBRT':
        if fi < 2:
            return _keep(X.flame_column(w, h, 'fire', fi, seed=73), lo, to)
        return _keep(X.boom(w, h, 'fire', (fi - 1) / 3.0, seed=74), lo, to)
    if fam == 'TRNP':
        if fi < 4:
            return _keep(X.swirl(w, h, fi), lo, to)
        return _keep(X.gust(w, h, (fi - 4) / 1.0), lo, to)
    if fam == 'TRNF':
        return _keep(X.funnel(w, h, fi), lo, to)
    if fam == 'ARCB':
        if fi < 2:
            return _keep(X.zap(w, h, fi), lo, to)
        return _keep(X.flash(w, h, 'plasma', fi), lo, to)
    if fam == 'HLFR':
        if rot > 0:
            return render(X.missile_fig((240, 70, 170), fi, body=(110, 80, 130)), (rot - 1) * 45, ppu=k, elev=10)
        return _keep(X.boom(w, h, 'magic', fi / 3.0, seed=75), lo, to)
    if fam == 'CLAP':
        return _keep(X.shock_ring(w, h, fi / 4.0), lo, to)
    return None


def fx(fam, fi, rot, k):
    w, h, lo, to = cur()
    if fam in ('FBAL', 'DBRT', 'TRNP', 'TRNF', 'ARCB', 'HLFR', 'CLAP'):
        return player_shots(fam, fi, rot, k, w, h, lo, to)
    # rotated, 3D things
    if rot > 0:
        facing = (rot - 1) * 45
        if fam in ROCKETS:
            return render(X.rocket_fig(**ROCKETS[fam]), facing, ppu=k, elev=10)
        if fam in SEEKERS:
            return render(X.missile_fig(SEEKERS[fam], fi), facing, ppu=k, elev=10)
        if fam in BALLS and BALLS[fam][0] == 'bolt':
            return render(X.bolt_fig(BOLT_RGB[BALLS[fam][1]]), facing, ppu=k, elev=10)
        if fam in BALLS:
            return _keep(X.orb(w, h, BALLS[fam][1], fi, seed=hash(fam) % 50), lo, to)
        return None
    # unrotated
    if fam in BALLS and fi < 2:
        if BALLS[fam][0] == 'bolt':
            return _keep(X.orb(w, h, BALLS[fam][1], fi, seed=hash(fam) % 50), lo, to)
        return _keep(X.orb(w, h, BALLS[fam][1], fi, seed=hash(fam) % 50), lo, to)
    if fam == 'GRCK' and fi < 2:
        return render(X.rock_fig(fi), 20, ppu=k, elev=12)
    if fam in BOOMS:
        n0 = {'TRKT': 1, 'MISL': 1, 'MCRK': 1, 'SBLT': 2, 'DRNB': 2, 'BAL7': 2, 'MCSK': 1, 'WZSK': 1}.get(fam, 2 if fam in BALLS or fam == 'GRCK' else 0)
        total = {'BEXP': 5, 'FBXP': 3, 'BFE1': 6, 'BFE2': 4, 'PLSE': 5, 'APBX': 5}.get(fam, 5 if fam not in ('TRKT', 'MISL', 'MCRK') else 4)
        idx = fi - n0
        p = (idx + 1) / max(1, (total - n0))
        return _keep(X.boom(w, h, BOOMS[fam] if fam != 'GRCK' else 'dust', min(1, p), seed=hash(fam) % 90), lo, to)
    if fam in SPHERES:
        im = X.blob(w, h, SPHERES[fam], rx=(w / 2 - 1) * (0.92 - 0.05 * (fi % 2)), ry=(h / 2 - 1) * (0.92 - 0.05 * (fi % 2)), seed=3 + fi * 5, rough=0.35, core=0.9)
        return _keep(im, lo, to)
    if fam in ('TFOG', 'IFOG'):
        n = 10 if fam == 'TFOG' else 5
        return _keep(X.sparkle_column(w, h, 'fog' if fam == 'TFOG' else 'gold', fi, n), lo, to)
    if fam in ('FIRE', 'BOSF'):
        return _keep(X.flame_column(w, h, 'fire' if fam == 'FIRE' else 'magic', fi), lo, to)
    if fam == 'PUFF':
        return _keep(X.puff(w, h, fi), lo, to)
    if fam == 'BLUD':
        return _keep(X.splat(w, h, fi), lo, to)
    return None


def _keep(im, lo, to):
    return im, lo, to


def firstperson(fam, fi, rot, k):
    if fam in FP.FLASHES:
        return FP.flash_lump(fam, fi)
    if fam not in FP.FAMILIES:
        return None
    return FP.weapon_lump(fam, fi)


PROVIDERS = [firstperson, fx]
