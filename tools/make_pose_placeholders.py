#!/usr/bin/env python3
"""Begotten: build a PWAD of PLACEHOLDER player pose sprites plus the BGCLASS lines that use them.

usage: make_pose_placeholders.py WAD [-o begotten_poses.wad]

WAD only supplies the PLAYPAL. Load the result with `-file begotten_poses.wad` (or merge its lumps into
begotten.wad) to see the pose system working with stand-in art; replace the sprites with your renders
and keep the BGCLASS lines (edit them to taste).

Every frame has 8 separate rotation lumps (A1 A2 A3 ... A8, none mirrored) with the rotation number
drawn on it, so you can check from the outside that the right direction is shown.
"""
import argparse, struct, sys
from PIL import Image, ImageDraw, ImageFont

# sprite name, frames, colour, label        (one block per class / pose)
SETS = [
    # soldier: green
    ("SRUN", 6, (60, 170, 60), "RUN"),
    ("SSHT", 3, (210, 210, 60), "SHOOT"),
    ("SJMP", 3, (60, 200, 200), "JUMP"),
    ("SRFL", 1, (140, 100, 50), "RIFLE"),     # soldier holding the rifle (weapon specific)
    ("SRFS", 3, (230, 150, 40), "RFSHOT"),
    # witch: purple
    ("WRUN", 6, (150, 70, 200), "RUN"),
    ("WSHT", 3, (210, 90, 190), "SHOOT"),
    ("WJMP", 3, (110, 110, 230), "JUMP"),
    # swordsman: red
    ("XRUN", 6, (200, 60, 60), "RUN"),
    ("XSHT", 3, (230, 120, 80), "SLASH"),
    ("XJMP", 3, (230, 80, 140), "JUMP"),
    ("XPAR", 3, (240, 240, 240), "PARRY"),
]

BGCLASS = """# placeholder poses (make_pose_placeholders.py): replace the sprites, keep or edit these lines
edit soldier
  pose run   SRUN 6 3
  pose shoot SSHT 3 3
  pose jump  SJMP
  pose stand SRFL 1 4 weapon shotgun
  pose shoot SRFS 3 3 weapon shotgun
edit witch
  pose run   WRUN 6 3
  pose shoot WSHT 3 3
  pose jump  WJMP
edit swordsman
  pose run   XRUN 6 3
  pose shoot XSHT 3 3
  pose jump  XJMP
  pose parry XPAR 3 3
"""


def read_playpal(path):
    d = open(path, 'rb').read()
    n, o = struct.unpack('<ii', d[4:12])
    for i in range(n):
        p, s, nm = struct.unpack('<ii8s', d[o + 16 * i:o + 16 * i + 16])
        if nm.rstrip(b'\0') == b'PLAYPAL':
            pal = d[p:p + 768]
            return [tuple(pal[i * 3:i * 3 + 3]) for i in range(256)]
    sys.exit('no PLAYPAL in ' + path)


def nearest_factory(pal):
    cache = {}

    def nearest(rgb):
        if rgb in cache:
            return cache[rgb]
        best, bd = 0, 1 << 30
        for i, (r, g, b) in enumerate(pal):
            if i == 255 and False:
                continue
            d = (r - rgb[0]) ** 2 + (g - rgb[1]) ** 2 + (b - rgb[2]) ** 2
            if d < bd:
                best, bd = i, d
        cache[rgb] = best
        return best
    return nearest


def make_image(colour, label, frame, rot):
    W, H = 48, 56
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    dark = tuple(int(c * 0.55) for c in colour)
    d.rectangle([12, 18, 35, 55], fill=colour, outline=(0, 0, 0, 255))      # body
    d.ellipse([16, 2, 31, 17], fill=colour, outline=(0, 0, 0, 255))         # head
    # facing marker: rotation 1 = facing the viewer; the dot moves around the head
    import math
    ang = math.pi / 2 + (rot - 1) * math.pi / 4          # 1: down (towards viewer), 3: left ...
    cx, cy = 23.5 + 12 * math.cos(ang) * 0.0, 10
    nx, ny = cx + 5 * math.cos(ang), cy + 5 * math.sin(ang) * 0.6
    d.ellipse([nx - 2, ny - 2, nx + 2, ny + 2], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))
    d.rectangle([12, 18, 35, 30], fill=dark)
    f = ImageFont.load_default(size=10)
    d.text((14, 19), label[:5], fill=(255, 255, 255, 255), font=f)
    d.text((14, 32), "%s%d" % (chr(65 + frame), rot), fill=(255, 255, 0, 255),
           font=ImageFont.load_default(size=14))
    return im


def to_patch(im, nearest):
    W, H = im.size
    px = im.load()
    cols = []
    for x in range(W):
        posts = b''
        y = 0
        while y < H:
            while y < H and px[x, y][3] < 128:
                y += 1
            if y >= H:
                break
            start = y
            data = bytearray()
            while y < H and px[x, y][3] >= 128:
                data.append(nearest(px[x, y][:3]))
                y += 1
            posts += bytes([start, len(data), 0]) + bytes(data) + b'\0'
        cols.append(posts + b'\xff')
    header = struct.pack('<hhhh', W, H, W // 2, H - 2)
    offs = []
    pos = 8 + 4 * W
    for c in cols:
        offs.append(pos)
        pos += len(c)
    return header + b''.join(struct.pack('<i', o) for o in offs) + b''.join(cols)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('-o', '--output', default='begotten_poses.wad')
    a = ap.parse_args()
    nearest = nearest_factory(read_playpal(a.wad))

    lumps = [('S_START', b'')]
    for name, frames, colour, label in SETS:
        for f in range(frames):
            for rot in range(1, 9):
                lumps.append(('%s%s%d' % (name, chr(65 + f), rot), to_patch(make_image(colour, label, f, rot), nearest)))
    lumps.append(('S_END', b''))
    lumps.append(('BGCLASS', BGCLASS.encode()))

    body = b''
    entries = []
    for n, b in lumps:
        entries.append((12 + len(body), len(b), n))
        body += b
    dirs = b''.join(struct.pack('<ii8s', p, s, n.encode()) for p, s, n in entries)
    open(a.output, 'wb').write(b'PWAD' + struct.pack('<ii', len(lumps), 12 + len(body)) + body + dirs)
    print('wrote %s: %d lumps' % (a.output, len(lumps)))


main()
