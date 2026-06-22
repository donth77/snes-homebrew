#!/usr/bin/env python3
"""Step 1 of the enemy work: turn the deco layer (BG1) from ALL-tiles-resident into a
per-page STREAMED layer, so most of its 28KB of VRAM is freed for bigger enemy sprites.

This is pure codegen on top of build_level.py's existing output -- it does NOT touch build_level.py
or the current renderer, so the working build is unaffected until we opt in. It reads:
  res/level/deco_tileset.pic   (the 880 4bpp tiles, 32 bytes each)
  res/level/deco_pagesA/B.bin  (the 19 pages x 32x32 global tilemap entries)
and emits, per page, only the tiles that page uses, re-indexed local to a 2-slot window:
  deco_pagetiles.bin   concatenated per-page 4bpp tiles (local 0 = blank, then the page's tiles)
  deco_pageinfo.bin    per-page (u16 tileOffset_in_tiles, u16 tileCount) into deco_pagetiles.bin
  deco_smapsA/B.bin     per-page 32x32 maps with WINDOW-LOCAL indices (even page -> slot0 base 0,
                        odd page -> slot1 base 256), palette/prio/flip bits preserved.

The renderer (Step 2) DMAs a page's tiles into its parity slot (even->0x4000, odd->0x5000, relative
to BG1's char base) + its local map. Window = 512 tiles (0x4000..0x6000, 16KB) vs 880 resident (28KB).

LOSSLESS by construction (local tiles are copied from the global .pic; the map keeps the same
flip/palette bits) -- asserted below by reconstructing each page and comparing to the global version.
"""
import os, struct
import numpy as np

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "res", "level")
SLOT = 256                                           # tiles per window slot (max page uses 239)
PAGE = 32

pic = open(os.path.join(OUT, "deco_tileset.pic"), "rb").read()      # 880 tiles x 32 bytes
ntiles_global = len(pic) // 32
pa = open(os.path.join(OUT, "deco_pagesA.bin"), "rb").read()
pb = open(os.path.join(OUT, "deco_pagesB.bin"), "rb").read()
pages = np.frombuffer(pa + pb, np.uint16).reshape(-1, PAGE, PAGE)   # (NP,32,32)
NP = pages.shape[0]

SECTION_MAX = 24 * 1024                              # each .bin must fit one 32KB bank (a page DMA can't
                                                     # cross a bank); 24KB leaves room for WLA to place it.
page_ptiles = []                                     # per-page 4bpp tile bytes (local 0 = blank, then used)
page_used = []                                       # per-page list of global tile indices used
smaps = []
maxcount = 0
for pg in range(NP):
    pm = pages[pg]
    gid = (pm & 0x3FF).astype(np.int32)              # global tile index
    upper = (pm & 0xFC00).astype(np.uint16)          # palette(10-12) + prio(13) + flip(14-15)
    used = sorted({int(g) for g in gid.flatten() if g != 0})
    assert len(used) <= SLOT, f"page {pg}: {len(used)} tiles > slot {SLOT}"
    maxcount = max(maxcount, len(used))
    local = {0: 0}
    for li, g in enumerate(used, start=1):
        local[g] = li
    slotbase = SLOT if (pg & 1) else 0               # even page -> slot 0, odd page -> slot 1

    lm = np.zeros((PAGE, PAGE), np.uint16)
    for r in range(PAGE):
        for c in range(PAGE):
            g = int(gid[r, c])
            if g != 0:
                lm[r, c] = np.uint16((local[g] + slotbase) | int(upper[r, c]))
    smaps.append(lm)
    page_used.append(used)
    pt = bytearray(bytes(32))                         # local 0 = blank tile
    for g in used:
        pt += pic[g * 32: g * 32 + 32]
    page_ptiles.append(bytes(pt))

# bank-pack pages into sections (no page split across a section/bank). info = (section, byteOffset, count)
sections = [bytearray()]
info = []
for pg in range(NP):
    pt = page_ptiles[pg]
    if len(sections[-1]) + len(pt) > SECTION_MAX:
        sections.append(bytearray())
    info.append((len(sections) - 1, len(sections[-1]), len(pt) // 32))
    sections[-1] += pt

# --- lossless self-check: each cell's local tile (from its section+offset) == the global tile. ---
for pg in range(NP):
    sec, boff, cnt = info[pg]; slotbase = SLOT if (pg & 1) else 0
    pm = pages[pg]; lm = smaps[pg]; sdata = sections[sec]
    for r in range(PAGE):
        for c in range(PAGE):
            g = int(pm[r, c] & 0x3FF)
            if g == 0:
                continue
            li = int(lm[r, c] & 0x3FF) - slotbase     # local index within the page
            loc = sdata[boff + li * 32: boff + li * 32 + 32]
            assert pic[g * 32: g * 32 + 32] == loc, f"page {pg} cell {r},{c}: tile mismatch"
            assert (lm[r, c] & 0xFC00) == (pm[r, c] & 0xFC00), f"page {pg}: flip/pal mismatch"

for i, s in enumerate(sections):
    open(os.path.join(OUT, f"deco_pt{i}.bin"), "wb").write(bytes(s))
open(os.path.join(OUT, "deco_pageinfo.bin"), "wb").write(
    b"".join(struct.pack("<HHH", sec, off, cnt) for sec, off, cnt in info))
sm = b"".join(lm.tobytes() for lm in smaps)
cut = 15 * PAGE * PAGE * 2
open(os.path.join(OUT, "deco_smapsA.bin"), "wb").write(sm[:cut])
open(os.path.join(OUT, "deco_smapsB.bin"), "wb").write(sm[cut:])

tot = sum(len(s) for s in sections)
print(f"deco stream: {NP} pages, max {maxcount} tiles/page (slot {SLOT}); {len(sections)} sections "
      f"(sizes { [len(s)//1024 for s in sections] }KB), total {tot//1024}KB (vs {ntiles_global*32//1024}KB "
      f"resident); window 2x{SLOT}={2*SLOT*32//1024}KB VRAM. LOSSLESS verified.")
