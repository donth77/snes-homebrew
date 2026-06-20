ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error "Set the PVSNESLIB_HOME environment variable (see the repo README).")
endif

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

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
res/level/.stamp: tools/build_level.py
	python3 tools/build_level.py
	touch $@

res/level/ground_tileset.pic: res/level/.stamp
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -t png -i res/level/ground_tileset.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -t png -i res/level/deco_tileset.png
	touch data.asm   # data.asm incbins these; make can't see incbin deps, so force ROM rebuild

# Parallax (mountains + graveyard) -> ONE 2bpp BG2 with TWO sub-palettes; adapt_parallax.py emits
# the .pic/.pal/.map directly (gfx4snes can't do two sub-palettes in one 2bpp tileset).
res/level/parallax_tiles.pic: tools/adapt_parallax.py
	python3 tools/adapt_parallax.py
	touch data.asm

# Sky gradient: per-scanline backdrop colour HDMA table sampled from the demo's bg-moon sky.
res/level/sky_gradient.bin: tools/adapt_sky.py
	python3 tools/adapt_sky.py
	touch data.asm

# Moon: tools/adapt_moon.py extracts the disk -> 64x64 sprite (gfx4snes -s 64, like the hero).
res/moon.png: tools/adapt_moon.py
	python3 tools/adapt_moon.py

res/moon.pic: res/moon.png
	$(GFXCONV) -s 64 -o 16 -u 16 -R -t png -i res/moon.png
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

bitmaps: res/hero_a.bin res/level/.stamp res/level/ground_tileset.pic res/level/parallax_tiles.pic res/level/sky_gradient.bin res/moon.pic
