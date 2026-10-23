"""providers for monsters and player classes"""
import functools
import art_monsters, art_monsters2, art_classes, art_class_weapons
from art_render import render
from art_apply import facing_for

MON = art_monsters.MONSTERS
# unused stock families get the nearest drawn monster
ALIAS = {'POSS': 'RTRP', 'SPOS': 'RTRP', 'SSWV': 'STRP', 'CPOS': 'STRP', 'BSPI': 'BEET'}
CLASS_OF = {'PLAY': 'S', 'PLYC': 'S', 'SRUN': 'S', 'SSHT': 'S', 'SJMP': 'S', 'SRFL': 'S', 'SRFS': 'S',
            'WPLY': 'W', 'XPLY': 'X', 'WRUN': 'W', 'WSHT': 'W', 'WJMP': 'W', 'XRUN': 'X', 'XSHT': 'X', 'XJMP': 'X', 'XPAR': 'X'}


@functools.lru_cache(maxsize=64)
def _fig(fam, fi):
    return MON[fam].figure(fi)


@functools.lru_cache(maxsize=64)
def _cfig(fam, fi):
    if fam in art_class_weapons.FAMILIES:
        return art_class_weapons.figure(art_classes.CLASSES[art_class_weapons.FAMILIES[fam][0]], fam, fi)
    return art_classes.CLASSES[CLASS_OF[fam]].figure(fam, fi)


def monsters(fam, fi, rot, k):
    key = ALIAS.get(fam, fam)
    if key not in MON:
        return None
    return render(_fig(key, fi), facing_for(rot), ppu=k)


def classes(fam, fi, rot, k):
    if fam not in CLASS_OF and fam not in art_class_weapons.FAMILIES:
        return None
    return render(_cfig(fam, fi), facing_for(rot), ppu=k)


PROVIDERS = [monsters, classes]
