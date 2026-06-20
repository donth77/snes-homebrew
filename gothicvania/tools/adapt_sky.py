#!/usr/bin/env python3
"""Simple backdrop gradient HDMA table for setModeHdmaColor -- the PROVEN format: every entry is
[linecount, 0x00, 0x00, colour_lo, colour_hi], i.e. it only ever writes CGRAM colour 0 (the
backdrop). (An earlier version varied CGADD to recolour the cloud-highlight slot per scanline for a
pink moon-glow; it rendered in the test runner but CORRUPTED the palette on real Mesen, so it's
gone.) The clouds (BG2) cover the upper sky, so this backdrop is mostly the dark sky seen through
the graveyard's gaps -- a dark purple, darkening toward the bottom."""
import os
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "res", "level")
ctrl = np.array([[0, 22, 5, 44], [104, 14, 2, 34], [223, 8, 0, 22]], float)   # dark purple gradient
def bgr555(r, g, b): return (int(b) >> 3 << 10) | (int(g) >> 3 << 5) | (int(r) >> 3)
tbl = bytearray()
for y0 in range(0, 224, 8):
    r = np.interp(y0, ctrl[:, 0], ctrl[:, 1]); g = np.interp(y0, ctrl[:, 0], ctrl[:, 2]); b = np.interp(y0, ctrl[:, 0], ctrl[:, 3])
    c = bgr555(r, g, b)
    tbl += bytes([8, 0x00, 0x00, c & 0xFF, c >> 8])
tbl += bytes([0x00])
open(os.path.join(OUT, "sky_gradient.bin"), "wb").write(tbl)
print(f"sky backdrop (CGRAM 0 only, safe): {len(tbl)}B, {(len(tbl)-1)//5} bands")
