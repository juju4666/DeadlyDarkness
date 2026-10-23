"""art_apply.py - replace the labelled placeholder sprites in a content tree with drawn art.

apply(tree, k): for every sprite lump in tree/sprites whose family a provider knows, render it (k pixels per map unit) and
write it over the placeholder with its hot spot.  Families nobody knows keep their placeholder."""
import os, re, sys, glob, io
from urllib.parse import unquote
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from pack_wad import png_bytes, png_offsets
from art_render import render

PROVIDERS = []          # list of (family set or None, function(fam, frame_idx, rot, k) -> (img, ox, oy) | None)


def parse_stem(stem):
    s = unquote(stem)
    fam, frame, rot = s[:4], s[4:5], s[5:6]
    extra = s[6:]
    return fam, frame, rot, extra


def facing_for(rot, zero=20.0):
    r = int(rot)
    return zero if r == 0 else (r - 1) * 45.0


def job(args):
    global CUR
    path, fam, frame, rot, k = args
    raw = open(path, 'rb').read()
    w0, h0 = Image.open(io.BytesIO(raw)).size
    lo0, to0 = png_offsets(raw) or (0, 0)
    CUR = (w0, h0, lo0, to0)
    fi = ord(frame) - 65
    for prov in PROVIDERS_RUN:
        res = prov(fam, fi, int(rot), k)
        if res is not None:
            img, ox, oy = res
            out = png_bytes(img, ox, oy)
            open(path, 'wb').write(out)
            return fam
    return None


CUR = (1, 1, 0, 0)
PROVIDERS_RUN = []


def load_providers():
    global PROVIDERS_RUN
    PROVIDERS_RUN = []
    import art_monsters, art_monsters2, art_classes
    mods = ['art_stage1_providers']
    for m in mods:
        mod = __import__(m)
        PROVIDERS_RUN.extend(mod.PROVIDERS)
    import art_props_providers, art_fx_providers
    PROVIDERS_RUN.extend(art_fx_providers.PROVIDERS)
    PROVIDERS_RUN.extend(art_props_providers.PROVIDERS)


def apply(tree, k, only=None, procs=2):
    load_providers()
    jobs = []
    for p in glob.glob(os.path.join(tree, 'sprites', '**', '*.png'), recursive=True):
        stem = os.path.splitext(os.path.basename(p))[0]
        fam, frame, rot, extra = parse_stem(stem)
        if only and fam not in only:
            continue
        if not rot.isdigit() or (extra and not (extra == '0' and rot == '0')):
            continue
        jobs.append((p, fam, frame, rot, k))
    # biggest families first so the two workers finish together
    jobs.sort(key=lambda j: (j[1] not in ('BEET', 'MECH'), j[1]))
    done = {}
    if procs > 1:
        with Pool(procs, initializer=load_providers) as pool:
            for fam in pool.imap_unordered(job, jobs, chunksize=8):
                done[fam] = done.get(fam, 0) + 1
    else:
        for j in jobs:
            fam = job(j)
            done[fam] = done.get(fam, 0) + 1
    return done
