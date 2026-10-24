#!/usr/bin/env python3
"""
pack_wad.py - keep the game's content as an ordinary folder tree and build begotten.wad from it.

  python pack_wad.py unpack begotten.wad content        # one-time: WAD -> folder tree
  python pack_wad.py unpack --merge --suffix-dups begotten_poses.wad content   # add another wad to a tree
  python pack_wad.py pack   content begotten.wad        # folder tree -> WAD
  python pack_wad.py check  content                     # validate only (names, sprites, textures ...)
  python pack_wad.py                                    # no arguments: opens the window (no terminal needed)

Needs Python 3.8+ and Pillow (pip install pillow).

Folder layout (the FIRST folder decides what a file is; below it, make as many folders as you like):

  palette/     PLAYPAL, COLORMAP, ENDOOM ... (raw lumps)
  sprites/     *.png  -> picture lumps between S_START / S_END      (name = file name, e.g. SPRTA1.png)
  patches/     *.png  -> picture lumps between P_START / P_END      (wall patches used by textures.txt)
  textures/    *.png  -> each picture is a complete wall texture (patch + texture entry, same name)
  flats/       *.png  -> floor/ceiling flats between F_START / F_END (64x64, 64x128, 128x128 or 256x256)
  graphics/    *.png  -> picture lumps outside any marker (menu, HUD, title screens, BGPIC ...)
  sounds/      *.wav  -> DMX sound lumps (8-bit mono is exact; 16-bit / stereo is converted)
  music/       *.mus *.mid *.ogg ... -> raw music lumps
  colormaps/   raw lumps between C_START / C_END
  maps/        *.wad  -> one small wad per map (open and save it in your map editor); all its lumps are copied
  data/        everything else, raw (BGCLASS.txt, DEHACKED.txt, UMAPINFO.txt, ANIMATED.lmp ...)
  textures.txt composite wall textures (what TEXTURE1 holds), see the README
  wad.cfg      wad type: "type = IWAD" or "type = PWAD"

The lump name is the file name without its extension, in capitals, 8 characters at most.
Two files with the same name in the same group are an error (a trailing ~2, ~3 ... is dropped so
that BGCLASS.txt and BGCLASS~2.txt can both exist: the engine reads every BGCLASS* lump).
PNG offsets (sprite hot spot) live in a "grAb" chunk, the same as SLADE / ZDoom use.
Colours are matched to palette/PLAYPAL.lmp when packing, so a later palette change only needs a re-pack.
Transparent = alpha below 128.
"""
import argparse, io, os, re, struct, sys, wave, zlib

try:
    from PIL import Image
except ImportError:
    sys.exit("pack_wad.py needs Pillow:  pip install pillow")

START = {'S_START': 'S', 'SS_START': 'S', 'P_START': 'P', 'PP_START': 'P', 'P1_START': 'P', 'P2_START': 'P',
         'P3_START': 'P', 'F_START': 'F', 'FF_START': 'F', 'F1_START': 'F', 'F2_START': 'F', 'F3_START': 'F',
         'C_START': 'C'}
END = {'S_END', 'SS_END', 'P_END', 'PP_END', 'P1_END', 'P2_END', 'P3_END', 'F_END', 'FF_END', 'F1_END', 'F2_END',
       'F3_END', 'C_END'}
MAPPARTS = {'THINGS', 'LINEDEFS', 'SIDEDEFS', 'VERTEXES', 'SEGS', 'SSECTORS', 'NODES', 'SECTORS', 'REJECT',
            'BLOCKMAP', 'BEHAVIOR', 'SCRIPTS', 'ZNODES', 'GL_VERT', 'GL_SEGS', 'GL_SSECT', 'GL_NODES', 'GL_PVS'}
PALETTE_LUMPS = {'PLAYPAL', 'PLAYPAL1', 'PLAYPAL2', 'PLAYPAL3', 'PLAYPAL4', 'PLAYPAL5', 'PLAYPAL6', 'PLAYPAL7', 'PLAYPAL8', 'PLAYPAL9', 'COLORMAP', 'ENDOOM', 'GENMIDI', 'DMXGUS', 'DMXGUSC'}
FLAT_SIZES = {4096: (64, 64), 8192: (64, 128), 16384: (128, 128), 65536: (256, 256)}
GROUPS = ['palette', 'sprites', 'patches', 'textures', 'flats', 'graphics', 'sounds', 'music', 'colormaps', 'maps', 'data']
RAW_EXT = {'.lmp', '.txt', '.mus', '.mid', '.midi', '.ogg', '.mp3', '.flac', '.bin', '.deh', '.bex', '.wad'}


def natural(f):
    # NAME.txt first, then NAME~2.txt, NAME~3.txt ... (numbers compared as numbers)
    m = re.fullmatch(r'(.*?)(?:~(\d+))?(\.[^.]*)?', f)
    return (m.group(1).upper(), int(m.group(2) or 0), f)


class Error(Exception):
    pass


# ---------------------------------------------------------------- wad io
def read_wad(path):
    d = open(path, 'rb').read()
    ident, n, off = struct.unpack('<4sii', d[:12])
    if ident not in (b'IWAD', b'PWAD'):
        raise Error("%s is not a WAD" % path)
    out = []
    for i in range(n):
        pos, size, nm = struct.unpack('<ii8s', d[off + i * 16:off + i * 16 + 16])
        out.append((nm.split(b'\0')[0].decode('latin1').upper(), d[pos:pos + size]))
    return ident.decode(), out


def write_wad(path, ident, lumps):
    body, ents, pos = [], [], 12
    for nm, data in lumps:
        ents.append(struct.pack('<ii8s', pos, len(data), nm.encode('latin1').ljust(8, b'\0')))
        body.append(data)
        pos += len(data)
    with open(path, 'wb') as f:
        f.write(ident.encode() + struct.pack('<ii', len(lumps), pos))
        f.write(b''.join(body))
        f.write(b''.join(ents))


# ---------------------------------------------------------------- file names
SAFE = re.compile(r"[A-Z0-9_+!$&'()@#.-]")


def fname(lump):
    return ''.join(c if SAFE.fullmatch(c) else '%%%02X' % ord(c) for c in lump)


def lumpname(stem):
    stem = re.sub(r'~\d+$', '', stem)
    stem = re.sub(r'%([0-9A-Fa-f]{2})', lambda m: chr(int(m.group(1), 16)), stem)
    return stem.upper()


# ---------------------------------------------------------------- pictures
def decode_patch(b, pal):
    """Doom picture -> (RGBA image, left, top) or None if it is not a valid picture."""
    if len(b) < 8:
        return None
    w, h, lo, to = struct.unpack('<hhhh', b[:8])
    if not (0 < w <= 2048 and 0 < h <= 254) or len(b) < 8 + 4 * w:
        return None
    cols = struct.unpack('<%di' % w, b[8:8 + 4 * w])
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    px = img.load()
    for x, o in enumerate(cols):
        if o < 8 + 4 * w or o >= len(b):
            return None
        while True:
            if o >= len(b):
                return None
            top = b[o]
            if top == 255:
                break
            if o + 3 > len(b):
                return None
            ln = b[o + 1]
            if o + 4 + ln > len(b) or top + ln > h:
                return None
            for k in range(ln):
                px[x, top + k] = pal[b[o + 3 + k]] + (255,)
            o += ln + 4
    return img, lo, to


def png_bytes(img, lo, to):
    bio = io.BytesIO()
    img.save(bio, 'PNG')
    d = bio.getvalue()
    if lo or to:
        chunk = struct.pack('>ii', lo, to)
        grab = struct.pack('>I', 8) + b'grAb' + chunk + struct.pack('>I', zlib.crc32(b'grAb' + chunk))
        i = d.index(b'IDAT') - 4
        d = d[:i] + grab + d[i:]
    return d


def png_offsets(d):
    i = 8
    while i + 8 <= len(d):
        ln, kind = struct.unpack('>I4s', d[i:i + 8])
        if kind == b'grAb' and ln == 8:
            return struct.unpack('>ii', d[i + 8:i + 16])
        if kind == b'IDAT':
            break
        i += 12 + ln
    return None


class Palette:
    def __init__(self, data):
        if len(data) < 768:
            raise Error("PLAYPAL is shorter than 768 bytes")
        self.rgb = [tuple(data[i * 3:i * 3 + 3]) for i in range(256)]
        self.cache = {}

    def index(self, c, glow=True):
        """palette index nearest to colour c. glow=False (walls and floors): an exact colour of the glow range (240-254)
        still gives that glow index, but a merely near one never does: glow colours shine at every light level, so an
        approximate match would make a speck of the texture glow in the dark.  Index 255 (the spare) is never used."""
        key = c if glow else (c, 'noglow')
        i = self.cache.get(key)
        if i is None:
            best = None
            for k, p in enumerate(self.rgb):
                if k == 255 or (not glow and k >= 240 and p != tuple(c)):
                    continue
                dd = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2
                if best is None or dd < best:
                    best, i = dd, k
                    if dd == 0:
                        break
            self.cache[key] = i
        return i


def _post(out, top, run, prev):
    """append one post (rows `top`.. of a column) to `out`; `prev` is the absolute top of the previous post of the
    column (-1 for none). Pictures taller than 254 rows use DeePsea's tall patch convention, which the engine reads: a
    post whose topdelta is not larger than the previous post's top is relative to it, otherwise it is absolute. Where
    the distance is too big, empty posts are put in between. Returns the new `prev`."""
    while True:
        if top <= 254 and top > prev:
            out += bytes([top, len(run), run[0]]) + bytes(run) + bytes([run[-1]])
            return top
        d = top - prev
        if prev >= 0 and 0 < d <= min(254, prev):
            out += bytes([d, len(run), run[0]]) + bytes(run) + bytes([run[-1]])
            return top
        # an empty post to step with: the farthest one we can reach that is still above `top`
        q_abs = 254 if prev < 254 else -1
        q_rel = prev + min(254, prev) if prev > 0 else -1
        if q_abs >= q_rel and q_abs > prev:
            out += bytes([q_abs, 0, 0, 0])
            prev = q_abs
        elif q_rel > prev:
            out += bytes([q_rel - prev, 0, 0, 0])
            prev = q_rel
        else:
            raise Error("cannot place a post at row %d" % top)


def encode_patch(pngdata, pal, name, glow=True):
    img = Image.open(io.BytesIO(pngdata)).convert('RGBA')
    w, h = img.size
    if h > 1024 or w > 2048:
        raise Error("%s: picture %dx%d is too large (max 2048x1024)" % (name, w, h))
    off = png_offsets(pngdata) or (0, 0)
    px = img.load()
    cols = []
    for x in range(w):
        out = bytearray()
        prev = -1
        y = 0
        while y < h:
            if px[x, y][3] < 128:
                y += 1
                continue
            y0 = y
            run = bytearray()
            while y < h and px[x, y][3] >= 128 and len(run) < 254:
                run.append(pal.index(px[x, y][:3], glow))
                y += 1
            prev = _post(out, y0, run, prev)
        out.append(255)
        cols.append(bytes(out))
    head = struct.pack('<hhhh', w, h, off[0], off[1])
    pos = 8 + 4 * w
    offs = []
    for c in cols:
        offs.append(pos)
        pos += len(c)
    return head + struct.pack('<%di' % w, *offs) + b''.join(cols)


def decode_flat(b, pal):
    if len(b) not in FLAT_SIZES:
        return None
    w, h = FLAT_SIZES[len(b)]
    img = Image.new('RGB', (w, h))
    img.putdata([pal[i] for i in b])
    return img


def encode_flat(pngdata, pal, name, glow=False):
    img = Image.open(io.BytesIO(pngdata)).convert('RGB')
    if img.size not in FLAT_SIZES.values():
        raise Error("%s: a flat must be 64x64, 64x128, 128x128 or 256x256, this one is %dx%d" % ((name,) + img.size))
    d = img.tobytes()
    return bytes(pal.index((d[i], d[i + 1], d[i + 2]), glow) for i in range(0, len(d), 3))


def decode_sound(b):
    if len(b) < 32 or struct.unpack('<H', b[:2])[0] != 3:
        return None
    rate, n = struct.unpack('<H', b[2:4])[0], struct.unpack('<I', b[4:8])[0]
    if n < 32 or 8 + n > len(b) or rate == 0:
        return None
    samples = b[8 + 16:8 + n - 16]
    bio = io.BytesIO()
    with wave.open(bio, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(1)
        wf.setframerate(rate)
        wf.writeframes(samples)
    return bio.getvalue()


def encode_sound(wavdata, name):
    with wave.open(io.BytesIO(wavdata), 'rb') as wf:
        ch, sw, rate, nf = wf.getnchannels(), wf.getsampwidth(), wf.getframerate(), wf.getnframes()
        raw = wf.readframes(nf)
    if sw == 1:
        vals = list(raw)
    elif sw == 2:
        vals = [(struct.unpack('<h', raw[i:i + 2])[0] >> 8) + 128 for i in range(0, len(raw), 2)]
    else:
        raise Error("%s: only 8 or 16 bit WAV files are supported" % name)
    if ch > 1:
        vals = [sum(vals[i:i + ch]) // ch for i in range(0, len(vals), ch)]
    s = bytes(vals)
    if not s:
        raise Error("%s: empty sound" % name)
    return struct.pack('<HHI', 3, rate, len(s) + 32) + s[:1] * 16 + s + s[-1:] * 16


def is_text(b):
    return len(b) > 0 and all(c in b'\t\r\n' or 32 <= c < 127 or c >= 160 for c in b)


def music_ext(b):
    for magic, ext in ((b'MUS\x1a', '.mus'), (b'MThd', '.mid'), (b'OggS', '.ogg'), (b'ID3', '.mp3'), (b'fLaC', '.flac')):
        if b.startswith(magic):
            return ext
    return '.lmp'


# ---------------------------------------------------------------- unpack
def unpack(src, dst, merge=False, suffix_dups=False):
    ident, lumps = read_wad(src)
    os.makedirs(dst, exist_ok=True)
    if not merge and os.listdir(dst):
        raise Error("%s is not empty (use --merge to add to an existing tree)" % dst)
    cfg = os.path.join(dst, 'wad.cfg')
    if not os.path.exists(cfg):
        open(cfg, 'w').write("type = %s\n" % ident)
    palf = os.path.join(dst, 'palette', 'PLAYPAL.lmp')
    if os.path.exists(palf):
        play = open(palf, 'rb').read()
    else:
        play = next((d for n, d in lumps if n == 'PLAYPAL'), None)
    if play is None:
        raise Error("no PLAYPAL in %s or in the tree: needed to turn pictures into PNG" % src)
    pal = Palette(play).rgb
    stats = {}
    written = set()

    def put(group, sub, name, ext, data):
        d = os.path.join(dst, group, sub) if sub else os.path.join(dst, group)
        os.makedirs(d, exist_ok=True)
        base = fname(name)
        p = os.path.join(d, base + ext)
        k = 2
        while os.path.exists(p) or os.path.normcase(p) in written:
            if not suffix_dups:
                raise Error("lump %s exists twice (%s); use --suffix-dups to keep both" % (name, group))
            p = os.path.join(d, '%s~%d%s' % (base, k, ext))
            k += 1
        written.add(os.path.normcase(p))
        open(p, 'wb').write(data)
        stats[group] = stats.get(group, 0) + 1
        return p

    def picture(group, sub, name, data):
        r = decode_patch(data, pal)
        if r:
            put(group, sub, name, '.png', png_bytes(*r))
        elif is_text(data):
            put('data', '', name, '.txt', data)   # a text lump that sits between the markers
        else:
            put(group, sub, name, '.lmp', data)

    ns, i, n = None, 0, len(lumps)
    while i < n:
        name, data = lumps[i]
        if name in START:
            ns = START[name]
            i += 1
            continue
        if name in END:
            ns = None
            i += 1
            continue
        if ns == 'S':
            picture('sprites', name[:4], name, data)
        elif ns == 'P':
            picture('patches', '', name, data)
        elif ns == 'F':
            img = decode_flat(data, pal)
            if img:
                bio = io.BytesIO()
                img.save(bio, 'PNG')
                put('flats', '', name, '.png', bio.getvalue())
            else:
                put('flats', '', name, '.lmp', data)
        elif ns == 'C':
            put('colormaps', '', name, '.lmp', data)
        elif re.fullmatch(r'MAP\d\d|E\dM\d', name):
            j = i + 1
            while j < n and lumps[j][0] in MAPPARTS:
                j += 1
            mp = put('maps', '', name, '.wad', b'')
            write_wad(mp, 'PWAD', lumps[i:j])
            i = j
            continue
        elif name in ('TEXTURE1', 'TEXTURE2', 'PNAMES'):
            pass  # rebuilt from textures.txt
        elif name in PALETTE_LUMPS:
            put('palette', '', name, '.lmp', data)
        elif name.startswith('DS') and decode_sound(data):
            put('sounds', '', name, '.wav', decode_sound(data))
        elif name.startswith(('D_', 'DP', 'MUS')) and (music_ext(data) != '.lmp' or name.startswith('D_')):
            put('music', '', name, music_ext(data), data)
        elif is_text(data):
            put('data', '', name, '.txt', data)
        else:
            r = decode_patch(data, pal)
            if r and len(data) > 8:
                put('graphics', '', name, '.png', png_bytes(*r))
            else:
                put('data', '', name, '.lmp', data)
        i += 1

    # textures.txt from TEXTURE1/2 + PNAMES
    pn = next((d for nm, d in lumps if nm == 'PNAMES'), None)
    tx = [d for nm, d in lumps if nm in ('TEXTURE1', 'TEXTURE2')]
    if pn and tx:
        cnt = struct.unpack('<i', pn[:4])[0]
        pnames = [pn[4 + 8 * k:12 + 8 * k].split(b'\0')[0].decode('latin1').upper() for k in range(cnt)]
        out = []
        for t in tx:
            nt = struct.unpack('<i', t[:4])[0]
            offs = struct.unpack('<%di' % nt, t[4:4 + 4 * nt])
            for o in offs:
                nm = t[o:o + 8].split(b'\0')[0].decode('latin1').upper()
                w, h = struct.unpack('<hh', t[o + 12:o + 16])
                npat = struct.unpack('<h', t[o + 20:o + 22])[0]
                out.append("%s %d %d" % (nm, w, h))
                for k in range(npat):
                    ox, oy, pi = struct.unpack('<hhh', t[o + 22 + 10 * k:o + 28 + 10 * k])
                    out.append("  %s %d %d" % (pnames[pi], ox, oy))
        tp = os.path.join(dst, 'textures.txt')
        mode = 'a' if (merge and os.path.exists(tp)) else 'w'
        with open(tp, mode) as f:
            if mode == 'w':
                f.write("# composite wall textures: NAME width height, then one indented line per patch: PATCH x y\n")
            f.write('\n'.join(out) + '\n')
        stats['textures.txt'] = sum(1 for l in out if not l.startswith(' '))
    return stats


# ---------------------------------------------------------------- pack
class Tree:
    def __init__(self, root):
        self.root = root
        if not os.path.isdir(root):
            raise Error("%s is not a folder" % root)
        self.errors, self.warnings = [], []
        pf = os.path.join(root, 'palette', 'PLAYPAL.lmp')
        if not os.path.exists(pf):
            raise Error("%s is missing" % pf)
        self.pal = Palette(open(pf, 'rb').read())

    def files(self, group):
        base = os.path.join(self.root, group)
        out = []
        for dp, dn, fn in os.walk(base):
            dn.sort()
            for f in sorted(fn, key=natural):
                out.append(os.path.join(dp, f))
        return out

    def rel(self, p):
        return os.path.relpath(p, self.root).replace(os.sep, '/')

    def lumpfor(self, p):
        stem, ext = os.path.splitext(os.path.basename(p))
        nm = lumpname(stem)
        if len(nm) > 8 or not nm:
            raise Error("%s: lump names are at most 8 characters (%r)" % (self.rel(p), nm))
        return nm, ext.lower()

    def build(self):
        pal = self.pal
        lumps, seen = [], {}

        def add(group, nm, data, p):
            key = (group, nm)
            if key in seen and group != 'maps':
                self.errors.append("duplicate lump %s in %s: %s and %s" % (nm, group, seen[key], self.rel(p)))
            seen[key] = self.rel(p)
            lumps.append((nm, data))

        def raw(group):
            for p in self.files(group):
                nm, ext = self.lumpfor(p)
                add(group, nm, open(p, 'rb').read(), p)

        def pictures(group, flats=False, glow=True):
            out = []
            for p in self.files(group):
                nm, ext = self.lumpfor(p)
                data = open(p, 'rb').read()
                try:
                    if ext == '.png':
                        data = encode_flat(data, pal, self.rel(p)) if flats else encode_patch(data, pal, self.rel(p), glow)
                except Error as e:
                    self.errors.append(str(e))
                    continue
                add(group, nm, data, p)
                out.append((nm, data))
            return out

        raw('palette')
        raw('data')
        pictures('graphics')
        for p in self.files('sounds'):
            nm, ext = self.lumpfor(p)
            data = open(p, 'rb').read()
            if ext == '.wav':
                try:
                    data = encode_sound(data, self.rel(p))
                except (Error, wave.Error) as e:
                    self.errors.append("%s: %s" % (self.rel(p), e))
                    continue
            add('sounds', nm, data, p)
        raw('music')
        # maps
        for p in self.files('maps'):
            if p.lower().endswith('.wad'):
                try:
                    _, ml = read_wad(p)
                except Error as e:
                    self.errors.append("%s: %s" % (self.rel(p), e))
                    continue
                if not ml:
                    self.errors.append("%s: the map file is empty" % self.rel(p))
                lumps.extend(ml)
            else:
                self.errors.append("%s: only .wad files go in maps/" % self.rel(p))
        # sprites
        lumps.append(('S_START', b''))
        sprites = pictures('sprites')
        lumps.append(('S_END', b''))
        self.check_sprites([n for n, _ in sprites])
        # patches + wall textures
        lumps.append(('P_START', b''))
        patches = pictures('patches', glow=False)    # (walls and floors do not glow unless a colour is exactly a glow colour)
        texs = pictures('textures', glow=False)
        lumps.append(('P_END', b''))
        lumps.append(('F_START', b''))
        pictures('flats', flats=True)
        lumps.append(('F_END', b''))
        cm = self.files('colormaps')
        if cm:
            lumps.append(('C_START', b''))
            raw('colormaps')
            lumps.append(('C_END', b''))
        lumps.extend(self.texture_lumps(patches, texs))
        return lumps

    def check_sprites(self, names):
        frames = {}
        for n in names:
            if len(n) < 6:
                self.errors.append("sprite lump %s: name must be 4 letters + frame + rotation" % n)
                continue
            for k in range(4, len(n) - 1, 2):
                frames.setdefault((n[:4], n[k]), set()).add(n[k + 1])
        for (spr, fr), rots in sorted(frames.items()):
            r = set(rots)
            if '0' in r:
                continue
            ok8 = all(str(i) in r for i in range(1, 9))
            ok16 = ok8 and all(c in r for c in '9ABCDEFG')
            if not ok8:
                missing = [str(i) for i in range(1, 9) if str(i) not in r]
                self.errors.append("sprite %s frame %s: rotations %s missing (the engine refuses incomplete frames)" %
                                   (spr, fr, ','.join(missing)))
            elif any(c in r for c in '9ABCDEFG') and not ok16:
                self.errors.append("sprite %s frame %s: has some of the extra rotations 9-G but not all" % (spr, fr))

    def texture_lumps(self, patches, texs):
        pnames, defs = [], []
        have = {n for n, _ in patches} | {n for n, _ in texs}

        def pidx(n):
            if n not in pnames:
                pnames.append(n)
            return pnames.index(n)

        tp = os.path.join(self.root, 'textures.txt')
        if os.path.exists(tp):
            cur = None
            for ln, line in enumerate(open(tp, encoding='latin1'), 1):
                s = line.split('#')[0].rstrip()
                if not s.strip():
                    continue
                t = s.split()
                try:
                    if s[0] in ' \t':
                        if cur is None:
                            raise ValueError
                        if t[0].upper() not in have and not self.lump_anywhere(t[0].upper()):
                            self.errors.append("textures.txt:%d: patch %s does not exist" % (ln, t[0].upper()))
                        cur[2].append((pidx(t[0].upper()), int(t[1]), int(t[2])))
                    else:
                        cur = [t[0].upper(), (int(t[1]), int(t[2])), []]
                        if len(cur[0]) > 8:
                            raise ValueError
                        defs.append(cur)
                except (ValueError, IndexError):
                    self.errors.append("textures.txt:%d: cannot read %r" % (ln, s.strip()))
        names = {d[0] for d in defs}
        for n, data in texs:
            if n in names:
                self.errors.append("texture %s is defined both in textures.txt and in textures/" % n)
                continue
            w, h = struct.unpack('<hh', data[:4])
            defs.append([n, (w, h), [(pidx(n), 0, 0)]])
        if not defs:
            return []
        if len(pnames) > 32767:
            self.errors.append("too many patches in textures")
        pn = struct.pack('<i', len(pnames)) + b''.join(p.encode('latin1').ljust(8, b'\0') for p in pnames)
        bodies = []
        for nm, (w, h), pl in defs:
            b = nm.encode('latin1').ljust(8, b'\0') + struct.pack('<I', 0) + struct.pack('<hh', w, h) + struct.pack('<I', 0) + struct.pack('<h', len(pl))
            for pi, x, y in pl:
                b += struct.pack('<hhhhh', x, y, pi, 1, 0)
            bodies.append(b)
        head = 4 + 4 * len(bodies)
        offs, pos = [], head
        for b in bodies:
            offs.append(pos)
            pos += len(b)
        t1 = struct.pack('<i', len(bodies)) + struct.pack('<%di' % len(bodies), *offs) + b''.join(bodies)
        return [('PNAMES', pn), ('TEXTURE1', t1)]

    def lump_anywhere(self, nm):
        for g in GROUPS:
            for p in self.files(g):
                if lumpname(os.path.splitext(os.path.basename(p))[0]) == nm:
                    return True
        return False


def pack(src, dst, check_only=False):
    t = Tree(src)
    lumps = t.build()
    for w in t.warnings:
        print("warning:", w)
    if t.errors:
        for e in t.errors:
            print("ERROR:", e)
        raise Error("%d problem(s); nothing written" % len(t.errors))
    ident = 'IWAD'
    cfg = os.path.join(src, 'wad.cfg')
    if os.path.exists(cfg):
        m = re.search(r'type\s*=\s*(IWAD|PWAD)', open(cfg).read(), re.I)
        if m:
            ident = m.group(1).upper()
    if not check_only:
        write_wad(dst, ident, lumps)
    return ident, lumps


# ---------------------------------------------------------------- window
def gui():
    import contextlib, json, queue, threading
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    cfgfile = os.path.join(os.path.expanduser('~'), '.begotten_pack_wad.json')
    try:
        saved = json.load(open(cfgfile))
    except (OSError, ValueError):
        saved = {}

    root = tk.Tk()
    root.title("Begotten - WAD packer")
    root.geometry("720x480")
    root.minsize(600, 400)
    folder, wadv, merge = tk.StringVar(value=saved.get('folder', '')), tk.StringVar(value=saved.get('wad', '')), \
        tk.BooleanVar(value=False)
    q = queue.Queue()
    buttons = []

    frm = ttk.Frame(root, padding=12)
    frm.pack(fill='both', expand=True)
    frm.columnconfigure(1, weight=1)
    frm.rowconfigure(5, weight=1)

    def pick_folder():
        d = filedialog.askdirectory(title="Content folder", initialdir=folder.get() or None)
        if d:
            folder.set(d)

    def pick_wad_open():
        f = filedialog.askopenfilename(title="WAD to unpack", initialdir=os.path.dirname(wadv.get()) or None,
                                       filetypes=[("WAD files", "*.wad"), ("All files", "*.*")])
        if f:
            wadv.set(f)

    def pick_wad_save():
        f = filedialog.asksaveasfilename(title="WAD to write", defaultextension=".wad",
                                         initialfile=os.path.basename(wadv.get()) or "begotten.wad",
                                         initialdir=os.path.dirname(wadv.get()) or None,
                                         filetypes=[("WAD files", "*.wad")])
        if f:
            wadv.set(f)

    ttk.Label(frm, text="Content folder").grid(row=0, column=0, sticky='w')
    ttk.Entry(frm, textvariable=folder, width=20).grid(row=0, column=1, sticky='ew', padx=6, pady=3)
    ttk.Button(frm, text="Browse...", command=pick_folder).grid(row=0, column=2)
    ttk.Label(frm, text="WAD file").grid(row=1, column=0, sticky='w')
    ttk.Entry(frm, textvariable=wadv, width=20).grid(row=1, column=1, sticky='ew', padx=6, pady=3)
    wbtn = ttk.Frame(frm)
    wbtn.grid(row=1, column=2)
    ttk.Button(wbtn, text="Open...", command=pick_wad_open).pack(side='left')
    ttk.Button(wbtn, text="Save as...", command=pick_wad_save).pack(side='left', padx=(4, 0))

    bar = ttk.Frame(frm)
    bar.grid(row=2, column=0, columnspan=3, sticky='ew', pady=(10, 2))
    logbox = ttk.Frame(frm)
    logbox.grid(row=5, column=0, columnspan=3, sticky='nsew', pady=(8, 0))
    log = tk.Text(logbox, height=12, wrap='word', state='disabled')
    sb = ttk.Scrollbar(logbox, command=log.yview)
    log['yscrollcommand'] = sb.set
    sb.pack(side='right', fill='y')
    log.pack(side='left', fill='both', expand=True)

    def say(t):
        log['state'] = 'normal'
        log.insert('end', t)
        log.see('end')
        log['state'] = 'disabled'

    def run(job, label):
        if not folder.get().strip():
            messagebox.showwarning("Begotten", "Choose the content folder first.")
            return
        for b in buttons:
            b['state'] = 'disabled'
        say("\n== %s ==\n" % label)
        try:
            json.dump({'folder': folder.get(), 'wad': wadv.get()}, open(cfgfile, 'w'))
        except OSError:
            pass

        class W(io.TextIOBase):
            def write(self, t):
                q.put(t)
                return len(t)

        def work():
            try:
                with contextlib.redirect_stdout(W()):
                    job()
                q.put("\nDone.\n")
            except Error as e:
                q.put("\nERROR: %s\n" % e)
            except Exception as e:  # keep the window alive whatever happens
                q.put("\nUNEXPECTED ERROR: %r\n" % (e,))
            q.put(None)

        threading.Thread(target=work, daemon=True).start()

    def do_pack():
        if not wadv.get().strip():
            return messagebox.showwarning("Begotten", "Choose the WAD file to write (Save as...).")
        if os.path.exists(wadv.get()) and not messagebox.askyesno(
                "Begotten", "%s exists. Replace it?" % os.path.basename(wadv.get())):
            return

        def job():
            ident, l = pack(folder.get(), wadv.get())
            print("wrote %s: %s, %d lumps" % (wadv.get(), ident, len(l)))
        run(job, "Pack folder into WAD")

    def do_unpack():
        if not os.path.isfile(wadv.get()):
            return messagebox.showwarning("Begotten", "Choose the WAD to unpack (Open...).")
        if os.path.isdir(folder.get()) and os.listdir(folder.get()) and not merge.get():
            return messagebox.showwarning(
                "Begotten", "The folder is not empty. Tick \"Add to a folder that already has content\" "
                            "or choose an empty folder.")

        def job():
            st = unpack(wadv.get(), folder.get(), merge.get(), merge.get())
            print("unpacked: " + ', '.join("%s %d" % kv for kv in sorted(st.items())))
        run(job, "Unpack WAD into folder")

    def do_check():
        def job():
            ident, l = pack(folder.get(), None, True)
            print("ok: %d lumps, nothing wrong found" % len(l))
        run(job, "Check folder")

    def do_open():
        d = folder.get()
        if not os.path.isdir(d):
            return
        if sys.platform.startswith('win'):
            os.startfile(d)
        else:
            import subprocess
            subprocess.Popen(['open' if sys.platform == 'darwin' else 'xdg-open', d])

    for text, cmd in (("Pack folder into WAD", do_pack), ("Unpack WAD into folder", do_unpack),
                      ("Check folder", do_check)):
        b = ttk.Button(bar, text=text, command=cmd)
        b.pack(side='left', padx=(0, 6))
        buttons.append(b)
    ttk.Button(bar, text="Open folder", command=do_open).pack(side='right')
    ttk.Checkbutton(frm, text="Add to a folder that already has content (merge; for adding a second WAD)",
                    variable=merge).grid(row=3, column=0, columnspan=3, sticky='w')
    ttk.Label(frm, text="Pack: needs the content folder and where to write the WAD.   "
                        "Unpack: needs the WAD and the folder to fill.", foreground='gray', wraplength=680).grid(
        row=4, column=0, columnspan=3, sticky='w', pady=(4, 0))

    def pump():
        try:
            while True:
                t = q.get_nowait()
                if t is None:
                    for b in buttons:
                        b['state'] = 'normal'
                else:
                    say(t)
        except queue.Empty:
            pass
        root.after(80, pump)

    pump()
    say("Pick the content folder and a WAD, then press a button.\n")
    root.mainloop()



def main():
    if len(sys.argv) == 1:
        return gui()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a = sub.add_parser('unpack')
    a.add_argument('wad')
    a.add_argument('folder')
    a.add_argument('--merge', action='store_true', help='add to a folder that already has content')
    a.add_argument('--suffix-dups', action='store_true', help='keep lumps whose name already exists (NAME~2)')
    b = sub.add_parser('pack')
    b.add_argument('folder')
    b.add_argument('wad')
    c = sub.add_parser('check')
    c.add_argument('folder')
    args = ap.parse_args()
    try:
        if args.cmd == 'unpack':
            st = unpack(args.wad, args.folder, args.merge, args.suffix_dups)
            print("unpacked: " + ', '.join("%s %d" % kv for kv in sorted(st.items())))
        elif args.cmd == 'pack':
            ident, l = pack(args.folder, args.wad)
            print("wrote %s: %s, %d lumps" % (args.wad, ident, len(l)))
        else:
            ident, l = pack(args.folder, None, True)
            print("ok: %d lumps, nothing wrong found" % len(l))
    except Error as e:
        sys.exit("error: %s" % e)


if __name__ == '__main__':
    main()
