# Plan: 4 on-screen enemies (lifting the 2-enemy cap)

**Status:** deferred. Current build supports **2** on-screen enemies of any type (skeleton /
hell-gato / ghost share the two slots, streamed per-frame). This documents how to reach **4**.

## Why it's 2 today

OBJ VRAM is 16 KB / 512 tiles total, carved up as:

| Region        | VRAM            | Tiles    | Use                                  |
|---------------|-----------------|----------|--------------------------------------|
| Hero          | `$0000–$0800`   | 0–127    | one 64×64 frame (L+R halves), 4 KB   |
| Moon          | `$0800–$1000`   | 128–255  | one 64×64 sprite, 4 KB               |
| **Enemies**   | `$1000–$2000`   | 256–511  | **8 KB**                             |

A 64×64 sprite is 64 tiles = 2 KB of *content*. But uploading one reliably is the catch: a 64×64
OBJ reads its tiles strided across the 16-wide OBJ grid (cols 0–7 of 8 rows), so a clean upload
needs **one contiguous DMA**, not 8 strided per-row DMAs (those starve in the music-shortened
VBlank → torn/half sprites). The fix we shipped pads each frame to a **128-wide band** (skeleton
left, blank right) so gfx4snes lays it out contiguous and we DMA two 2 KB halves like the hero.

That makes each enemy **4 KB** (half of it blank padding). 8 KB ÷ 4 KB = **2 enemies.**

## The target

Reach **4 enemies** in the same 8 KB by *not wasting the blank half* — put **two enemies in one
128-wide band**: enemy A in the left 64×64, enemy B in the right 64×64. Then one band (4 KB) holds
two enemies, and 8 KB holds four. This is exactly how the band is laid out today; we just need to
fill the right half with a *second enemy's frame* instead of blank.

## Approach A — WRAM-composited bands (recommended)

The problem: enemies A and B animate independently, so their two halves change on different frames.
gfx4snes packs them at build time (static); we need to combine **two arbitrary current frames** at
runtime, then upload the combined 128-wide band contiguously.

Pipeline per band, per upload:
1. Keep a **128-wide (4 KB) staging buffer in WRAM** (`$7E`/`$7F` — we have ~120 KB free).
2. When either enemy in the band changes frame, rebuild the band in WRAM: copy enemy A's 2 KB
   frame into the left half, enemy B's 2 KB into the right half. (Both source frames are already
   the contiguous 2 KB "left 64×64" layout from gfx4snes `-s 64`.)
3. DMA the 4 KB WRAM band → its VRAM slot, **two 2 KB halves across two VBlanks**, exactly like
   the hero — so it still fits a starved VBlank.

OAM: each 64×64 in the band is its own OBJ. The left enemy uses the band's base name; the right
enemy uses base + 8 (the +8 tile-column offset on the same grid rows, like the hero's L/R halves).

### The one hard part
PVSnesLib has **no WRAM→VRAM staging-DMA helper**, and no "memcpy in WRAM" that's cheap enough.
Two ways to build the WRAM band in step 2:
- **`memcpy` (simple, slower):** copy 2×2 KB/frame on the CPU. ~4 KB of byte copies when a frame
  changes is a few % of a frame — measure it; if enemies change frame every ~6 frames and you have
  4 of them, worst case is a couple of copies/frame. Probably fine; profile in Mesen's event viewer.
- **Hand-rolled WRAM→WRAM DMA (fast):** DMA can copy WRAM→WRAM (general-purpose channel, B-bus to
  WRAM via `$2180`), offloading the copy from the CPU. More code; do this only if `memcpy` profiles
  too hot.

### Steps
1. Add a 4 KB WRAM staging buffer per band (2 bands → 8 KB WRAM; trivial).
2. Change `enemyDstBase` to 2 bands but **4 enemy slots** (slots 0/1 → band 0 L/R, slots 2/3 →
   band 1 L/R), with names `{256, 256+8, 384, 384+8}`.
3. Rewrite `enemyVBlankUpload`: when a slot is dirty, rebuild its band's WRAM buffer (its half),
   then upload that band's two halves. Track per-band "dirty" rather than per-slot.
4. Bump `ENEMY_SLOTS` 2→4; the spawn loop, combat, and draw already index a pool — they scale.
5. OAM: 4 enemy OBJs need 4 OAM slots. Today enemies use OBJ 0/4 (in front of the hero at 8/12).
   Four enemies want OBJ 0/4/16/20 (keep them all below the hero's 8/12). Re-check the z-order map.

### Risks
- **VBlank budget:** four streamed enemies + hero + page seams can't *all* upload in one VBlank.
  The round-robin already serializes uploads (≤1 enemy half/VBlank, deferred behind page streams);
  with 4 enemies a frame change may take a few VBlanks to show. At walk speed that's invisible, but
  verify no torn frames when 4 change at once near a page seam.
- **Profile the WRAM rebuild** (event viewer) before committing to memcpy vs DMA.

## Approach B — smaller enemies (cheaper, changes the look)
Author enemies at **32×32** (256 B/frame). A 32×32 needs no padding trick (it fits the grid cleanly),
streams in one small DMA, and **8+ fit the 8 KB** with margin. This matches how SNES games actually
did crowds. Cost: enemies become half-size vs the hero — a real art decision, and a deviation from
the demo's proportions. **For a future original game (the jam), prefer this** — design enemies at
32×32 up front and skip the compositing complexity entirely.

## Approach C — reclaim VRAM
The moon is a static 4 KB 64×64 that never animates. It could live in BG VRAM (drawn as a BG sprite
layer) or be shrunk, freeing a 3rd enemy band (→ 3 enemies, or 6 with compositing). Lower priority;
only worth it if 4 via Approach A still isn't enough.

## Recommendation
- **For this Gothicvania port:** Approach A (WRAM-composited bands) keeps the demo's 64×64 look and
  reaches 4 — enough for the demo's densest cluster (skeleton + gato + skeleton around tiles 200–210).
- **For the jam game:** Approach B — design 32×32 enemies and never hit this wall.
