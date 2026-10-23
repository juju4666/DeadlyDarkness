import functools
import art_props
from art_render import render


@functools.lru_cache(maxsize=256)
def _fig(fam, fi):
    return art_props.PROPS[fam][0](fi)


def props(fam, fi, rot, k):
    if fam not in art_props.PROPS:
        return None
    builder, opt = art_props.PROPS[fam]
    f = _fig(fam, fi)
    facing = opt.get('facing', 0) + opt.get('spin', 0) * fi + (0 if rot in (0, 1) else (rot - 1) * 45)
    return render(f, facing, ppu=k, elev=opt.get('elev', 14))


PROVIDERS = [props]
