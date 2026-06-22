#!/usr/bin/env python3
"""Title-screen overlays -> two 4bpp BGs (BG0 = logo, BG1 = "PRESS START"), composited in main.c OVER
the live moon + scrolling mountains + sky-gradient scene.

Two separate BGs (not one) so "PRESS START" can BLINK by toggling its layer on/off while the logo stays.

Outputs (4bpp .pic + 16-col .pal + 32x32 .map, like adapt_parallax emits its own):
  res/title_logo.pic/.pal/.map    BG0, palette block 1 (CGRAM 16..31)
  res/title_press.pic/.pal/.map   BG1, palette block 2 (CGRAM 32..47)
"""
import os, struct
import numpy as np
from PIL import Image, ImageFont, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPR  = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets", "Phaser Demo",
                    "assets", "sprites")
OUT  = os.path.join(ROOT, "res")
FONT = "/Users/tomdonohue/Documents/gameassets/Kenney Game Assets All-in-1 3.5.0/Other/Fonts/Kenney Pixel.ttf"

SCR_W, SCR_H = 256, 224
LOGO_W   = 248                 # scale the 290px logo down to this width (fits 256 with a small margin)
LOGO_CY  = 130                 # logo vertical centre: BELOW the moon (sprite y28..92) so the moon shows
                               # cleanly ABOVE the title instead of poking out as a nub behind it
PRESS_CY = 190                 # "PRESS START" vertical centre (low, under the logo)

RED, PURPLE = (214, 28, 28), (60, 2, 133)     # logo ink: bright red + its dark-purple drop shadow
GOLD = (255, 206, 64)                          # "PRESS START" colour (warm gold)


def snap_logo():
    """Load the 290x91 2-colour logo, scale to LOGO_W, snap every pixel back to {transparent, RED, PURPLE}."""
    im = Image.open(os.path.join(SPR, "title-screen.png")).convert("RGBA")
    h = round(im.height * LOGO_W / im.width)
    im = im.resize((LOGO_W, h), Image.LANCZOS)
    a = np.array(im)
    op = a[:, :, 3] > 110                                  # keep firmly-opaque pixels (drop the soft AA halo)
    rgb = a[:, :, :3].astype(int)
    # nearest of the two ink colours by luminance-weighted distance
    dr = ((rgb - np.array(RED)) ** 2).sum(2)
    dp = ((rgb - np.array(PURPLE)) ** 2).sum(2)
    idx = np.zeros(a.shape[:2], np.uint8)
    idx[op & (dr <= dp)] = 1
    idx[op & (dr >  dp)] = 2
    return idx


def make_text(txt, size=28, maxw=244):
    """Render text in the Kenney pixel font as a 1-colour index image, shrinking to fit maxw."""
    while size >= 8:
        font = ImageFont.truetype(FONT, size)
        tmp = Image.new("L", (512, 96), 0)
        d = ImageDraw.Draw(tmp)
        bb = d.textbbox((0, 0), txt, font=font)
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        if w <= maxw:
            d.text((-bb[0], -bb[1]), txt, fill=255, font=font)
            return (np.array(tmp)[:h, :w] > 110).astype(np.uint8)   # 0 transparent / 1 ink
        size -= 2
    raise RuntimeError("text won't fit")


def place(idx_small, cy):
    """Centre a small index image on the 256x224 screen at vertical centre cy."""
    canvas = np.zeros((SCR_H, SCR_W), np.uint8)
    h, w = idx_small.shape
    x0 = (SCR_W - w) // 2
    y0 = cy - h // 2
    canvas[y0:y0 + h, x0:x0 + w] = idx_small
    return canvas


# --- 4bpp tile dedup (with H/V flips) + encode, identical entry/map scheme to adapt_parallax -----------
def build_bg(canvas, subpal):
    cmap, tiles = {}, []
    blank = np.zeros((8, 8), np.uint8); cmap[blank.tobytes()] = 0; tiles.append(blank)

    def entry(a):
        orbit = [a, a[:, ::-1], a[::-1, :], a[::-1, ::-1]]
        bs = [o.tobytes() for o in orbit]; k = min(bs)
        if k not in cmap:
            cmap[k] = len(tiles); tiles.append(orbit[bs.index(k)])
        ti = cmap[k]; canon = tiles[ti]
        for hf in (0, 1):
            for vf in (0, 1):
                t = canon[:, ::-1] if hf else canon
                t = t[::-1, :] if vf else t
                if np.array_equal(t, a):
                    return ti | (subpal << 10) | (hf << 14) | (vf << 15)
        raise RuntimeError("flip mismatch")

    tm = [0] * (32 * 32)
    for ty in range(SCR_H // 8):
        for tx in range(32):
            a = canvas[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            if a.any():
                tm[ty * 32 + tx] = entry(a.copy())
    return tiles, tm


def enc4(t):                                              # SNES 4bpp tile = 32 bytes (planes 0,1 then 2,3)
    out = bytearray(32)
    for r in range(8):
        p0 = p1 = p2 = p3 = 0
        for c in range(8):
            v = int(t[r, c])
            p0 |= (v & 1) << (7 - c); p1 |= ((v >> 1) & 1) << (7 - c)
            p2 |= ((v >> 2) & 1) << (7 - c); p3 |= ((v >> 3) & 1) << (7 - c)
        out[2 * r] = p0; out[2 * r + 1] = p1
        out[16 + 2 * r] = p2; out[16 + 2 * r + 1] = p3
    return bytes(out)


def bgr555(c): r, g, b = c; return (b >> 3 << 10) | (g >> 3 << 5) | (r >> 3)


def emit(name, canvas, colors):
    tiles, tm = build_bg(canvas, subpal=SUBPAL[name])
    assert len(tiles) <= 1024, f"{name}: {len(tiles)} tiles > 1024"
    open(os.path.join(OUT, f"{name}.pic"), "wb").write(b"".join(enc4(t) for t in tiles))
    pal = [0] + [bgr555(c) for c in colors]
    pal += [0] * (16 - len(pal))                          # pad to a full 16-colour palette block
    open(os.path.join(OUT, f"{name}.pal"), "wb").write(struct.pack("<16H", *pal))
    open(os.path.join(OUT, f"{name}.map"), "wb").write(struct.pack("<1024H", *tm))
    print(f"{name}: {len(tiles)} 4bpp tiles, colors={colors}")


WHITE = (236, 232, 224)                                   # end-screen "THANKS FOR PLAYING" (soft white)
BLOOD = (200, 24, 24)                                      # game-over "GAME OVER" (blood red)
SUBPAL = {"title_logo": 1, "title_press": 2, "title_thanks": 1, "title_gameover": 1}   # CGRAM blocks 1/2
emit("title_logo",     place(snap_logo(),                     LOGO_CY),  [RED, PURPLE])
emit("title_press",    place(make_text("PRESS START"),        PRESS_CY), [GOLD])
emit("title_thanks",   place(make_text("THANKS FOR PLAYING"), 112),      [WHITE])
emit("title_gameover", place(make_text("GAME OVER"),          112),      [BLOOD])

# preview the composited title overlay (logo + press) on a dark sky, for eyeballing
prev = np.full((SCR_H, SCR_W, 3), (20, 8, 34), np.uint8)
for idx, cols in [(place(snap_logo(), LOGO_CY), [RED, PURPLE]), (place(make_text("PRESS START"), PRESS_CY), [GOLD])]:
    for v, c in enumerate(cols, 1):
        prev[idx == v] = c
Image.fromarray(prev).resize((SCR_W * 2, SCR_H * 2), Image.NEAREST).save("/tmp/title_preview.png")
print("preview -> /tmp/title_preview.png")
