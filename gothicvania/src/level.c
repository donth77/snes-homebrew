/*---------------------------------------------------------------------------------
    level.c — the level renderer's DATA layer + the reusable night-sky scene.

    Data: ROM addresses of each streamed 2KB page (ground/deco) and per-tile collision
    lookup (cellv). The page-streaming + camera logic itself runs inside the play loop
    (play.c), which calls these. Scene: the moon OBJ and the COLOR-MATH sky gradient
    are shared by BOTH the play scene and the title screen, so they live here as setup
    helpers (setupMoon / armSkyGradient) instead of being duplicated.
---------------------------------------------------------------------------------*/
#include "game.h"

// ROM address of streaming page p for each background (pages 0..14 in A, 15..18 in B).
u8 *groundPage(u16 p)
{
    if (p < PAGE_SPLIT) return (u8 *)(&groundpagesA) + (u32)p * 2048;
    return (u8 *)(&groundpagesB) + (u32)(p - PAGE_SPLIT) * 2048;
}
// --- STREAMED deco: window-local tilemap + per-page tiles (see build_stream.py) ---
// Window-local deco tilemap for page p (pages 0..14 in smapsA, 15..18 in smapsB). DMA to BG1_MAP + slot.
u8 *decoSmap(u16 p)
{
    if (p < PAGE_SPLIT) return (u8 *)(&deco_smapsA) + (u32)p * 2048;
    return (u8 *)(&deco_smapsB) + (u32)(p - PAGE_SPLIT) * 2048;
}
// ROM addr of page p's deco tiles. deco_pageinfo[p] = (u16 section, u16 byteOffset, u16 count); the tiles
// are bank-packed into deco_pt0..2 so a page's tile DMA never crosses a bank boundary.
u8 *decoPageTiles(u16 p)
{
    u16 *info = (u16 *)(&deco_pageinfo) + (u32)p * 3;
    u8 *base = (info[0] == 0) ? &deco_pt0 : (info[0] == 1) ? &deco_pt1 : &deco_pt2;
    return base + info[1];
}
u16 decoPageTileBytes(u16 p)
{
    return ((u16 *)(&deco_pageinfo))[(u32)p * 3 + 2] * 32;
}

// 0 = empty, 1 = full solid (walls + floor), 2 = one-way platform (lands from the top only,
// Off the sides/below = solid wall.
u8 cellv(s16 col, s16 row)
{
    if (col < 0 || col >= LVL_COLS || row >= LVL_ROWS) return 1;
    if (row < 0) return 0;
    return levelCollision[row * LVL_COLS + col];
}

// Moon + GLOW: a 128x64 sprite = two 64x64 OBJs, so the bright glow is SMOOTH (16-colour OBJ,
// not the blocky 4-colour BG). Fixed in the sky at OBJ priority 0 -- LOWEST -- so the mountains
// (BG2 prio 1) pass in front of it AND it's the first thing to drop if a scanline ever overruns
// 34 sprite-tiles (it never does: moon 16 + hero 16 = 32). Mountain BG tiles have transparent
// gaps so this smooth moon+glow shows through at pixel resolution -> no tile-stepped moon edge.
// prio 0 = behind the mountains (the play scene: the ridge passes in front of the moon).
// prio 1 = in front of the mountains (the title: a clean, stable moon disk while the mountains drift,
// since a drifting BG2 silhouette would otherwise occlude/shimmer the small moon sprite).
void setupMoon(u8 prio)
{
    // Set the OBJ base + size mode (small 32 / large 64). The play scene gets this from setupHero's
    // oamInitGfxSet, but the title has no hero -- without it the moon's two OBJ_LARGE sprites render at
    // the wrong size (only a 32x32 corner showed -> a "nub"). Self-contained so any caller is correct.
    oamInitGfxAttr(HERO_VRAM, OBJ_SIZE32_L64);
    dmaCopyVram(&moontiles, MOON_VRAM, (&moontilesend - &moontiles));
    setPalette(&moonpal, 128 + 16, (&moonpalend - &moonpal));
    oamSet(8, 96, 28, prio, 0, 0, 128, 1);           // moon+glow, ONE 64x64 OBJ (was two: 128x64). The
    oamSetEx(8, OBJ_LARGE, OBJ_SHOW);                 // freed 2KB (tiles 192..255, 0x0C00) is the enemy band.
}

// Smooth night-sky gradient via COLOR MATH on HDMA ch6. The BG2 sky is transparent, so the
// BACKDROP (CGRAM 0) shows through and ch6 ADDs a per-scanline fixed colour to it on $2132
// (COLDATA) -- NEVER CGRAM, so it can't scramble the palette. setModeHdmaColor only arms ch6 from
// the table (handling its ROM bank); we then override the destination + transfer mode to COLDATA.
// CAUTION: setParallaxScrolling() clobbers ch6's table-bank pointer, so in the play scene this MUST
// be called AFTER setParallaxScrolling (else the sky reads the wrong bank and tints green).
void armSkyGradient(void)
{
    setModeHdmaColor((u8 *) &sky_coldata);    // arms HDMA ch6 from the table (handles the table's ROM bank)
    REG_DMAP6   = 0x02;                        // OVERRIDE: transfer mode 2 = write the destination twice/line
    REG_BBAD6   = 0x32;                        // OVERRIDE: destination = $2132 (COLDATA), NOT $2122 (CGDATA)
    REG_CGWSEL  = 0x00;                        // colour math always on; use the FIXED colour (not the subscreen)
    REG_CGADSUB = 0x20;                        // ADD the fixed colour to the BACKDROP only (bit5); BGs/OBJ untouched
    REG_COLDATA = 0xE0;                        // init fixed colour = black; HDMA then drives red+blue per scanline
    setPaletteColor(0, SKY_BACKDROP);          // backdrop = the dark top-of-sky colour (the gradient's floor)
}
