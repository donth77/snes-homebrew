#!/usr/bin/env python3
"""Smooth night-sky gradient via per-scanline COLOR MATH -- the SAFE way to gradient the sky.

The SNES background can only show 4 colours per 8x8 tile, so a smooth gradient sky cannot be baked into
BG2 without banding/dithering (both rejected). And the old per-scanline *CGRAM* HDMA (setModeHdmaColor)
corrupts the palette on real hardware. So instead we paint the sky on the BACKDROP (CGRAM 0) and use
HDMA to ADD a per-scanline fixed colour to it via register $2132 (COLDATA = the colour-math fixed
colour). This NEVER writes CGRAM, so it cannot corrupt the palette -- a normal per-scanline colour-math
effect. Transfer mode 2 writes $2132 twice per line: $20|red5 then $80|blue5 (green stays = backdrop).

Colours are sampled to match the DEMO: a DARK, muted night sky -- deep indigo at the top easing to a
muted purple around the moon -- so the bright pink moon sprite STANDS OUT against it (not a hot-pink sky
the moon drowns in). The moon's glow is cut at the moon-band luminance so its warm halo meets the cool
sky at equal brightness (a natural halo, the moon still pops). Outputs:
  res/level/sky_coldata.bin  - the HDMA table (mode-2 continuous: 2 bytes/scanline + $00 terminator)
  src/sky_params.h           - #define SKY_BACKDROP (bgr555 of the dark backdrop floor = CGRAM 0)
  res/sky_dark.txt           - MOONLIT (the moon-band colour; adapt_moon cuts the glow at its luminance)
  /tmp/sky_grad.png          - a preview of the resulting sky colours
"""
import os
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "res", "level")

NLINES = 130                   # scanlines the gradient covers (the sky band; below it the level is opaque)

# Per-scanline sky colour, ABSOLUTE (r, b) -- green is held at the backdrop's (the demo sky is ~0 green).
# Sampled/scaled from the demo: deep indigo top -> muted purple moon band (a touch brighter than the demo
# so the 5-bit ramp has enough steps to stay smooth) -> easing dark toward the horizon. Bluer up high,
# more balanced in the moon band, exactly like the demo. Moon sprite sits at y=28..92 (centre 60).
# Gentle darkening from the moon band down to the horizon (a muted purple, NOT black -- black merged
# with the dark mountains into a "rectangle"). The mountains are cut to jagged peaks in adapt_parallax,
# so the sky shows below them and there's nothing to merge into.
CP = [(0, 12, 44), (24, 46, 74), (60, 60, 90), (92, 48, 74), (108, 30, 54), (NLINES - 1, 18, 42)]
GREEN = 2                      # near-zero green, like the demo (-> 0 in 5-bit, a pure indigo/purple)
# MOONLIT = the moon-band colour; the moon glow is cut at its luminance so the warm halo meets the cool
# sky at equal brightness. Bright pink DISK (from the sprite) then pops against this dark sky.
MOONLIT = (56, GREEN, 80)


def lerp_rb(y):
    for i in range(len(CP) - 1):
        y0, r0, b0 = CP[i]; y1, r1, b1 = CP[i + 1]
        if y0 <= y <= y1:
            t = (y - y0) / (y1 - y0) if y1 > y0 else 0.0
            t = t * t * (3 - 2 * t)                            # smoothstep ease
            return r0 + (r1 - r0) * t, b0 + (b1 - b0) * t
    return CP[-1][1], CP[-1][2]


def to5(v):  return max(0, min(31, int(round(v)) >> 3))        # 8-bit -> 5-bit SNES channel
def bgr555(c): r, g, b = c; return (to5(b) << 10) | (to5(g) << 5) | to5(r)

# Backdrop = the per-channel MINIMUM over the whole profile, so the colour-math ADD is >= 0 everywhere.
minR = min(min(r for _, r, _ in CP), MOONLIT[0])
minB = min(min(b for _, _, b in CP), MOONLIT[2])
BACKDROP = (minR, GREEN, minB)
b5 = (to5(BACKDROP[0]), to5(BACKDROP[1]), to5(BACKDROP[2]))

# Build the HDMA table. CRITICAL: an HDMA continuous entry's line-count is only 7 bits, so it can cover
# at most 127 scanlines -- a single entry for >127 lines overflows into the flag bit and corrupts the
# table on REAL HARDWARE (Mesen renders it leniently, which hid this). So split into <=127-line chunks.
data = bytearray(); rows = []
y = 0
while y < NLINES:
    chunk = min(127, NLINES - y)
    data += bytes([0x80 | chunk])                            # continuous: `chunk` lines, each its own unit
    for _ in range(chunk):
        r, b = lerp_rb(y)
        rAdd = max(0, to5(r) - b5[0])                         # red  add above the backdrop (5-bit)
        bAdd = max(0, to5(b) - b5[2])                         # blue add above the backdrop (5-bit)
        data += bytes([0x20 | rAdd, 0x80 | bAdd])            # $2132 <- red, then $2132 <- blue
        rows.append((b5[0] + rAdd, b5[1], b5[2] + bAdd))
        y += 1
data += bytes([0x00])
os.makedirs(OUT, exist_ok=True)
open(os.path.join(OUT, "sky_coldata.bin"), "wb").write(bytes(data))

# Also emit the per-scanline sky colour (8-bit RGB, the exact gradient the BACKDROP shows) so
# adapt_moon.py can composite the moon glow OVER it -> the glow fades into the real sky and the
# sprite's edge lands on the background colour, so there's NO hard cutout edge around the moon.
def _exp8(v5): return (v5 << 3) | (v5 >> 2)                    # 5-bit -> 8-bit the way the SNES expands it
sky8 = bytearray()
for (r, g, b) in rows:
    sky8 += bytes([_exp8(r), _exp8(g), _exp8(b)])
open(os.path.join(OUT, "sky_rows.bin"), "wb").write(bytes(sky8))

open(os.path.join(ROOT, "src", "sky_params.h"), "w").write(
    "// generated by tools/gen_sky_gradient.py -- dark backdrop floor (CGRAM 0); HDMA adds a per-scanline\n"
    "// indigo->purple ramp on top of it via $2132 (colour math), never CGRAM.\n"
    f"#define SKY_BACKDROP 0x{bgr555(BACKDROP):04X}\n")

open(os.path.join(ROOT, "res", "sky_dark.txt"), "w").write(f"{MOONLIT[0]},{MOONLIT[1]},{MOONLIT[2]}")

prev = np.zeros((NLINES + 24, 256, 3), np.uint8)
for y, (r, g, b) in enumerate(rows):
    prev[y, :] = (r * 8, g * 8, b * 8)
prev[NLINES:] = (b5[0] * 8, b5[1] * 8, b5[2] * 8)
Image.fromarray(prev).resize((256 * 3, (NLINES + 24) * 3), Image.NEAREST).save("/tmp/sky_grad.png")
nlevels = len({(r, b) for r, _, b in rows})
print(f"sky gradient: {NLINES} lines, {nlevels} distinct colours, backdrop={BACKDROP} "
      f"(0x{bgr555(BACKDROP):04X}), moonlit={MOONLIT}, table={len(data)}B")
