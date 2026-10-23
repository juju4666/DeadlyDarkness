"""contact sheets for looking at the drawn art"""
import sys
from PIL import Image, ImageDraw


def sheet(rows, path, bg=(64, 64, 88), scale=1.0, label=None):
    """rows: list of lists of (img, ox, oy); aligned on a common floor line per row"""
    cells = []
    H = 0
    for row in rows:
        up = max(oy for _, _, oy in row)
        down = max(im.height - oy for im, _, oy in row)
        cells.append((row, up, down))
        H += up + down + 14
    W = max(sum(im.width + 8 for im, _, _ in row) for row in rows) + 8
    out = Image.new('RGBA', (W, H + 4), bg + (255,))
    y = 4
    for row, up, down in cells:
        x = 4
        for im, ox, oy in row:
            out.paste(im, (x, y + up - oy), im)
            x += im.width + 8
        d = ImageDraw.Draw(out)
        d.line([0, y + up, W, y + up], fill=(90, 90, 120, 255))
        y += up + down + 14
    if scale != 1:
        out = out.resize((int(out.width * scale), int(out.height * scale)), Image.NEAREST if scale > 1 else Image.LANCZOS)
    out.save(path)
