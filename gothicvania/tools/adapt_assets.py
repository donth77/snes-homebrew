#!/usr/bin/env python3
"""Adapt Gothicvania CC0 art -> SNES-ready indexed PNGs.

Output: gothicvania/res/{hero,bg,ground}.png
  * <=16 colours each, indexed (paletted) PNG as gfx4snes expects
  * sprites/ground: palette index 0 = transparency (magenta convention)
Re-runnable; reads the raw CC0 pack under gothicvania/assets/.
"""
import os, glob
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets")
RES  = os.path.join(ROOT, "res")
os.makedirs(RES, exist_ok=True)
MAGENTA = (255, 0, 255)

def to_indexed(rgba, transparent=True, budget=16, name=""):
    h, w = rgba.shape[:2]
    rgb, alpha = rgba[:, :, :3], rgba[:, :, 3]
    opaque = alpha > 0
    slots = budget - (1 if transparent else 0)
    uniq = sorted(set(map(tuple, rgb[opaque])))
    if len(uniq) > slots:
        tmp = Image.fromarray(rgb, "RGB").quantize(colors=slots, method=Image.MEDIANCUT, dither=Image.NONE)
        pal = tmp.getpalette()[:slots * 3]
        colors = [tuple(pal[i:i + 3]) for i in range(0, slots * 3, 3)]
        out = (np.array(tmp) + (1 if transparent else 0)).astype(np.uint8)
        palette = ([MAGENTA] if transparent else []) + colors
        print(f"  {name}: REDUCED {len(uniq)} -> {slots} colours")
    else:
        base = 1 if transparent else 0
        out = np.zeros((h, w), np.uint8)
        for i, c in enumerate(uniq):
            out[(rgb[:, :, 0] == c[0]) & (rgb[:, :, 1] == c[1]) & (rgb[:, :, 2] == c[2]) & opaque] = i + base
        palette = ([MAGENTA] if transparent else []) + list(uniq)
    if transparent:
        out[~opaque] = 0
    img = Image.fromarray(out, "P")
    flat = []
    for c in palette:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(palette))
    img.putpalette(flat)
    return img, len(palette)

def hero():
    frames = sorted(glob.glob(os.path.join(BASE, "Characters", "Hero", "Sprites", "hero-run", "hero-run-*.png")))
    L, T, R, B = 37, 49, 100, 90      # union content bbox measured from the 6 frames
    cw, ch = R - L, B - T             # 63 x 41
    CELL = 64
    sheet = Image.new("RGBA", (CELL, CELL * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        im = Image.open(f).convert("RGBA").crop((L, T, R, B))   # native art faces LEFT (hero runs left)
        sheet.paste(im, ((CELL - cw) // 2, i * CELL + (CELL - ch)))   # centre-x, bottom-align
    img, n = to_indexed(np.array(sheet), transparent=True, name="hero")
    img.save(os.path.join(RES, "hero.png"))
    print(f"hero.png   {CELL}x{CELL*len(frames)}  frames={len(frames)}  palette={n}")

def bg():
    im = Image.open(os.path.join(BASE, "Environment", "background.png")).convert("RGBA")
    a = np.array(im)
    seam = np.abs(a[:, 0, :3].astype(int) - a[:, -1, :3].astype(int)).mean()
    # Uniform downscale to 256 wide so the round moon stays round (was squished),
    # then pad to 256 tall with the darkest sky colour to fill the 32x32 tilemap.
    # NEAREST keeps the exact 13-colour palette. Horizontal seamlessness survives.
    sw, sh = im.size                                  # 384 x 224
    im = im.resize((256, round(sh * 256 / sw)), Image.NEAREST)   # -> 256 x 149
    a0 = np.array(im)
    op = a0[a0[:, :, 3] > 0][:, :3]
    dark = tuple(int(v) for v in op[np.argmin(op.sum(axis=1))])  # darkest existing colour
    canvas = Image.new("RGBA", (256, 256), dark + (255,))
    canvas.paste(im, (0, 0))                          # moon scene at top, dark fill below
    # Distant mountains composited into the gap. They ride the slow far layer (vs the
    # fast ground = parallax depth). bg-mountains has a solid body that runs down past
    # the ground line, so no empty band remains above the grass.
    mtn = Image.open(os.path.join(BASE, "Phaser Demo", "assets", "environment", "bg-mountains.png")).convert("RGBA")
    mw, mh = mtn.size
    mtn = mtn.resize((256, round(mh * 256 / mw)), Image.NEAREST)
    canvas.paste(mtn, (0, 88), mtn)                   # peaks rise into the gap; body under the ground
    img, n = to_indexed(np.array(canvas), transparent=False, name="bg")
    img.save(os.path.join(RES, "bg.png"))
    print(f"bg.png     256x256  palette={n}  seam(orig L vs R)={seam:.1f}")

def ground():
    ts = Image.open(os.path.join(BASE, "Environment", "tileset.png")).convert("RGBA")
    a = np.array(ts); H, W = a.shape[:2]; al = a[:, :, 3]
    colhas = (al > 0).any(axis=0)
    pieces, x = [], 0
    while x < W:
        if colhas[x]:
            x0 = x
            while x < W and colhas[x]:
                x += 1
            rows = np.where((al[:, x0:x] > 0).any(axis=1))[0]
            pieces.append((x0, int(rows.min()), x, int(rows.max()) + 1))
        else:
            x += 1
    print("  tileset pieces (x0,y0,x1,y1  WxH):")
    for p in pieces:
        print(f"    {p}  {p[2]-p[0]}x{p[3]-p[1]}")
    # The wide grass-dirt strip (x64..160) is the only continuous ground piece; its
    # right end frays into loose blades, so take its solid left 64px as a tiling unit.
    unit = ts.crop((64, 55, 128, 96))   # 64x41: grass top + solid dirt body
    nh = unit.height
    GTOP = 160
    canvas = np.zeros((256, 256, 4), np.uint8)
    ua = np.array(unit)
    for tx in range(0, 256, 64):
        canvas[GTOP:GTOP + nh, tx:tx + 64] = ua
        for yy in range(GTOP + nh, 256):              # extend dirt to the bottom (PAL safe)
            canvas[yy, tx:tx + 64] = ua[-1]
    img, n = to_indexed(canvas, transparent=True, name="ground")
    img.save(os.path.join(RES, "ground.png"))
    print(f"ground.png 256x256  unit=(64,55,128,96) 64x{nh}  grassTop=y{GTOP}  palette={n}")

if __name__ == "__main__":
    hero(); bg(); ground()
    print("done -> gothicvania/res/")
