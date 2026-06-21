#!/usr/bin/env python3
"""Moon + glow as a smooth 16-colour SPRITE (128x64 = two 64x64 OBJs), COMPOSITED over the gradient sky.

The earlier version cut the glow at a brightness threshold, keeping the source's warm pink right up to a
hard 1-bit-alpha edge -- against the cool purple gradient sky that read as "the moon cut out with
scissors". The fix: blend the moon's glow ONTO the exact per-scanline sky colour the BACKDROP shows
(gen_sky_gradient.py -> sky_rows.bin), with a smooth radial falloff. The glow fades into the real sky,
and where it vanishes the sprite pixel already equals the sky/backdrop -- so the sprite edge is
invisible (no cutout). The moon disk itself stays solid and bright so it still stands out."""
import os, sys
import numpy as np
from PIL import Image, ImageFilter
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV  = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets",
                    "Phaser Demo", "assets", "environment")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from adapt_assets import to_indexed
a = np.array(Image.open(os.path.join(ENV, "bg-moon.png")).convert("RGBA"))
CX, CY = 194, 65
WD, HT = 128, 64
box = a[CY - HT // 2:CY - HT // 2 + HT, CX - WD // 2:CX - WD // 2 + WD, :3].astype(int)
yy, xx = np.mgrid[0:HT, 0:WD]
cx, cy = WD / 2 - 0.5, HT / 2 - 0.5
r = np.hypot(xx - cx, yy - cy)
disk = r <= 28                                                         # the moon disk
# fill very-dark disk pixels (source mountain overlap) so the disk has no holes
lum = 0.3 * box[:, :, 0] + 0.6 * box[:, :, 1] + 0.1 * box[:, :, 2]
dk = disk & (lum < 18); br = disk & (lum >= 18)
if dk.any() and br.any():
    box[dk] = np.median(box[br], axis=0)
lum = 0.3 * box[:, :, 0] + 0.6 * box[:, :, 1] + 0.1 * box[:, :, 2]

# NORMALISE THE DISK: the source moon is lit from the upper-right, so its left/bottom edge is shaded
# darker -- which read as "a shadow on the moon". Flatten that low-frequency directional shading while
# KEEPING the high-frequency crater detail: new luminance = disk-mean + (lum - heavily-blurred lum).
box = box.astype(float)
_g = lum.copy(); _g[~disk] = lum[disk].mean()                        # neutralise outside so the blur holds at the rim
_low = np.asarray(Image.fromarray(np.clip(_g, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(6)), float)
_dm = lum[disk].mean()
_flat = np.clip(_dm + (lum - _low), _dm * 0.66, _dm * 1.20)          # uniform base + craters
_scd = _flat[disk] / np.maximum(lum[disk], 1.0)                      # per-pixel scale to flatten the shading
box[disk] = np.clip(box[disk] * _scd[:, None], 0, 255)              # even, glowing disk -- detail kept
lum = 0.3 * box[:, :, 0] + 0.6 * box[:, :, 1] + 0.1 * box[:, :, 2]

# COMPOSITE the glow OVER the exact per-scanline gradient sky (sky_rows.bin) so it FADES INTO the real
# sky (no hard cutout). The glow brightness is BLURRED first so the halo is a smooth haze, not the
# source's blotchy cloud texture; a smooth elliptical falloff dissolves it to pure sky by the sprite edge.
SPR_Y = 28
sky_rows = np.frombuffer(open(os.path.join(ROOT, "res", "level", "sky_rows.bin"), "rb").read(),
                         np.uint8).reshape(-1, 3).astype(float)
sky = sky_rows[SPR_Y:SPR_Y + HT][:, None, :]                          # (HT,1,3) -> broadcasts over width
lum_sky = (sky * [0.3, 0.6, 0.1]).sum(2)                              # (HT,1)
lum_glow = np.asarray(Image.fromarray(np.clip(lum, 0, 255).astype(np.uint8))
                      .filter(ImageFilter.GaussianBlur(3)), float)    # smoothed glow -> no blotches

SCALE = float(os.environ.get("MOON_GLOW_SCALE", "130"))              # higher -> tighter, less hazy halo
ed = ((xx - cx) / (WD / 2)) ** 2 + ((yy - cy) / (HT / 2)) ** 2        # 0 at centre, 1 at the inscribed ellipse
falloff = np.clip((1.0 - ed) / 0.45, 0, 1)
falloff = falloff * falloff * (3 - 2 * falloff)                      # smoothstep
alpha = np.clip((lum_glow - lum_sky) / SCALE, 0, 1) * falloff
alpha[disk] = 1.0                                                     # the moon disk is solid

comp = alpha[:, :, None] * box + (1 - alpha[:, :, None]) * sky        # glow blended onto the gradient sky
opaque = alpha > 0.05                                                 # transparent ONLY where it's pure sky
moon = np.zeros((HT, WD, 4), np.uint8)
moon[:, :, :3] = np.clip(comp, 0, 255).astype(np.uint8)
moon[:, :, 3] = np.where(opaque, 255, 0)
idx, nc = to_indexed(moon, transparent=True, budget=16, name="moon")
idx.save(os.path.join(ROOT, "res", "moon.png"))
print(f"moon: {WD}x{HT}, {nc} colours, {int(opaque.sum())} opaque px, normalised disk + smoothed glow")
