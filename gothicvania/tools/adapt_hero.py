#!/usr/bin/env python3
"""Convert the hero's animation spritesheets into one 2-sprite (128px-wide) SNES strip
(single 16-colour palette), anchored so the FEET stay planted -- no slide, no jitter, no
clipped sword -- whichever way the limbs swing.

The catch: which reference is steady depends on the animation. When the hero stands
(idle/attack/crouch) the feet are planted and only the sword/cape/arms move, so the boots
are rock-steady (span <=1px) while the centre of mass lurches (the attack thrust drags COM
14px right). When the legs cycle (run/jump) it is the opposite -- the boots swing 12-36px
while COM holds to ~1px. Anchoring on the wrong one is exactly what made the idle feet
"slide" and the attack body lurch back and forth. So we choose PER ANIMATION:
  * boots span <=4px  -> anchor each frame on its OWN boots (feet pinned, limbs swing free).
  * boots span  >4px  -> anchor on the per-anim mean COM (one value, so cycling legs don't
    drag the body); each anim's anchor still lands its feet at the same x, so no pop on switch.
Vertical anchor is always the lowest wide leg-row (ground contact). We size the 128px box to
the widest pose (the attack thrust) so nothing clips.

Outputs: res/hero.png (128 x 64*N strip, each row [L|R]) and src/hero_anim.h. The Makefile
runs gfx4snes -R then splits hero.pic into 4KB L|R bands; main.c DMAs the current band.
"""
import os, sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SS   = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets",
                    "Characters", "Hero", "Spritesheets")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from adapt_assets import to_indexed

BOX = 64
FW  = 160
SHEETS = [("IDLE", "hero-idle.png"), ("RUN", "hero-run.png"), ("JUMP", "hero-jump.png"),
          ("CROUCH", "hero-crouch.png"), ("ATTACK", "hero-attack.png"),
          ("HURT", "hero-hurt.png")]   # appended LAST so existing frame numbers don't shift (HURT = frame 20)

def features(a):
    # Per-frame anchors. boots-x = centre of the lowest WIDE row (>=5 opaque px = the legs,
    # not the thin sword) -> the planted feet. com-x = centroid of all opaque px. feet-y =
    # that lowest wide row -> the ground-contact line. The sword/cape are thin, so they barely
    # move boots-x but they DO drag com-x around (the attack thrust shoves com 14px right, the
    # idle cape sways it ~2px), which is why com is only safe to anchor on when the legs cycle.
    ys, xs = np.where(a[:, :, 3] > 0)
    fy = int(ys.max())
    for yy in range(a.shape[0] - 1, -1, -1):
        if int((a[yy, :, 3] > 0).sum()) >= 5:
            fy = yy; break
    legxs = xs[ys >= fy - 2]
    return int((int(legxs.min()) + int(legxs.max())) // 2), int(round(xs.mean())), fy

# pass 1: load every frame, grouped by animation, with its boots/com/feet anchors + bbox.
anims = []                                               # [(name, [frame dict, ...])]
for name, fn in SHEETS:
    sheet = np.array(Image.open(os.path.join(SS, fn)).convert("RGBA"))
    fr = []
    for i in range(sheet.shape[1] // FW):
        a = sheet[:, i * FW:i * FW + FW]
        ys, xs = np.where(a[:, :, 3] > 0)
        if len(xs) == 0:
            continue
        bx, cx, fy = features(a)
        fr.append({"a": a, "bx": bx, "cx": cx, "fy": fy,
                   "bb": (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)})
    anims.append((name, fr))

# Choose the horizontal anchor PER ANIMATION. If the boots barely move across the anim's frames
# (idle/attack/crouch: span <=4px = the feet are planted, only the sword/cape/arms move) anchor
# each frame on its OWN boots, pinning the feet exactly -> no slide, and the limbs swing freely.
# If the legs cycle (run/jump: big boots span) the boots are a moving target, so fall back to the
# steady centre of mass, ONE value for the whole anim (so the cycling legs don't drag the body).
for name, fr in anims:
    bspan = max(f["bx"] for f in fr) - min(f["bx"] for f in fr)
    if bspan <= 4:
        for f in fr: f["ax"] = f["bx"]                  # per-frame boots  -> pinned feet
    else:
        cx = int(round(sum(f["cx"] for f in fr) / len(fr)))
        for f in fr: f["ax"] = cx                       # per-anim COM     -> steady body
    # each anim's anchor lands its feet at the same composite x, so anims don't pop on switch.

seq = [(name, f["a"], f["ax"], f["fy"], f["bb"]) for name, fr in anims for f in fr]
maxD = max(b[3] - fy for _, _, _, fy, b in seq)
# Keep the art at FULL SIZE (no scaling -> crisp, and the hero stays taller than the
# gravestones). The head sits at a fixed box-x (slight left bias so the long attack thrust
# reaches as far right as possible); the body fits 64px, only the very sword tip may touch
# the edge. Feet sit near the bottom with room below for the down-pointing sword.
# FULL SIZE (crisp). The hero is a 2-sprite metasprite: a 128px-wide area split into a
# LEFT and RIGHT 64x64 sprite, so the sword fits whether it points down-left (idle/run) or
# thrusts right (attack) -- no clipping. Head sits at the centre (the mirror axis for hflip).
COMP_W = 2 * BOX
HEAD_BX = BOX                                  # head at composite centre (box 64)
FEET_BY = BOX - maxD - 2

frames, counts = [], {}
for name, a, fx, fy, b in seq:
    s = a.copy(); sh, sw = s.shape[0], s.shape[1]
    s[s[:, :, 3] < 128] = 0
    ox, oy = HEAD_BX - fx, FEET_BY - fy
    comp = np.zeros((BOX, COMP_W, 4), np.uint8)
    x0, y0 = max(0, ox), max(0, oy); sx0, sy0 = x0 - ox, y0 - oy
    w = min(COMP_W - x0, sw - sx0); h = min(BOX - y0, sh - sy0)
    if w > 0 and h > 0:
        comp[y0:y0 + h, x0:x0 + w] = s[sy0:sy0 + h, sx0:sx0 + w]
    frames.append((comp[:, 0:BOX].copy(), comp[:, BOX:COMP_W].copy()))   # (left, right)
    counts[name] = counts.get(name, 0) + 1

# Strip is 128 wide: each row is [L | R] for one frame, so gfx4snes packs both 64x64 halves
# into a single 4KB band (L at gfxoffset 0, R at gfxoffset 8) -> one DMA per frame.
N = len(frames)
strip = np.zeros((BOX * N, COMP_W, 4), np.uint8)
for f, (L, R) in enumerate(frames):
    strip[f * BOX:f * BOX + BOX, 0:BOX] = L
    strip[f * BOX:f * BOX + BOX, BOX:COMP_W] = R
idx, nc = to_indexed(strip, transparent=True, budget=16, name="hero")
idx.save(os.path.join(ROOT, "res", "hero.png"))

with open(os.path.join(ROOT, "src", "hero_anim.h"), "w") as h:
    h.write("// generated by tools/adapt_hero.py -- 2-sprite hero, head@x%d feet@y%d\n" % (HEAD_BX, FEET_BY))
    h.write(f"#define HERO_FRAMES {N}\n")
    h.write(f"#define HERO_FEET_X {HEAD_BX}\n#define HERO_FEET_Y {FEET_BY}\n")
    pos = 0
    for name, _ in SHEETS:
        c = counts.get(name, 0)
        h.write(f"#define A_{name}_F {pos}\n#define A_{name}_N {c}\n")
        pos += c

print(f"hero: {N} frames (2-sprite, full size), head@x{HEAD_BX} feet@y{FEET_BY}, palette {nc} colours")
