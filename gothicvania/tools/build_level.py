#!/usr/bin/env python3
"""Reshape the 16x16 Tiled level into TWO independent SNES backgrounds -- 
for our own page-streaming renderer (no PVSnesLib map engine).

Why two layers (this replaced a single flattened background): Flattening both into one SNES background forces a per-pixel composite, which multiplies
the unique 8x8 tile count past the 1024-per-background limit -> a lossy merge -> recoloured /
fused object pixels. Keeping them as two backgrounds makes the tile counts ADD, not multiply:

    Main Layer (ground + grass)  ->  ~100 tiles   -> BG0 (front), palette 0
    Back Layer (decorations)     ->  ~900 tiles   -> BG1 (behind), palette 1

each well under 1024, so each is rendered PIXEL-EXACT with NO merge at all. We still squeeze
them with two LOSSLESS tricks: dedup identical 8x8 tiles, and dedup tiles that are H/V flips of
each other (the SNES tilemap has free per-cell flip bits). Mode-1 priority puts BG0 in front of
BG1, so the grass covers decoration bases while decoration tops show through the transparent sky.

Outputs into res/level/ (TAG = ground | deco):
  TAG_tileset.png    indexed (<=16 col) compact tileset; gfx4snes -R -> TAG_tileset.pic/.pal
  TAG_pagesA.bin     pre-expanded 32x32 streaming pages 0..14 (2-byte SNES tilemap entries:
  TAG_pagesB.bin     tile index + palette + H/V flip), pages 15..18; DMA'd straight ROM->VRAM.
  collision.bin      300x14 per-cell collision (0 empty, 1 solid, 2 jump-through platform)
"""
import json, os, sys, struct
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets", "Phaser Demo", "assets")
OUT  = os.path.join(ROOT, "res", "level")
os.makedirs(OUT, exist_ok=True)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from adapt_assets import to_indexed

mj = json.load(open(os.path.join(SRC, "maps", "map.json")))
W, H = mj["width"], mj["height"]                    # 300 x 14 (16x16 cells)
# The map uses TWO tilesets: 'tileset' (terrain, firstgid 1) and 'objects' (decorations,
# firstgid 281). Load both and pick by gid.
TSETS = []  # (firstgid, rgba_array, cols)
for tsd in sorted(mj["tilesets"], key=lambda t: t["firstgid"]):
    path = os.path.normpath(os.path.join(SRC, "maps", tsd["image"]))
    img = Image.open(path).convert("RGBA")
    TSETS.append((tsd["firstgid"], np.array(img), img.width // 16))
lyr = {L["name"]: L["data"] for L in mj["layers"] if L.get("type") == "tilelayer"}

def tile16(raw):
    gid = raw & 0x1FFFFFFF
    fg, arr, cols = TSETS[0]
    for t in TSETS:
        if gid >= t[0]:
            fg, arr, cols = t
    i = gid - fg; px = (i % cols) * 16; py = (i // cols) * 16
    tile = arr[py:py + 16, px:px + 16].copy()                  # 16x16 RGBA
    if raw & 0x20000000: tile = np.transpose(tile, (1, 0, 2))  # Tiled diagonal flip
    if raw & 0x80000000: tile = tile[:, ::-1]                  # horizontal flip
    if raw & 0x40000000: tile = tile[::-1, :]                  # vertical flip
    return tile

newW, newH = W * 2, H * 2                            # 600 x 28 (full height)
PAGE_W, PAGE_H = 32, 32
NP = (newW + PAGE_W - 1) // PAGE_W                    # 19 pages
SPLIT = 15                                            # pages 0..14 in A, 15..18 in B

def render_layer(name):
    """Render one Tiled tile-layer per-cell into an RGBA image. A layer never overlaps itself,
    so this is exact -- no compositing, no quantisation loss beyond the 16-colour palette."""
    img = np.zeros((H * 16, W * 16, 4), np.uint8)
    data = lyr.get(name)
    if not data:
        return img
    for y in range(H):
        for x in range(W):
            raw = data[y * W + x]
            if raw & 0x1FFFFFFF:
                img[y * 16:y * 16 + 16, x * 16:x * 16 + 16] = tile16(raw)
    return img

def declutter(img):
    """Drop fully-8-isolated opaque pixels (grass/branch tips that read as floating specks over
    the black sky). Detached by definition, so this never breaks a connected branch or blade."""
    op = img[:, :, 3] > 0
    R, C = op.shape; p = np.pad(op, 1); nb = np.zeros_like(op)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dy or dx:
                nb |= p[1 + dy:1 + dy + R, 1 + dx:1 + dx + C]
    iso = op & ~nb
    if iso.any():
        img[iso] = 0
    return int(iso.sum())

def build_layer(name, palette, tag):
    """Render -> quantise to its own <=16-colour palette -> dedup 8x8 UP TO H/V FLIP (free on the
    SNES) -> emit compact tileset.png + streaming pages with (tile|palette|flip) entries. No lossy
    merge."""
    img = render_layer(name)
    spk = declutter(img)
    qimg, nc = to_indexed(img, transparent=True, budget=16, name=tag)
    quant = np.array(qimg.convert("RGBA"))
    quant[img[:, :, 3] == 0] = 0                     # transparent (per original alpha) -> blank

    # Flip-canonical dedup: a tile and its H/V/HV mirrors share ONE stored tile; the cell carries
    # the flip bits that turn the stored tile back into what's displayed. Tile 0 = blank.
    cmap, ctiles = {}, []
    blank = np.zeros((8, 8, 4), np.uint8)
    cmap[blank.tobytes()] = 0; ctiles.append(blank)

    def entry(a, pal):
        orbit = [a, a[:, ::-1], a[::-1, :], a[::-1, ::-1]]
        bs = [o.tobytes() for o in orbit]
        k = min(bs)
        if k not in cmap:
            cmap[k] = len(ctiles); ctiles.append(orbit[bs.index(k)])
        idx = cmap[k]; canon = ctiles[idx]
        for h in (0, 1):
            for v in (0, 1):
                t = canon[:, ::-1] if h else canon
                t = t[::-1, :] if v else t
                if np.array_equal(t, a):
                    return idx | (pal << 10) | (h << 14) | (v << 15)
        raise RuntimeError("flip mismatch")          # impossible: a is in canon's orbit

    tm = [0] * (newW * newH)
    for ty in range(newH):
        for tx in range(newW):
            a = quant[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            if a[:, :, 3].any():
                tm[ty * newW + tx] = entry(a.copy(), palette)
    N = len(ctiles)
    assert N <= 1024, f"{tag}: {N} tiles > 1024 (10-bit tile field)"

    # Lossless self-check: rebuild the layer from tileset+tilemap (applying the flip bits) and
    # require it to equal the quantised layer EXACTLY -- no pixel moved, dropped, or recoloured.
    recon = np.zeros_like(quant)
    for ty in range(newH):
        for tx in range(newW):
            e = tm[ty * newW + tx]
            t = ctiles[e & 0x3FF]
            if (e >> 14) & 1: t = t[:, ::-1]
            if (e >> 15) & 1: t = t[::-1, :]
            recon[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = t
    assert np.array_equal(recon, quant), f"{tag}: reconstruction is not lossless!"

    # Compact tileset PNG for gfx4snes (re-index so it carries a clean <=16-colour palette).
    COLS = 16; ROWS = (N + COLS - 1) // COLS
    tim = Image.new("RGBA", (COLS * 8, ROWS * 8), (0, 0, 0, 0))
    for i, a in enumerate(ctiles):
        tim.paste(Image.fromarray(a, "RGBA"), ((i % COLS) * 8, (i // COLS) * 8))
    to_indexed(np.array(tim), transparent=True, budget=16, name=tag)[0].save(
        os.path.join(OUT, f"{tag}_tileset.png"))

    # Pre-expand into 32x32 streaming pages (256px each), row-major, padded past the level with 0.
    pages = []
    for pg in range(NP):
        for r in range(PAGE_H):
            for cc in range(PAGE_W):
                c = pg * PAGE_W + cc
                pages.append(tm[r * newW + c] if (r < newH and c < newW) else 0)
    pb = struct.pack("<%dH" % len(pages), *pages)
    cut = SPLIT * PAGE_W * PAGE_H * 2                 # 15 * 2048 = 30720 bytes
    open(os.path.join(OUT, f"{tag}_pagesA.bin"), "wb").write(pb[:cut])
    open(os.path.join(OUT, f"{tag}_pagesB.bin"), "wb").write(pb[cut:])
    print(f"{tag:6}: {N} tiles (flip-deduped, LOSSLESS), palette {nc} col, {spk} specks removed, "
          f"pages {len(pb)}B")
    return N

# Two backgrounds
# Palette slots leave CGRAM 0..15 for BG2's 2bpp parallax (which can only address low CGRAM):
#   BG2 mountains = entry 0 (CGRAM 0..3), deco = entry 1 (CGRAM 16..31), ground = entry 2 (32..47).
ng = build_layer("Main Layer", 2, "ground")          # ground + grass  -> BG0 front,  palette 2
nd = build_layer("Back Layer", 1, "deco")            # decorations     -> BG1 behind, palette 1

# Per-cell collision from the Collisions layer (unchanged). Emitted as a C const array so the
# player code can read it directly (a tcc const array is placed + far-addressed correctly).
col = lyr.get("Collisions Layer", [0] * (W * H))
collision = bytes((col[i] & 0x1FFFFFFF) & 0xFF for i in range(W * H))
open(os.path.join(OUT, "collision.bin"), "wb").write(collision)
cc = open(os.path.join(ROOT, "src", "level_collision.c"), "w")
cc.write("// generated by tools/build_level.py -- 16x16-cell collision (0 empty, 1/2 solid)\n")
cc.write(f"#define LEVEL_COLS {W}\n#define LEVEL_ROWS {H}\n")
cc.write(f"const unsigned char levelCollision[{W*H}] = {{\n")
for y in range(H):
    cc.write("  " + ",".join(str(collision[y * W + x]) for x in range(W)) + ",\n")
cc.write("};\n")
cc.close()

print(f"two backgrounds: ground={ng} tiles, deco={nd} tiles (both < 1024, no merge)")
print(f"collision: {W}x{H} = {len(collision)} bytes")
