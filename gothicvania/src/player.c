/*---------------------------------------------------------------------------------
    player.c — the hero sprite: ROM frame addressing + OBJ-slot setup.

    The hero's per-frame physics, collision, animation selection and OAM positioning
    run inline in the play loop (play.c), which is where they're tightly coupled to
    the camera/level. This file owns the hero's DATA: where each animation frame lives
    in ROM (heroFrameSrc) and the one-time sprite-slot init (setupHero).
---------------------------------------------------------------------------------*/
#include "game.h"

// Address in ROM of hero frame f's 4KB band (its L+R 64x64 halves), split into three banks.
u8 *heroFrameSrc(u8 f)
{
    if (f < HBANK)     return (u8 *)(&hero_a) + (u32)f * HERO_BANDSZ;
    if (f < 2 * HBANK) return (u8 *)(&hero_b) + (u32)(f - HBANK) * HERO_BANDSZ;
    return (u8 *)(&hero_c) + (u32)(f - 2 * HBANK) * HERO_BANDSZ;
}

// Load the hero palette + size + frame 0 into the sprite slot; later frames DMA over it.
// Two 64x64 OBJs make the full 128x64 hero: OBJ0 = left half, OBJ4 = right half.
void setupHero(void)
{
    // Palette size is the literal 32 (a 16-colour OBJ palette), NOT (&hero_palend - &hero_pal): with the ROM
    // fuller now, that tiny .rodata_heropal section lands at a bank tail, so hero_palend resolves to offset
    // $10000 and `lda.w #hero_palend` overflows 16 bits at assemble time. The literal avoids referencing it.
    oamInitGfxSet(heroFrameSrc(0), HERO_BANDSZ, &hero_pal, 32, 0, HERO_VRAM, OBJ_SIZE32_L64);
    oamSetEx(HERO_OAM,     OBJ_LARGE, OBJ_SHOW);      // left half
    oamSetEx(HERO_OAM + 4, OBJ_LARGE, OBJ_SHOW);      // right half
}
