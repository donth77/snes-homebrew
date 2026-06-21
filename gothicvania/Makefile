ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error "Set the PVSNESLIB_HOME environment variable (see the repo README).")
endif

# Audio: title song (Title.mid) + in-game Baroque (Baroque.mid) -> .it (tools/mid2it.py) -> ONE soundbank
# via smconv. AUDIOFILES order sets the MOD_* indices: title.it -> MOD_TITLE(0), song.it -> MOD_SONG(1).
# (Must be set BEFORE including snes_rules, which generates + links $(SOUNDBANK).asm.)
# effectssfx.it MUST be first (the -f flag marks the first module as the loadable SFX bank):
#   effectssfx.it -> MOD_EFFECTSSFX(0), title.it -> MOD_TITLE(1), baroque.it -> MOD_BAROQUE(2).
AUDIOFILES := res/effectssfx.it res/title.it res/baroque.it
export SOUNDBANK := res/soundbank

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

SMCONVFLAGS := -s -o $(SOUNDBANK) -V -b 5 -f   # -f: 1st module = SFX bank; soundbank in ROM bank 5

.PHONY: bitmaps all

export ROMNAME := gothicvania

all: bitmaps $(ROMNAME).sfc

clean: cleanBuildRes cleanRom cleanGfx

#---------------------------------------------------------------------------------
# Level data: tools/build_level.py reshapes the demo's 16x16 map into our 8x8 tileset
# (res/level/tileset.png) + pre-expanded streaming pages (pagesA/B.bin) + collision.bin.
# The .stamp keeps that single multi-output run from re-firing per output. gfx4snes -R
# keeps every tile (no reduction/reorder) so page tile indices stay 1:1 — NO tmx2snes.
#---------------------------------------------------------------------------------
# MIDI -> Impulse Tracker modules (tools/mid2it.py); snes_rules' smconv packs them into the soundbank.
res/baroque.it: tools/mid2it.py assets/Baroque.mid
	python3 tools/mid2it.py
res/title.it: tools/mid2it.py assets/Title.mid
	python3 tools/mid2it.py title
# OGG sound effects -> one effects module (tools/adapt_sfx.py reuses mid2it.py's .it writer).
res/effectssfx.it: tools/adapt_sfx.py tools/mid2it.py
	python3 tools/adapt_sfx.py

res/level/.stamp: tools/build_level.py
	python3 tools/build_level.py
	touch $@

res/level/ground_tileset.pic: res/level/.stamp
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -t png -i res/level/ground_tileset.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -t png -i res/level/deco_tileset.png
	touch data.asm   # data.asm incbins these; make can't see incbin deps, so force ROM rebuild

# Deco tile-STREAMING: reshape the resident deco tileset+pages into per-page streamed tiles (bank-packed)
# + window-local maps, so only on-screen deco tiles are VRAM-resident (frees ~11KB for enemy sprites).
res/level/deco_pt0.bin: tools/build_stream.py res/level/ground_tileset.pic res/level/.stamp
	python3 tools/build_stream.py
	touch data.asm

# Parallax (mountains + graveyard) -> ONE 2bpp BG2 with TWO sub-palettes; adapt_parallax.py emits
# the .pic/.pal/.map directly (gfx4snes can't do two sub-palettes in one 2bpp tileset).
res/level/parallax_tiles.pic: tools/adapt_parallax.py res/level/sky_coldata.bin
	python3 tools/adapt_parallax.py
	touch data.asm

# Sky gradient: per-scanline COLOR-MATH HDMA table ($2132 COLDATA, never CGRAM -> can't corrupt the
# palette). gen_sky_gradient.py ALSO writes src/sky_params.h (backdrop colour, included by main.c) and
# res/sky_dark.txt (the moon-band colour, read by adapt_moon + adapt_parallax), so it must run first.
res/level/sky_coldata.bin: tools/gen_sky_gradient.py
	python3 tools/gen_sky_gradient.py
	touch data.asm src/main.c

# Moon: tools/adapt_moon.py extracts the disk + glow -> 128x64 sprite. Depends on the gradient so the
# glow fades to the same moon-band colour (res/sky_dark.txt) -> the sprite melts into the sky.
res/moon.png: tools/adapt_moon.py res/level/sky_coldata.bin
	python3 tools/adapt_moon.py

res/moon.pic: res/moon.png
	$(GFXCONV) -s 64 -o 16 -u 16 -R -t png -i res/moon.png
	touch data.asm

# Title screen overlays: "GothicVania" logo (BG0) + "PRESS START" (BG1), 4bpp .pic/.pal/.map emitted
# directly by adapt_title.py (like adapt_parallax), composited over the live moon/mountains/sky by main.c.
res/title_logo.pic: tools/adapt_title.py
	python3 tools/adapt_title.py
	touch data.asm

# Hero: all animations -> one 64x64 feet-anchored frame strip (tools/adapt_hero.py),
# then gfx4snes -R (keep every tile so each frame is exactly 64 tiles = 2048 bytes),
# then split into two banks so the player code can DMA one frame at a time.
res/hero.png src/hero_anim.h: tools/adapt_hero.py
	python3 tools/adapt_hero.py

res/hero.pic: res/hero.png
	$(GFXCONV) -s 64 -o 16 -u 16 -R -t png -i $<

res/hero_a.bin: res/hero.pic
	dd if=res/hero.pic of=res/hero_a.bin bs=4096 count=7 2>/dev/null
	dd if=res/hero.pic of=res/hero_b.bin bs=4096 skip=7 count=7 2>/dev/null
	dd if=res/hero.pic of=res/hero_c.bin bs=4096 skip=14 2>/dev/null
	touch data.asm

bitmaps: res/hero_a.bin res/level/.stamp res/level/ground_tileset.pic res/level/deco_pt0.bin res/level/parallax_tiles.pic res/level/sky_coldata.bin res/moon.pic res/title_logo.pic
