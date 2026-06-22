.include "hdr.asm"

; --- Level = TWO backgrounds, exactly the two layers (built by tools/build_level.py;
;     rendered by our own lockstep page-streamer, not PVSnesLib's map engine). Splitting the
;     layers instead of flattening keeps each tileset under 1024 tiles, so both are pixel-exact
;     with no lossy merge. Each .incbin stays under WLA's 32KB section (= one LoROM bank); a DMA
;     never crosses a bank, and a page is bank-internal, so every page DMA is hardware-safe. ---

; Ground layer (Main: grass + dirt + skulls) -> BG0 (front), palette 0
.section ".rodata_groundtiles" superfree
groundtiles:    .incbin "res/level/ground_tileset.pic"
groundtilesend:
groundpal:      .incbin "res/level/ground_tileset.pal"
groundpalend:
.ends
.section ".rodata_groundpagesA" superfree
groundpagesA:   .incbin "res/level/ground_pagesA.bin"   ; pages 0..14
.ends
.section ".rodata_groundpagesB" superfree
groundpagesB:   .incbin "res/level/ground_pagesB.bin"   ; pages 15..18
.ends

; Decoration layer (Back: trees + gravestones + crosses) -> BG1 (behind), palette 1. STREAMED: only the
; on-screen pages' tiles are resident (a 2-slot window), freeing ~11KB of VRAM for enemy
; sprites. build_stream.py emits per-page tiles (bank-packed into deco_pt0..2 so no page-DMA crosses a
; bank) + per-page window-local maps; the renderer DMAs a page's tiles into its parity slot.
.section ".rodata_decopal" superfree
decopal:        .incbin "res/level/deco_tileset.pal"
decopalend:
.ends
.section ".rodata_decopt0" superfree
deco_pt0:       .incbin "res/level/deco_pt0.bin"        ; per-page 4bpp tiles, bank-packed section 0
.ends
.section ".rodata_decopt1" superfree
deco_pt1:       .incbin "res/level/deco_pt1.bin"        ; section 1
.ends
.section ".rodata_decopt2" superfree
deco_pt2:       .incbin "res/level/deco_pt2.bin"        ; section 2
.ends
.section ".rodata_decopageinfo" superfree
deco_pageinfo:  .incbin "res/level/deco_pageinfo.bin"   ; per-page (u16 section, u16 byteOffset, u16 count)
.ends
.section ".rodata_decosmapsA" superfree
deco_smapsA:    .incbin "res/level/deco_smapsA.bin"     ; window-local maps, pages 0..14
.ends
.section ".rodata_decosmapsB" superfree
deco_smapsB:    .incbin "res/level/deco_smapsB.bin"     ; pages 15..18
.ends

; Parallax far layer: mountains silhouette -> BG2 (2bpp / 4-colour). Static repeating 512px
; background (56 tiles + a 64x32 map), scrolled at 0.07x; no streaming.
.section ".rodata_parallax" superfree
parallaxtiles:  .incbin "res/level/parallax_tiles.pic"
parallaxtilesend:
parallaxpal:    .incbin "res/level/parallax_tiles.pal"
parallaxpalend:
parallaxmap:    .incbin "res/level/parallax_map.bin"     ; two 32x32 screens (4096B)
.ends

; Sky gradient: per-scanline COLOR-MATH HDMA table (channel 6 -> $2132 COLDATA, NOT CGRAM, so it can't
; corrupt the palette). gen_sky_gradient.py builds it; main.c arms it. Mode-2: red byte then blue byte
; per scanline = a smooth dark->moonlit magenta ramp added to the backdrop.
.section ".rodata_sky" superfree
sky_coldata:    .incbin "res/level/sky_coldata.bin"
.ends

; Title screen overlays: "GothicVania" logo -> BG0, "PRESS START" -> BG1 (both 4bpp), composited over
; the live moon + scrolling mountains + sky-gradient scene (the TitleScreen, sans credits/instr).
.section ".rodata_title" superfree
title_logo_tiles:  .incbin "res/title_logo.pic"
title_logo_tilesend:
title_logo_pal:    .incbin "res/title_logo.pal"
title_logo_map:    .incbin "res/title_logo.map"
title_press_tiles: .incbin "res/title_press.pic"
title_press_tilesend:
title_press_pal:   .incbin "res/title_press.pal"
title_press_map:   .incbin "res/title_press.map"
title_thanks_tiles: .incbin "res/title_thanks.pic"   ; END screen "THANKS FOR PLAYING" overlay (BG0)
title_thanks_tilesend:
title_thanks_pal:  .incbin "res/title_thanks.pal"
title_thanks_map:  .incbin "res/title_thanks.map"
title_gameover_tiles: .incbin "res/title_gameover.pic"   ; GAME OVER screen (death) overlay (BG0)
title_gameover_tilesend:
title_gameover_pal: .incbin "res/title_gameover.pal"
title_gameover_map: .incbin "res/title_gameover.map"
.ends

; Moon: a single 64x64 OBJ sprite (own palette), fixed in the sky, drawn behind the mountains.
.section ".rodata_moon" superfree
moontiles:      .incbin "res/moon.pic"
moontilesend:
moonpal:        .incbin "res/moon.pal"
moonpalend:
.ends

; --- Hero animations (CC0, adapted by tools/adapt_hero.py): 20 64x64 frames as one
;     strip, split into banks so the player can DMA the current frame per change. ---
.section ".rodata_heroA" superfree
hero_a:         .incbin "res/hero_a.bin"   ; frames 0..6  (each frame = one 4KB L|R band)
.ends
.section ".rodata_heroB" superfree
hero_b:         .incbin "res/hero_b.bin"   ; frames 7..13
.ends
.section ".rodata_heroC" superfree
hero_c:         .incbin "res/hero_c.bin"   ; frames 14..19
.ends
.section ".rodata_heropal" superfree
hero_pal:       .incbin "res/hero.pal"
hero_palend:
.ends

; --- Enemies (CC0, adapted by tools/adapt_enemy.py): demo-size 64x64 OBJ frames. Each frame is a 128-wide
;     band (skeleton in the LEFT 64x64, right 64 blank) so it's a CONTIGUOUS 4KB block enemy.c DMAs in two
;     2KB halves like the hero (reliable in a tight VBlank). 14 frames x 4KB = 56KB -> two <32KB banks.
;     Own 16-colour palette -> OBJ palette 2. ---
.section ".rodata_skel_a" superfree
skel_a:         .incbin "res/skel_a.bin"      ; 128-wide 4KB frame bands, frames 0..6
.ends
.section ".rodata_skel_b" superfree
skel_b:         .incbin "res/skel_b.bin"      ; frames 7..13
.ends
.section ".rodata_skel_c" superfree
skel_c:         .incbin "res/skel_c.bin"      ; frames 14..18 (shared death poof). 19 x 4KB = 76KB -> 3 banks
.ends
.section ".rodata_skelpal" superfree
skelpal:        .incbin "res/skeleton.pal"
skelpalend:
.ends
