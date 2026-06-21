/*---------------------------------------------------------------------------------
    title.c — the TITLE state (the demo's TitleScreen).

    Moon + slowly-drifting mountains + the COLOR-MATH sky gradient, with the
    "GothicVania" logo (BG0) and a blinking "PRESS START" (BG1) over it. No level/
    ground, no credits, no instructions screen. START -> PLAY. The moon and sky
    gradient are the same shared scene helpers the play state uses (level.c).
---------------------------------------------------------------------------------*/
#include "game.h"

GameState titleState(void)
{
    u16 mtn = 0, prevPad = padsCurrent(0);
    u8  blink = 0, pressOn = 1;

    setScreenOff();
    setMode(BG_MODE1, 0);

    // BG0 = logo (palette block 1), BG1 = PRESS START (block 2): 4bpp overlays. BG2 = the play scene's
    // mountains tiles, shown over the same COLOR-MATH sky gradient.
    bgInitTileSet(0, &title_logo_tiles,  &title_logo_pal,  1,
                  (&title_logo_tilesend  - &title_logo_tiles),  32, BG_16COLORS, TITLE_LOGO_TILES);
    bgInitTileSet(1, &title_press_tiles, &title_press_pal, 2,
                  (&title_press_tilesend - &title_press_tiles), 32, BG_16COLORS, TITLE_PRESS_TILES);
    bgInitTileSet(2, &parallaxtiles, &parallaxpal, 0,
                  (&parallaxtilesend - &parallaxtiles), (&parallaxpalend - &parallaxpal),
                  BG_4COLORS, BG2_TILES);
    bgSetMapPtr(0, TITLE_LOGO_MAP,  SC_32x32);
    bgSetMapPtr(1, TITLE_PRESS_MAP, SC_32x32);
    bgSetMapPtr(2, BG2_MAP,         SC_64x32);

    WaitForVBlank();
    dmaCopyVram(&title_logo_map,        TITLE_LOGO_MAP,  2048);
    dmaCopyVram(&title_press_map,       TITLE_PRESS_MAP, 2048);
    dmaCopyVram(&parallaxmap,           BG2_MAP,         2048);
    dmaCopyVram((&parallaxmap) + 2048,  BG2_MAP + 0x400, 2048);

    // Moon (OBJ) over the sky -- same as the play scene; clear all other sprites first (e.g. a hero left
    // over from a previous playthrough on the END -> TITLE loop).
    oamClear(0, 128);
    setupMoon(1);                                // moon IN FRONT of the drifting mountains -> stable + clear

    bgSetEnable(0); bgSetEnable(1); bgSetEnable(2);
    bgSetScroll(0, 0, 0); bgSetScroll(1, 0, 0); bgSetScroll(2, 0, 0);

    // Sky gradient (HDMA ch6) -- same COLOR-MATH backdrop ramp as play. No parallax banding (ch3): the
    // title scrolls the mountains as ONE slow layer (the demo's bg-mountains drift), via bgSetScroll(2).
    armSkyGradient();

    setScreenOn();

    for (;;) {
        u16 pad = padsCurrent(0);
        mtn += 51;                                  // ~0.2 px/frame drift (8.8 fixed point), like the demo
        if (++blink >= 42) { blink = 0; pressOn ^= 1; }   // blink PRESS START (~0.7s, the demo's timer)
        if ((pad & KEY_START) && !(prevPad & KEY_START)) return ST_PLAY;
        prevPad = pad;

        WaitForVBlank();
        bgSetScroll(2, mtn >> 8, 0);                 // scroll write in VBlank (shared scroll-latch safe)
        if (pressOn) bgSetEnable(1); else bgSetDisable(1);
        REG_HDMAEN = 0x40;                           // ch6 (sky gradient) only -- no parallax banding
    }
}
