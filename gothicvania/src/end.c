/*---------------------------------------------------------------------------------
    end.c — the END state: "THANKS FOR PLAYING".

    Reached when the hero walks off the right edge of the level. A single overlay (BG0)
    on a plain dark backdrop (sky gradient + sprites off). START returns to the title.
---------------------------------------------------------------------------------*/
#include "game.h"

GameState endState(void)
{
    u16 prevPad = padsCurrent(0), t = 0;

    setScreenOff();
    setMode(BG_MODE1, 0);

    bgInitTileSet(0, &title_thanks_tiles, &title_thanks_pal, 1,
                  (&title_thanks_tilesend - &title_thanks_tiles), 32, BG_16COLORS, TITLE_LOGO_TILES);
    bgSetMapPtr(0, TITLE_LOGO_MAP, SC_32x32);
    WaitForVBlank();
    dmaCopyVram(&title_thanks_map, TITLE_LOGO_MAP, 2048);

    oamClear(0, 128);                                // no sprites on the end screen
    bgSetEnable(0); bgSetDisable(1); bgSetDisable(2);
    bgSetScroll(0, 0, 0);

    REG_HDMAEN  = 0x00;                              // kill the sky-gradient HDMA -> a flat dark backdrop
    REG_CGADSUB = 0x00;                              // colour math off
    setPaletteColor(0, SKY_BACKDROP);                // dark backdrop behind the text

    spcStop();                                       // STOP the looping in-game music on the end screen
    setScreenOn();

    for (;;) {
        u16 pad = padsCurrent(0);
        t++;
        // Ignore START briefly so the press that *reached* the end doesn't instantly skip the screen.
        if (t > 40 && (pad & KEY_START) && !(prevPad & KEY_START)) return ST_TITLE;
        prevPad = pad;
        spcProcess();                                // keep the SPC serviced (stopped -> silent)
        WaitForVBlank();
    }
}
