/*---------------------------------------------------------------------------------
    main.c — entry point + game-state dispatcher.

    gothicvania is an original Gothicvania Cemetery homebrew (CC0 assets, LoROM, no
    special chips, single ROM on NTSC + PAL). Flow: TITLE -> PLAY ->
    END. Each state owns its VRAM,
    palettes and HDMA, runs its own loop, and returns the next state to run here.
        title.c  -> titleState()   play.c -> playState()   end.c -> endState()
    Shared world/player state: game.c (+ game.h). Renderer data + the shared night-sky
    scene: level.c. The hero: player.c. Per-tile collision data: level_collision.c.
---------------------------------------------------------------------------------*/
#include "game.h"

int main(void)
{
    GameState st = ST_TITLE, prev = ST_TITLE;

    // Boot the SNESMod sound engine and point it at our soundbank (title + in-game songs). Once, at
    // startup (spcBoot uploads the SPC700 driver and takes a moment). Each state then loads/plays/stops
    // its own track and calls spcProcess() per frame to stream it.
    spcBoot();
    spcSetBank((u8 *)&SOUNDBANK__);
    // (soundbank effects use spcLoadEffect/spcEffect -- NOT spcAllocateSoundRegion, which is for raw-BRR
    //  effects; the PVSnesLib effects examples don't call it, and calling it stopped our effects firing.)

#if PPU_CLEAN_INIT
    // The SNES powers on with GARBAGE in CGRAM and in the colour-math / subscreen / window registers,
    // and nothing clears them implicitly. Without this, palettes get blended with random colour math
    // (the colours glitched DIFFERENTLY on every real-hardware reload; the testrunner's clean power-on
    // state hid it). Force a known-clean colour pipeline ONCE before any state draws:
    {
        u16 i;
        for (i = 0; i < 256; i++) setPaletteColor(i, 0); // wipe all of CGRAM to black first
    }
    REG_CGWSEL  = 0x00;                                  // no colour-math windowing
    REG_CGADSUB = 0x00;                                  // colour math DISABLED (no add/sub blend)
    REG_TS      = 0x00;                                  // nothing designated on the subscreen
    REG_COLDATA = 0xE0;                                  // fixed sub-backdrop colour = black
    REG_W12SEL  = 0x00;                                  // no window masks on BG1/BG2 ...
    REG_W34SEL  = 0x00;                                  // ... BG3/BG4 ...
    REG_WOBJSEL = 0x00;                                  // ... or sprites/colour
    REG_TMW     = 0x00;                                  // windows hide NOTHING on the main screen
    REG_TSW     = 0x00;                                  // (garbage here would erase a layer in a band)
#endif

    for (;;) {
        respawn = (st == ST_PLAY && prev == ST_PLAY);   // PLAY->PLAY = pit respawn: keep music, skip reload
        prev = st;
        switch (st) {
            case ST_TITLE:    st = titleState();    break;
            case ST_PLAY:     st = playState();     break;
            case ST_END:      st = endState();      break;
            case ST_GAMEOVER: st = gameoverState(); break;
        }
    }
    return 0;
}
