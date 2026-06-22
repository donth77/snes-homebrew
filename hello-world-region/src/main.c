/*---------------------------------------------------------------------------------
    hello-world  —  Region-aware "Hello World"

    Goal: prove the whole pipeline (compile -> Mesen2 -> real hardware) AND bake
    in NTSC/PAL correctness from the very first line, so it's never bolted on later.

    What it does:
      * Detects the console refresh rate at RUNTIME, so ONE ROM is correct on both
        60 Hz (NTSC) and 50 Hz (PAL) consoles  -- a hard competition requirement.
      * Ticks a seconds counter at the SAME wall-clock rate on both regions, by
        counting `snes_fps` frames per second instead of hardcoding 60.

    API provenance:
      - snes_50hz, snes_fps, consoleDrawText, consoleInitText  -> include/snes/console.h
      - WaitForVBlank                                          -> include/snes/interrupt.h
      - setMode, bgSet*, setScreenOn, BG_MODE1, SC_32x32       -> include/snes/background.h, video.h

    VBlank model: we use the DEFAULT VBlank handler (the `NMI VBlank` vector in
    hdr.asm). It calls consoleVblank() internally, which flushes consoleDrawText()
    output to VRAM *during VBlank* -- so our text writes never happen mid-frame.
    (Only if you override the handler via nmiSet() must you call consoleVblank()
    yourself; see the note in interrupt.h. We don't, so we don't.)
---------------------------------------------------------------------------------*/
#include <snes.h>

// Font tiles + palette, declared in data.asm (.incbin of the build-generated
// pvsneslibfont.pic / .pal). We only need their addresses here.
extern char tilfont, palfont;

int main(void)
{
    u16 frames  = 0;   // VBlanks counted within the current second (0 .. snes_fps-1)
    u16 seconds = 0;   // wall-clock seconds since boot

    // --- Text console: tell PVSnesLib where the font lives in VRAM ---
    // (These four calls are verbatim from the stock hello_world example.)
    consoleSetTextMapPtr(0x6800);          // tilemap address in VRAM
    consoleSetTextGfxPtr(0x3000);          // font tile graphics address in VRAM
    consoleSetTextOffset(0x0100);          // first tile index the font occupies
    consoleInitText(0, 16 * 2, &tilfont, &palfont);

    // --- Background: Mode 1, only BG0 (our text layer) enabled ---
    bgSetGfxPtr(0, 0x2000);
    bgSetMapPtr(0, 0x6800, SC_32x32);
    setMode(BG_MODE1, 0);
    bgSetDisable(1);
    bgSetDisable(2);

    // --- Static labels (drawn once; flushed to VRAM on the next VBlank) ---
    consoleDrawText(7, 6, "REGION-AWARE HELLO");

    // Runtime region detection: snes_50hz == 1 on a PAL/50 Hz console.
    // THIS is what lets a single ROM be correct on both regions.
    if (snes_50hz)
        consoleDrawText(10, 10, "PAL  / 50 HZ");
    else
        consoleDrawText(10, 10, "NTSC / 60 HZ");

    setScreenOn();

    while (1)
    {
        // Each loop iteration is exactly one frame, because WaitForVBlank() below
        // blocks until the next VBlank. So counting `snes_fps` frames == 1 real
        // second, IDENTICALLY on NTSC (60) and PAL (50). Hardcoding 60 here would
        // make the counter run ~17% slow on PAL -- the bug this exists to avoid.
        frames++;
        if (frames >= snes_fps)
        {
            frames = 0;
            seconds++;
            // Trailing spaces overwrite stale digits as the number grows wider.
            consoleDrawText(10, 14, "SECONDS: %d   ", seconds);
        }

        WaitForVBlank();   // default NMI handler flushes queued text to VRAM here
    }
    return 0;
}
