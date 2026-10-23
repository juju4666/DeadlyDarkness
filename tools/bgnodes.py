#!/usr/bin/env python3
"""Begotten: build the nodes of a map wad with ZDBSP, outside the map editor.

usage: bgnodes.py MAP.wad [OUT.wad] [--zdbsp PATH]

Why: UDB under Wine / Lutris can crash right after saving, in its own "building map nodes" step.  Turn the nodebuilder
off in UDB (or just let it crash: the map is already written), then run this on the saved file.  The engine needs
nodes; it does not build them itself.
Without OUT.wad the file is rewritten in place (the old one is kept as MAP.wad.bak).
ZDBSP is looked for in --zdbsp, $ZDBSP, next to this script, on PATH, and in any "Compilers/Nodebuilders" folder
below the current folder (the Compilers.zip of the UDB setup).
"""
import glob, os, shutil, subprocess, sys


def find_zdbsp(given):
    names = ['zdbsp.exe', 'zdbsp'] if os.name == 'nt' else ['zdbsp', 'zdbsp.exe']
    cands = [given, os.environ.get('ZDBSP')]
    here = os.path.dirname(os.path.abspath(__file__))
    for n in names:
        cands += [os.path.join(here, n), shutil.which(n)]
        cands += glob.glob(os.path.join('**', 'Nodebuilders', n), recursive=True)
    for c in cands:
        if c and os.path.isfile(c):
            return c
    return None


def main(argv):
    given = None
    if '--zdbsp' in argv:
        i = argv.index('--zdbsp')
        given = argv[i + 1]
        del argv[i:i + 2]
    if not 1 <= len(argv) <= 2:
        print(__doc__)
        return 1
    src = argv[0]
    out = argv[1] if len(argv) > 1 else None
    z = find_zdbsp(given)
    if not z:
        print('zdbsp not found: pass --zdbsp PATH or set ZDBSP')
        return 2
    tmp = (out or src) + '.nodes.tmp'
    r = subprocess.run([z, '-w', '-t', '-o', tmp, src])
    if r.returncode != 0 or not os.path.exists(tmp):
        print('zdbsp failed (see above); nothing was changed')
        return 3
    if out is None:
        shutil.copy2(src, src + '.bak')
        os.replace(tmp, src)
        print('nodes built in %s (old file: %s.bak)' % (src, src))
    else:
        os.replace(tmp, out)
        print('nodes built: %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
