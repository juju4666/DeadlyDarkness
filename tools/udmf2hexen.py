#!/usr/bin/env python3
"""Convert a UDMF map wad (one map) to a binary Doom-in-Hexen map wad.  usage: udmf2hexen.py IN.wad OUT.wad [--clear TEXTURE]
--clear fills every upper/lower wall part that UDB would show as "missing" with that (transparent) texture.
Nodes are not built (run bgnodes.py afterwards).  Lost: per-part texture offsets (the sidedef gets the offset of its
visible part), sector flat panning/rotation/scale, and every other UDMF-only field."""
import re, struct, sys

def lumps(path):
    d = open(path, 'rb').read(); n, o = struct.unpack('<ii', d[4:12]); r = []
    for i in range(n):
        p, s, nm = struct.unpack('<ii8s', d[o+16*i:o+16*i+16]); r.append((nm.rstrip(b'\0').decode(), d[p:p+s]))
    return r

def parse(text):
    text = re.sub(r'//[^\n]*', '', text); text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    blocks = {}
    for kind, body in re.findall(r'(\w+)\s*\{(.*?)\}', text, flags=re.S):
        f = {}
        for k, v in re.findall(r'(\w+)\s*=\s*([^;]+);', body):
            v = v.strip()
            f[k.lower()] = v.strip('"') if v.startswith('"') else (v == 'true' if v in ('true', 'false') else float(v))
        blocks.setdefault(kind.lower(), []).append(f)
    return blocks

def tex(s): return (s or '-').upper().ljust(8, '\0')[:8].encode('ascii') if s != '-' else b'-\0\0\0\0\0\0\0'
def i16(v): return max(-32768, min(32767, int(round(v))))

def convert(src, dst, clear=None):
    L = lumps(src); tm = next(d for n, d in L if n == 'TEXTMAP').decode('latin-1'); name = L[0][0]
    B = parse(tm); g = lambda f, k, d=0: f.get(k, d)
    if clear:
        S, D = B['sector'], B['sidedef']
        for l in B['linedef']:
            f, b = int(l.get('sidefront', -1)), int(l.get('sideback', -1))
            if f < 0 or b < 0: continue
            for a, o in ((f, b), (b, f)):
                sa, so, d = S[int(D[a]['sector'])], S[int(D[o]['sector'])], D[a]
                if sa.get('heightceiling', 0) > so.get('heightceiling', 0) and 'texturetop' not in d: d['texturetop'] = clear
                if sa.get('heightfloor', 0) < so.get('heightfloor', 0) and 'texturebottom' not in d: d['texturebottom'] = clear
    th = b''
    for t in B.get('thing', []):
        fl = (1 if g(t,'skill1',False) or g(t,'skill2',False) else 0) | (2 if g(t,'skill3',False) else 0) | \
             (4 if g(t,'skill4',False) or g(t,'skill5',False) else 0) | (8 if g(t,'ambush',False) else 0) | \
             (0x10 if g(t,'dormant',False) else 0) | (0x20 if g(t,'class1',False) else 0) | (0x40 if g(t,'class2',False) else 0) | \
             (0x80 if g(t,'class3',False) else 0) | (0x100 if g(t,'single',False) else 0) | (0x200 if g(t,'coop',False) else 0) | \
             (0x400 if g(t,'dm',False) else 0)
        a = [int(g(t, 'arg%d' % i)) & 255 for i in range(5)]
        th += struct.pack('<hhhhhhhB5B', i16(g(t,'id')), i16(g(t,'x')), i16(g(t,'y')), i16(g(t,'height')), i16(g(t,'angle')),
                          i16(g(t,'type')), fl, int(g(t,'special')) & 255, *a)
    vx = b''.join(struct.pack('<hh', i16(v['x']), i16(v['y'])) for v in B['vertex'])
    ld = b''
    for l in B['linedef']:
        fl = (1 if g(l,'blocking',False) else 0) | (2 if g(l,'blockmonsters',False) else 0) | (4 if g(l,'twosided',False) else 0) | \
             (8 if g(l,'dontpegtop',False) else 0) | (16 if g(l,'dontpegbottom',False) else 0) | (32 if g(l,'secret',False) else 0) | \
             (64 if g(l,'blocksound',False) else 0) | (128 if g(l,'dontdraw',False) else 0) | (256 if g(l,'mapped',False) else 0) | \
             (512 if g(l,'repeatspecial',False) else 0)
        act = 0
        for i, k in enumerate(['playercross', 'playeruse', 'monstercross', 'impact', 'playerpush', 'missilecross']):
            if g(l, k, False): act = i
        fl |= act << 10
        a = [int(g(l, 'arg%d' % i)) & 255 for i in range(5)]
        ld += struct.pack('<HHHB5BhH'[:-1] + 'h', int(l['v1']), int(l['v2']), fl, int(g(l,'special')) & 255, *a,
                          int(g(l,'sidefront',-1)), int(g(l,'sideback',-1)))
    sd = b''
    for s in B['sidedef']:
        up, lo, mi = s.get('texturetop', '-'), s.get('texturebottom', '-'), s.get('texturemiddle', '-')
        off = g(s,'offsetx_mid') if mi != '-' else 0
        for part, k in ((up, 'offsetx_top'), (lo, 'offsetx_bottom')):
            if not off and part != '-': off = g(s, k)
        off = off or g(s, 'offsetx')
        yoff = g(s,'offsety_mid') if mi != '-' else (g(s,'offsety_top') or g(s,'offsety_bottom') or g(s,'offsety'))
        sd += struct.pack('<hh8s8s8sH', i16(off), i16(yoff), tex(up), tex(lo), tex(mi), int(s['sector']))
    sc = b''
    for s in B['sector']:
        sc += struct.pack('<hh8s8shhh', i16(g(s,'heightfloor')), i16(g(s,'heightceiling')), tex(s.get('texturefloor','-')),
                          tex(s.get('textureceiling','-')), i16(g(s,'lightlevel',160)), i16(g(s,'special')), i16(g(s,'id')))
    out = [(name, b''), ('THINGS', th), ('LINEDEFS', ld), ('SIDEDEFS', sd), ('VERTEXES', vx), ('SEGS', b''), ('SSECTORS', b''),
           ('NODES', b''), ('SECTORS', sc), ('REJECT', b''), ('BLOCKMAP', b''), ('BEHAVIOR', b'ACS\0' + struct.pack('<ii', 8, 0))]
    data = b''; ents = []; pos = 12
    for n, d in out:
        ents.append((pos, len(d), n)); data += d; pos += len(d)
    dirr = b''.join(struct.pack('<ii8s', p, s, n.encode().ljust(8, b'\0')) for p, s, n in ents)
    open(dst, 'wb').write(b'PWAD' + struct.pack('<ii', len(out), 12 + len(data)) + data + dirr)
    print('things %d  lines %d  sides %d  vertices %d  sectors %d' % (len(B.get('thing', [])), len(B['linedef']), len(B['sidedef']), len(B['vertex']), len(B['sector'])))

if __name__ == '__main__':
    a = sys.argv[1:]
    convert(a[0], a[1], a[a.index('--clear') + 1] if '--clear' in a else None)
