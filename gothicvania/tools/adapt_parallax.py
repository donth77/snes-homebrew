#!/usr/bin/env python3
"""Far parallax onto ONE 2bpp BG2 (no free background: BG0/BG1 are the pixel-perfect level). Two
vertical bands, each its own 3-colour sub-palette, scrolled independently by HDMA (set in main.c):

  rows 0-103   sky   = background.png (moon+clouds) with the mountains composited IN FRONT, reduced
                       to 3 colours. The mountains keep their DETAIL because they're a dark silhouette
                       against the TEXTURED clouds (a flat sky would make them solid blobs). sub-pal 0;
                       dense mountain tiles get prio 1 (in front of the moon sprite). FIXED scroll
                       (static far background -- the only way the silhouette mountains stay detailed;
                       moving them needs a featureless sky, which flattens them).
  rows 104-223 graveyard  sub-pal 1 (teal), priority 1, scroll 0.25x (resized to 512 -> wraps clean).

gfx4snes can't do two sub-palettes in one 2bpp tileset, so we emit the .pic/.pal/.map ourselves.
Outputs into res/level/: parallax_tiles.pic, parallax_tiles.pal, parallax_map.bin
"""
import os, sys, struct
import numpy as np
from PIL import Image, ImageFilter
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from adapt_assets import to_indexed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV  = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets",
                    "Phaser Demo", "assets", "environment")
ENV2 = os.path.join(ROOT, "assets", "gothicvania-cemetery-files", "Assets", "Environment")
OUT  = os.path.join(ROOT, "res", "level")
BG_W, BG_H = 64, 32                                  # BG2 tilemap = 512x256
GBAND = 13                                           # first cell-row of the graveyard band (px 104). The
                                                     # mountains end here and the gravestones (closer layer)
                                                     # rise in front, so the mountain bases recede behind
                                                     # them rather than being "cut" 
GRV = [(0, 0, 34), (11, 31, 60), (28, 48, 70)]       # graveyard 3-colour sub-palette (teal)

def nearest_idx(img, pal):
    """opaque pixels -> index 1..3 (nearest of pal), 0 = transparent. Returns a HxW index array."""
    P = np.array(pal, np.int32); out = np.zeros(img.shape[:2], np.uint8)
    op = img[:, :, 3] > 0
    if op.any():
        d = np.abs(img[:, :, :3][op].astype(np.int32)[:, None, :] - P[None, :, :]).sum(2)
        out[op] = d.argmin(1) + 1
    return out

# --- SKY band: a clean MOON-GLOW GRADIENT (chosen over the noisy 3-colour clouds, which ate the tile
#     budget). Heavily blur background.png so the cloud texture dissolves into a smooth radial glow
#     (bright near the moon -> dark at the screen edges), then ORDERED-DITHER (Bayer 4x4) that smooth
#     glow to the 3 sky colours: a clean gradient that dedups to very few tiles, leaving budget for a
#     fully sharp graveyard. The mountains are painted ON TOP as a SHARP silhouette (darkest colour).
#     The pink moon SPRITE aligns over the glow centre (the baked bright disk shows around it as glow).
GLOW = float(os.environ.get("BG2_GLOW_BLUR", "16"))                                   # dissolve clouds -> glow
_skyimg = Image.open(os.path.join(ENV2, "background.png")).convert("RGBA").filter(ImageFilter.GaussianBlur(GLOW))
sky = np.array(_skyimg.resize((512, 224), Image.BILINEAR)).astype(np.int32)           # smooth glow, 512 wide
far = sky                                                                             # alias (preview block)
q, _ = to_indexed(sky.astype(np.uint8), transparent=True, budget=4, name="sky")       # 3 glow colours
qa = np.array(q.convert("RGBA")); op = sky[:, :, 3] > 0
SKY = sorted([tuple(int(x) for x in c) for c in np.unique(qa[op][:, :3].reshape(-1, 3), axis=0)[:3]],
             key=lambda c: 0.3 * c[0] + 0.6 * c[1] + 0.1 * c[2])                       # dark -> bright ramp
while len(SKY) < 3: SKY.append(SKY[-1])
# Option A: the moon's glow is now a smooth 16-colour SPRITE, so the BG sky is FLAT dark -- no gradient,
# so nothing can band/block. Use the EXACT colour the moon's glow fades to (written by adapt_moon.py) so
# the sprite melts into the sky with zero seam. (Mountains stay in front; their gaps show the moon.)
_D = tuple(int(x) for x in open(os.path.join(ROOT, "res", "sky_dark.txt")).read().split(","))
SKY = [_D, _D, _D]
_Lr = [0.3 * c[0] + 0.6 * c[1] + 0.1 * c[2] for c in SKY]                              # ramp luminances
_lum = (sky[:, :, :3] * [0.3, 0.6, 0.1]).sum(2)                                        # per-pixel luminance
_t = np.where(_lum < _Lr[1], (_lum - _Lr[0]) / max(_Lr[1] - _Lr[0], 1.0),
              1.0 + (_lum - _Lr[1]) / max(_Lr[2] - _Lr[1], 1.0))                       # 0..2 along the ramp
_B4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], float)
_boff = np.tile((_B4 + 0.5) / 16.0 - 0.5, (56, 128))[:224, :512]                       # tiled Bayer offset
sky_idx = (np.clip(np.round(_t + _boff), 0, 2).astype(np.uint8) + 1)                   # ordered-dithered 1..3
mtn = np.array(Image.open(os.path.join(ENV, "bg-mountains.png")).convert("RGBA"))
_ma = np.array(Image.fromarray(mtn[:, :, 3]).resize((512, 224), Image.LANCZOS))        # SOFT alpha (de-blocky)
# SHADED mountains (their own sub-palette 2): keep the source's moonlit faces/slopes instead of
# flattening to one dark silhouette. Quantise the mountain art to 3 luminance shades (dark body ->
# moonlit edge) and store them in the combined index; the per-tile palette picker (below) tags
# mountain tiles as sub-pal 2 so values 1..3 map to the MOUNTAIN colours, while sky tiles stay
# sub-pal 0 (the glow). Soft LANCZOS alpha -> smooth edge, no hard stair-stepped outline.
_mrgb = np.array(Image.fromarray(mtn[:, :, :3]).resize((512, 224), Image.LANCZOS)).astype(int)
_mop  = _ma > 110                                                                       # silhouette mask
_lum  = (_mrgb * np.array([0.3, 0.6, 0.1])).sum(2)
if _mop.any():
    _q = np.percentile(_lum[_mop], [25, 60])
    MTN = []
    for _lo, _hi in [(-1.0, _q[0]), (_q[0], _q[1]), (_q[1], 1e18)]:
        _sel = _mop & (_lum > _lo) & (_lum <= _hi)
        MTN.append(tuple(int(x) for x in _mrgb[_sel].mean(0)) if _sel.any() else (60, 20, 75))
else:
    MTN = [(30, 8, 45), (55, 20, 70), (95, 45, 115)]
_mdist  = np.stack([((_mrgb - np.array(c)) ** 2).sum(2) for c in MTN], 0)
_mshade = _mdist.argmin(0) + 1                                                          # 1..3 dark->light
# The brightest mountain shade == the glow colour: this is the moonlit rim AND it makes the SKY part
# of mountain-edge tiles (which render through sub-pal 2) blend into the glow -> no rectangular blocks.
MTN[-1] = SKY[2]
sky_idx[_mop] = _mshade[_mop]                                                           # mountains -> their shades
# THE FIX for "mountains cut off": below the sky band we fill the whole graveyard background with a solid
# block of the mountains' OWN darkest colour, so the mountain mass simply continues straight down past
# the band cut instead of ending on an edge (the gravestones then rise in FRONT of that block). Make the
# graveyard sub-palette's darkest entry == the mountain's darkest colour so the block matches exactly.
GRV[0] = tuple(int(x) for x in MTN[0])

# --- GRAVEYARD band: resized to 512 (it scrolls far enough to wrap, so it must tile seamlessly).
grv = np.array(Image.open(os.path.join(ENV, "bg-graveyard.png")).convert("RGBA")
               .resize((512, 224), Image.NEAREST))
grv_idx = nearest_idx(grv, GRV)
grv_idx[grv_idx == 0] = 1                             # fill the transparent background with GRV[0] (= the
                                                     # mountain colour): a solid block under the mountains

def coarsen(a, n):                                   # snap each nxn block to its top-left pixel
    h, w = a.shape
    return np.repeat(np.repeat(a[::n, ::n], n, 0), n, 1)[:h, :w]

# Coarsening factors (1 = full-res sharp). Blurring the clouds frees enough tile budget that the
# upper clouds + visible graveyard can stay sharp; only the level-occluded lower graveyard is coarsened.
_U  = int(os.environ.get("BG2_UPPER", "1"))           # upper clouds (rows 0-47)
_GV = int(os.environ.get("BG2_GVVIS", "2"))           # graveyard strip (mostly occluded by the level -> coarse)
_GH = int(os.environ.get("BG2_GVHID", "2"))           # lower graveyard (hidden behind the level -> coarse)
idximg = np.zeros((256, 512), np.uint8)
idximg[0:48]    = coarsen(sky_idx[0:48], _U)          # upper sky (cleared to transparent below)
idximg[48:104]  = sky_idx[48:104]                     # mountains + lower sky -> always SHARP
idximg[104:160] = coarsen(grv_idx[104:160], _GV)      # graveyard: full gravestones (they start at row ~101)
idximg[160:224] = coarsen(grv_idx[160:224], _GH)      # lower graveyard (mostly behind the level) -> coarse

# COLOR-MATH SKY: the sky/glow is now a smooth per-scanline gradient painted on the BACKDROP by HDMA
# on $2132 (gen_sky_gradient.py + main.c) -- NEVER CGRAM, so it can't corrupt the palette. So the BG2
# sky itself is fully TRANSPARENT (index 0) and that gradient backdrop shows through; only the mountain
# silhouette stays opaque (in front). Clearing the baked glow is what removes ALL sky banding.
idximg[0:GBAND * 8][~_mop[0:GBAND * 8]] = 0           # sky rows: keep mountains, drop everything else

# The mountains are NOT cut -- they keep their full solid base and flow straight down into the graveyard
# band below (which is filled dark, see grv_idx), so there's no cut-off edge and no isolated rectangle:
# the dark mountain mass continues into the dark graveyard, with the gravestones rising in front of it.

# Dedup 8x8 index patterns up to H/V flip; a shared pattern stores one tile, the tilemap picks
# sub-palette + priority.
cmap, tiles = {}, []
blank = np.zeros((8, 8), np.uint8); cmap[blank.tobytes()] = 0; tiles.append(blank)
def entry(a, subpal, prio):
    orbit = [a, a[:, ::-1], a[::-1, :], a[::-1, ::-1]]
    bs = [o.tobytes() for o in orbit]; k = min(bs)
    if k not in cmap:
        cmap[k] = len(tiles); tiles.append(orbit[bs.index(k)])
    ti = cmap[k]; canon = tiles[ti]
    for h in (0, 1):
        for v in (0, 1):
            t = canon[:, ::-1] if h else canon
            t = t[::-1, :] if v else t
            if np.array_equal(t, a):
                return ti | (subpal << 10) | (prio << 13) | (h << 14) | (v << 15)
    raise RuntimeError("flip mismatch")

# Sky band sub-pal 0; the DENSE mountain tiles (the solid ridge) get PRIORITY 1 so the ridge sits IN
# FRONT of the moon sprite (BG2.1 > OBJ0) = the moon rises behind the mountains. Sparse-peak/cloud
# tiles stay prio 0 (behind the moon) so its body stays clear (the detailed ridge reads as a natural
# horizon, not the blocky cut the 3-band flat mountains made). Graveyard = sub-pal 1 / prio 1.
mtn_mask = np.array(Image.fromarray(((mtn[:, :, 3] > 0).astype(np.uint8) * 255))
                    .resize((512, 224), Image.NEAREST)) > 127
# Mountains sit IN FRONT of the moon (priority 1), but in those tiles only the actual mountain PIXELS
# stay opaque -- the gaps are cleared to transparent (index 0) so the moon shows through at PIXEL
# resolution. A whole opaque mountain tile would step the moon's edge in 8px blocks; this keeps the
# silhouette pixel-sharp. (The glow behind the ridge is hidden by the ridge anyway, as in the source.)
for _ty in range(GBAND):
    for _tx in range(BG_W):
        if mtn_mask[_ty * 8:_ty * 8 + 8, _tx * 8:_tx * 8 + 8].mean() > 0.35:
            _blk = idximg[_ty * 8:_ty * 8 + 8, _tx * 8:_tx * 8 + 8]
            _blk[~_mop[_ty * 8:_ty * 8 + 8, _tx * 8:_tx * 8 + 8]] = 0
tm = [0] * (BG_W * BG_H)
for ty in range(BG_H):
    for tx in range(BG_W):
        a = idximg[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
        if not a.any():
            continue
        if ty < GBAND:                                   # sky band (clouds + mountains)
            mblk = mtn_mask[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            if mblk.mean() > 0.35:
                sp, pr = 2, 1                             # mountains IN FRONT of the moon (pixel-level silhouette)
            else:
                sp, pr = 0, 0                             # sky/glow, behind the moon
        else:                                            # graveyard band
            sp, pr = 1, 1
        tm[ty * BG_W + tx] = entry(a.copy(), sp, pr)
N = len(tiles)
assert N <= 1024, f"parallax: {N} tiles > 1024"

# self-check
recon = np.zeros((256, 512), np.uint8)
for ty in range(BG_H):
    for tx in range(BG_W):
        e = tm[ty * BG_W + tx]; t = tiles[e & 0x3FF]
        if (e >> 14) & 1: t = t[:, ::-1]
        if (e >> 15) & 1: t = t[::-1, :]
        recon[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = t
assert np.array_equal(recon, idximg), "parallax: reconstruction not lossless!"

def enc(t):                                          # 2bpp encode (16 bytes/tile)
    out = bytearray()
    for r in range(8):
        p0 = p1 = 0
        for c in range(8):
            v = int(t[r, c]); p0 |= (v & 1) << (7 - c); p1 |= ((v >> 1) & 1) << (7 - c)
        out += bytes([p0, p1])
    return bytes(out)
open(os.path.join(OUT, "parallax_tiles.pic"), "wb").write(b"".join(enc(t) for t in tiles))

def bgr555(c): r, g, b = c; return (b >> 3 << 10) | (g >> 3 << 5) | (r >> 3)
# sub-pal 0 (CGRAM 0-3) = sky/glow | sub-pal 1 (4-7) = graveyard | sub-pal 2 (8-11) = shaded mountains.
# CGRAM0 (backdrop) = darkest mountain shade so the cut-off below the ridge blends into the horizon.
pal = ([bgr555(_D)] + [bgr555(c) for c in SKY] + [0] + [bgr555(c) for c in GRV]
       + [0] + [bgr555(c) for c in MTN])                                                # backdrop = flat sky D
open(os.path.join(OUT, "parallax_tiles.pal"), "wb").write(struct.pack("<12H", *pal))

pages = []
for pg in range(2):
    for r in range(32):
        for cc in range(32):
            pages.append(tm[r * BG_W + pg * 32 + cc])
open(os.path.join(OUT, "parallax_map.bin"), "wb").write(struct.pack("<%dH" % len(pages), *pages))
print(f"parallax: {N} 2bpp tiles. sky(moon+clouds+detailed mountains) {SKY}, graveyard {GRV}")

if os.environ.get("BG2_PREVIEW"):                    # debug: render source vs quantized BG2 to /tmp
    from PIL import Image as _I
    _I.fromarray(far[:, :, :3].astype(np.uint8)).save("/tmp/bg2_src.png")
    _rgb = np.zeros((256, 512, 3), np.uint8)
    for _v, _c in enumerate(SKY, 1): _rgb[:104][idximg[:104] == _v] = _c
    for _v, _c in enumerate(GRV, 1): _rgb[104:][idximg[104:] == _v] = _c
    _I.fromarray(_rgb).save("/tmp/bg2_quant.png")
    _ys = np.where(mtn_mask.any(1))[0]
    print(f"PREVIEW: mountain rows {_ys.min()}..{_ys.max()}  glow={GLOW} coarsen upper={_U} "
          f"gv_vis={_GV} gv_hid={_GH}. src=/tmp/bg2_src.png quant=/tmp/bg2_quant.png")
