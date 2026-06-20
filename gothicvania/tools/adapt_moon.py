#!/usr/bin/env python3
"""Option A: moon + glow as a smooth 16-colour SPRITE (128x64 = two 64x64 OBJs). The glow fades to the
DARK SKY colour (then transparent) BEFORE reaching the sprite edges, so (1) it can never be cut off and
(2) it melts seamlessly into a plain dark background sky. The background sky is then just that one flat
dark colour -- no gradient, so nothing can band/block. Writes the dark sky colour to res/sky_dark.txt
so adapt_parallax.py paints the BG sky the exact matching colour."""
import os, sys
import numpy as np
from PIL import Image
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV  = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets",
                    "Phaser Demo", "assets", "environment")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from adapt_assets import to_indexed
a = np.array(Image.open(os.path.join(ENV, "bg-moon.png")).convert("RGBA"))
CX, CY = 194, 65
WD, HT = 128, 64
box = a[CY - HT // 2:CY - HT // 2 + HT, CX - WD // 2:CX - WD // 2 + WD, :3].astype(int)
lum = 0.3 * box[:, :, 0] + 0.6 * box[:, :, 1] + 0.1 * box[:, :, 2]
yy, xx = np.mgrid[0:HT, 0:WD]
cx, cy = WD / 2 - 0.5, HT / 2 - 0.5
r = np.hypot(xx - cx, yy - cy)
disk = r <= 28                                                         # the moon disk
# glow: bright pixels inside the inscribed ellipse (keeps it off the corners; fades to dark by the edge)
ell  = ((xx - cx) / (WD / 2 - 2)) ** 2 + ((yy - cy) / (HT / 2 - 2)) ** 2 <= 1.0
glow = ell & (lum > 22)
keep = disk | glow
# fill very-dark disk pixels (source mountain overlap) so the disk has no holes
dk = disk & (lum < 18); br = disk & (lum >= 18)
if dk.any() and br.any():
    box[dk] = np.median(box[br], axis=0).astype(box.dtype)
moon = np.zeros((HT, WD, 4), np.uint8)
moon[:, :, :3] = box; moon[:, :, 3] = np.where(keep, 255, 0)
idx, nc = to_indexed(moon, transparent=True, budget=16, name="moon")
idx.save(os.path.join(ROOT, "res", "moon.png"))
# DARK SKY colour = average of the dimmest ~3% of kept glow pixels (the fade edge) -> BG matches it
kept = box[keep]; klum = (kept * [0.3, 0.6, 0.1]).sum(1)
n = max(1, len(kept) // 33)
D = kept[np.argsort(klum)[:n]].mean(0).astype(int)
open(os.path.join(ROOT, "res", "sky_dark.txt"), "w").write(f"{int(D[0])},{int(D[1])},{int(D[2])}")
print(f"moon+glow: {WD}x{HT}, {nc} colours, {int(keep.sum())} opaque px, dark-sky D={D.tolist()}")
