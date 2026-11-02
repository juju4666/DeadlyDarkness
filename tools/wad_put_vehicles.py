#!/usr/bin/env python3
"""
wad_put_vehicles.py - puts the vehicle lumps (game/vehicles) into a begotten.wad WITHOUT touching anything else.

    python wad_put_vehicles.py IN.wad OUT.wad [game/vehicles]

Lumps that exist are replaced, new ones go in: BGVOXELV after BGVOXEL, the voxel models after VXCARD, the editor
icons before S_END. (tools/pack_wad.py drops UDMF map lumps, so a wad with maps in it must be edited like this.)
"""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_wad


def read_wad(path):
    d = open(path, 'rb').read()
    n, off = struct.unpack('<ii', d[4:12])
    lumps = []
    for i in range(n):
        p, sz, nm = struct.unpack('<ii8s', d[off + 16 * i:off + 16 * i + 16])
        lumps.append([nm.rstrip(b'\0').decode(), d[p:p + sz]])
    return d[:4], lumps


def write_wad(path, ident, lumps):
    parts, dirs, pos = [b'\0' * 12], [], 12
    for nm, data in lumps:
        dirs.append(struct.pack('<ii8s', pos if data else 0, len(data), nm.encode().ljust(8, b'\0')))
        parts.append(data)
        pos += len(data)
    out = b''.join(parts)
    out = ident + struct.pack('<ii', len(lumps), pos) + out[12:] + b''.join(dirs)
    open(path, 'wb').write(out)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    vdir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'game', 'vehicles')
    ident, lumps = read_wad(src)
    new = []
    for f in sorted(os.listdir(os.path.join(vdir, 'data', 'voxels'))):
        new.append(('voxel', os.path.splitext(f)[0].upper(), open(os.path.join(vdir, 'data', 'voxels', f), 'rb').read()))
    for f in sorted(os.listdir(os.path.join(vdir, 'sprites', 'props'))):
        new.append(('icon', os.path.splitext(f)[0].upper(), open(os.path.join(vdir, 'sprites', 'props', f), 'rb').read()))
    new.append(('def', 'BGVOXELV', open(os.path.join(vdir, 'data', 'BGVOXELV.txt'), 'rb').read()))
    index = {nm: i for i, (nm, _) in enumerate(lumps)}
    # sprites in a wad are pictures, not PNG: the same converter pack_wad uses, with the wad's own palette
    play = next(d for n, d in lumps if n == 'PLAYPAL')
    extra = next((d for n, d in lumps if n == 'PLAYPALX'), None)
    pal = pack_wad.Palette(play, extra)
    icons_png = {n: pack_wad.encode_patch(d, pal, n) for k, n, d in new if k == 'icon'}
    for kind, nm, data in new:
        if kind == 'icon':
            continue
        if nm in index:
            lumps[index[nm]][1] = data
    after = {'voxel': 'VXCARD', 'def': 'BGVOXEL'}
    for kind, nm, data in new:
        if kind == 'icon' or nm in index:
            continue
        pos = [i for i, (n, _) in enumerate(lumps) if n == after[kind]][0] + 1
        lumps.insert(pos, [nm, data])
        index = {n: i for i, (n, _) in enumerate(lumps)}
    # icons: png lumps are accepted by the engine's sprite loader the same way the other props' are
    send = [i for i, (n, _) in enumerate(lumps) if n == 'S_END'][-1]
    for nm, data in icons_png.items():
        if nm in index:
            lumps[index[nm]][1] = data
        else:
            lumps.insert(send, [nm, data])
            send += 1
            index = {n: i for i, (n, _) in enumerate(lumps)}
    write_wad(dst, ident, lumps)
    print('wrote', dst, len(lumps), 'lumps')


if __name__ == '__main__':
    main()
