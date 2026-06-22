/*---------------------------------------------------------------------------------
    gameover.c — the GAME OVER state, shown when the hero takes 3 contact hits.

    The demo has no health; enemies just knock you back, which can loop forever if you're
    cornered. So this build gives the hero 3 HP (no on-screen UI) -- the 3rd hit ends the
    run here. A single "GAME OVER" overlay (BG0) on a dark backdrop; START -> title.
    Structurally identical to end.c (the "THANKS FOR PLAYING" screen), just a red overlay.
---------------------------------------------------------------------------------*/
#include "game.h"

GameState gameoverState(void)
{
    u16 prevPad = padsCurrent(0), t = 0;

    setScreenOff();
    setMode(BG_MODE1, 0);

    bgInitTileSet(0, &title_gameover_tiles, &title_gameover_pal, 1,
                  (&title_gameover_tilesend - &title_gameover_tiles), 32, BG_16COLORS, TITLE_LOGO_TILES);
    bgSetMapPtr(0, TITLE_LOGO_MAP, SC_32x32);
    WaitForVBlank();
    dmaCopyVram(&title_gameover_map, TITLE_LOGO_MAP, 2048);

    oamClear(0, 128);                                // no sprites on the game-over screen
    bgSetEnable(0); bgSetDisable(1); bgSetDisable(2);
    bgSetScroll(0, 0, 0);

    REG_HDMAEN  = 0x00;                              // kill the sky-gradient HDMA -> a flat dark backdrop
    REG_CGADSUB = 0x00;                              // colour math off
    setPaletteColor(0, SKY_BACKDROP);                // dark backdrop behind the text

    spcStop();                                       // stop the in-game music on death
    setScreenOn();

    for (;;) {
        u16 pad = padsCurrent(0);
        t++;
        // brief lock-out so the hit that killed you doesn't instantly skip the screen
        if (t > 40 && (pad & KEY_START) && !(prevPad & KEY_START)) return ST_TITLE;
        prevPad = pad;
        spcProcess();                                // keep the SPC serviced (stopped -> silent)
        WaitForVBlank();
    }
}
