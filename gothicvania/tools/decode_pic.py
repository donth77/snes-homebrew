#!/usr/bin/env python3
"""Decode a SNES 4bpp .pic (+ .pal) to a PNG preview, tiles laid out 16-wide
(the OBJ name-table width), so we can read off animation-frame tile offsets.

Usage: decode_pic.py <file.pic> <file.pal> <out.png> [scale]
"""
import sys
import numpy as np
from PIL import Image

pic, pal, out = sys.argv[1], sys.argv[2], sys.argv[3]
scale = int(sys.argv[4]) if len(sys.argv) > 4 else 3

data = np.fromfile(pic, dtype=np.uint8)
pdata = np.fromfile(pal, dtype=np.uint8)
palette = []
for i in range(len(pdata) // 2):
    c = pdata[i * 2] | (pdata[i * 2 + 1] << 8)          # BGR555 little-endian
    palette.append((((c) & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3))

ntiles = len(data) // 32
WIDE = 16
rows = (ntiles + WIDE - 1) // WIDE
img = np.full((rows * 8, WIDE * 8, 3), (255, 0, 255), np.uint8)   # magenta = idx0/transparent
for t in range(ntiles):
    off = t * 32
    tx, ty = (t % WIDE) * 8, (t // WIDE) * 8
    for y in range(8):
        p0, p1 = data[off + y * 2], data[off + y * 2 + 1]
        p2, p3 = data[off + 16 + y * 2], data[off + 16 + y * 2 + 1]
        for x in range(8):
            b = 7 - x
            v = ((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3)
            if v:
                img[ty + y, tx + x] = palette[v] if v < len(palette) else (0, 0, 0)
im = Image.fromarray(img, "RGB").resize((WIDE * 8 * scale, rows * 8 * scale), Image.NEAREST)
im.save(out)
print(f"{pic}: {ntiles} tiles -> {rows} rows x {WIDE}-wide sheet; tile offsets are (row*16 + col)")
