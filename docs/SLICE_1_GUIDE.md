# Build Slice 1 — Region-Aware "Hello World"

**Goal:** prove your entire pipeline end to end (compile → Mesen2 → real
hardware) and bake in NTSC/PAL handling from the very first line of code, so
region-correctness is never bolted on later.

**What you'll see:** a screen that prints whether it's running on a 60 Hz
(NTSC) or 50 Hz (PAL) console, plus a seconds counter that ticks at the same
real-world rate on both — your proof that timing is region-correct.

All API calls below were taken from the PVSnesLib headers / official examples.
If anything doesn't compile, the library version differs — grep the real header
in `$PVSNESLIB_HOME/pvsneslib/include/snes/` before changing the call.

---

## 0. Prerequisites

- PVSnesLib installed, `$PVSNESLIB_HOME` set, `make` on PATH.
- Mesen2 installed (your debug emulator).
- Confirm the toolchain works first: build the stock `hello_world` example that
  ships in `snes-examples/`. If that produces a `.sfc` that runs, you're good.

---

## 1. Project skeleton

```
slice1/
├── Makefile
├── hdr.asm          ; ROM header (constraints live here)
├── data.asm         ; asset includes (just the font for now)
└── src/
    └── main.c
```

---

## 2. The ROM header — your constraints, encoded

`hdr.asm`. This is where LoROM / no-SRAM / no-special-chip compliance is
literally declared.

> **Don't copy this block verbatim.** The header/vector DSL differs slightly
> between PVSnesLib versions. Start from the `hdr.asm` in your installed
> `hello_world` (or `template`) example and edit **only** the `.SNESHEADER`
> fields below. The surrounding `.MEMORYMAP` / vector blocks shown here are
> illustrative — use your version's actual ones.

```asm
.MEMORYMAP
  SLOTSIZE $8000
  DEFAULTSLOT 0
  SLOT 0 $8000
.ENDME

.ROMBANKSIZE $8000        ; 32 KB banks (standard LoROM)
.ROMBANKS 8               ; 8 x 32KB = 256 KB. Raise later, keep <= 512 KB (16 banks)

.SNESHEADER
  ID "SNES"
  NAME "SLICE1 REGION TEST   "  ; must be EXACTLY 21 chars (pad with spaces)
  SLOWROM
  LOROM
  CARTRIDGETYPE $00        ; ROM only — no SRAM, no coprocessor
  ROMSIZE $08              ; 2 Mbit (256 KB)
  SRAMSIZE $00             ; no save RAM
  COUNTRY $01              ; sets emulator default region only; runtime detect handles both
  LICENSEECODE $00
  VERSION $00
.ENDME

.SNESNATIVEVECTOR
  COP EmptyHandler
  BRK EmptyHandler
  ABORT EmptyHandler
  NMI VBlank
  IRQ EmptyHandler
.ENDNATIVEVECTOR

.SNESEMUVECTOR
  COP EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  RESET main
  IRQBRK EmptyHandler
.ENDEMUVECTOR
```

> The vector block is usually provided by PVSnesLib's template `hdr.asm` — copy
> the one from the `hello_world` example and only edit the `.SNESHEADER` fields.
> Don't hand-roll vectors unless you know why.

---

## 3. Asset includes

`data.asm` — for slice 1, just the bundled font:

```asm
.section ".rodata1" superfree
tilfont: .incbin "pvsneslibfont.pic"
palfont: .incbin "pvsneslibfont.pal"
.ends
```

Copy `pvsneslibfont.pic` / `.pal` from the `hello_world` example.
**Each section must stay under 32 KB** — split into more named sections later.

---

## 4. `src/main.c`

```c
#include <snes.h>

extern char tilfont, palfont;   // font assets declared in data.asm

int main(void) {
    u16 frames  = 0;
    u16 seconds = 0;

    // --- Text console (verbatim from PVSnesLib hello world) ---
    consoleSetTextMapPtr(0x6800);
    consoleSetTextGfxPtr(0x3000);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &tilfont, &palfont);

    // --- Mode 1, only BG0 on for text ---
    bgSetGfxPtr(0, 0x2000);
    bgSetMapPtr(0, 0x6800, SC_32x32);
    setMode(BG_MODE1, 0);
    bgSetDisable(1);
    bgSetDisable(2);

    consoleDrawText(8, 6, "REGION-AWARE HELLO");

    // snes_50hz == 1 on a PAL/50Hz console; snes_fps is 50 or 60.
    if (snes_50hz)
        consoleDrawText(10, 10, "PAL  / 50 HZ");
    else
        consoleDrawText(10, 10, "NTSC / 60 HZ");

    setScreenOn();

    while (1) {
        // Tick one "second" using the detected refresh rate.
        // Because we divide by snes_fps, this counts real time
        // IDENTICALLY on NTSC and PAL — the whole point of slice 1.
        frames++;
        if (frames >= snes_fps) {
            frames = 0;
            seconds++;
            // consoleDrawText takes printf-style format args (see console.h /
            // the "Output Text Screen" wiki). Trailing spaces clear old digits.
            // If your version's consoleDrawText has no format args, convert the
            // number to a string with an itoa helper and draw that instead.
            consoleDrawText(10, 14, "SECONDS: %d   ", seconds);
        }

        WaitForVBlank();
    }
    return 0;
}
```

**The lesson baked in:** if you had instead moved something "1 pixel per frame",
it would crawl 17% slower on PAL. Dividing by `snes_fps` (or, later, a
fixed-timestep accumulator) is how you stay correct on both.

---

## 5. Makefile

Start from the `hello_world` Makefile and set the ROM name. The key line is the
include of PVSnesLib's build rules:

```make
ROMNAME := slice1
include ${PVSNESLIB_HOME}/devkitsnes/snes_rules
```

(That ruleset already wires up `816-tcc`, WLA-DX, the header, and asset tools.)

---

## 6. Build

```
make
```

Expect a wall of output (compiler + assembler stages) ending with `slice1.sfc`.
Errors show up in that stream — read from the **first** error down.

---

## 7. Run & region-test in Mesen2

1. Open `slice1.sfc`. You should see the title, the region line, and the
   counter.
2. Mesen2 lets you force console region (Settings → Emulation, or the region
   override). **Run it once as NTSC and once as PAL.**
3. Confirm: the region line changes, **and** the seconds counter advances at the
   same wall-clock rate in both. If PAL counts slower, your timing isn't using
   `snes_fps` correctly.
4. Use the **Event Viewer** to confirm there are no VRAM writes outside VBlank.

---

## 8. Flash to hardware

Copy `slice1.sfc` to your flashcart (FXPak Pro / Super EverDrive) and boot it on
a real SNES. If you have access to only one region, that's fine for now — the
emulator covered the other. Confirm it boots and displays correctly (no garbage
tiles, no black band cropping the text).

---

## 9. Definition of done

- [ ] `make` is clean, produces `slice1.sfc`.
- [ ] ROM is LoROM, ≤512 KB, `CARTRIDGETYPE $00`, `SRAMSIZE $00`.
- [ ] Region line correct under both Mesen2 region settings.
- [ ] Seconds counter ticks at equal real-world rate on NTSC and PAL.
- [ ] No illegal mid-frame VRAM writes (Event Viewer clean).
- [ ] Boots on real hardware.

---

## 10. Common first-slice pitfalls

- **Section > 32 KB** → WLA-DX error. Split assets into more `.section` blocks.
- **Garbage tiles on hardware but fine in emulator** → uninitialized VRAM/RAM;
  PVSnesLib's init normally clears it, but don't assume memory starts at zero.
- **Wrong header field name** → the header DSL varies by version; match the
  current "SNES ROM Header" wiki page exactly.
- **Editing the interrupt vectors by hand** → don't; reuse the template's.
