# Build Slice 2 — Title Screen + Music + Animated Moving Sprite

**Goal:** a real (tiny) game scene — a full-screen **title/background**, **one
music track** looping, and **one character sprite** that **animates** (cycles
frames) and **moves** over the background under D-pad control, all
**region-correct**.

This is the slice that exercises the four systems you'll lean on constantly:
backgrounds, sprites + animation, the SPC700 sound engine, and input — plus the
VRAM/CGRAM/ARAM budgeting that trips everyone up.

**Your single best reference for this slice** is the official
`graphics/Backgrounds/Mode1ContinuosScroll` example in `snes-examples/` — it
literally demonstrates a sprite moving over a Mode 1 background. Read it
alongside this guide; the code below uses the same verified API.

> API signatures here are from `sprite.h`, `background.h`, and the Sounds &
> Musics wiki. Two notes: (1) `oamInitGfxSet` gained a `paletteSize` parameter
> in a recent version — the 7-arg form below is current; older tutorials show 6.
> (2) Confirm `bgInitMapSet`'s argument order against the Mode1 example.

---

## 0. Prerequisites

- Slice 1 builds, runs, and is region-correct. This slice extends it.
- You can run `gfx4snes` and `smconv` (they ship with PVSnesLib).

---

## 1. Plan your VRAM, palettes, and ARAM up front

SNES dev punishes ad-hoc memory layout. Decide addresses **before** coding so BG
tiles, BG map, sprite tiles, and the font don't overlap. A clean Mode 1 layout:

| Region            | VRAM word addr | Notes                          |
|-------------------|----------------|--------------------------------|
| BG1 tiles (bg gfx)| `0x0000`       | title screen tileset           |
| BG1 tilemap       | `0x4000`       | the screen layout              |
| Sprite tiles      | `0x6000`       | character frames               |
| (font, if kept)   | separate       | only if you still draw text    |

Palettes (CGRAM): BG palettes 0–7, sprite palettes 0–7 (sprite palette N maps to
CGRAM 128 + N*16). Keep each ≤16 colors, **transparency = color 0** (magenta
255,0,255 convention).

ARAM (audio, 64 KB total): driver + your samples + the song must fit. `smconv`
prints the byte total and free space — watch it.

---

## 2. Prepare assets (original work only)

**Background (title screen)**
- Draw an indexed image (≤16 colors) sized to the screen, e.g. 256×224.
- Convert with `gfx4snes` to produce tiles `.pic`, palette `.pal`, map `.map`.

**Character sprite**
- Draw a sheet of equal frames stacked **vertically**, e.g. four 16×16 frames
  (16×64 total), ≤16 colors, transparency first.
- Convert with `gfx4snes` (size 16). It emits the sprite `.pic` + `.pal`.

**Music**
- Compose a short loop in OpenMPT (or Schism Tracker) as an `.it`:
  - **≤8 channels**, **instrument mode**, no note exceeding 128 kHz playback.
  - **Turn OFF IT sample compression** (OpenMPT advanced setting `ITCompression`)
    — `smconv` can't read it.
  - Keep samples small / low sample rate; all samples must fit ~58 KB after BRR.

---

## 3. `data.asm` — declare the assets

```asm
.section ".rodata_bg" superfree
bg_tiles:   .incbin "res/title.pic"
bg_tiles_end:
bg_map:     .incbin "res/title.map"
bg_map_end:
bg_pal:     .incbin "res/title.pal"
bg_pal_end:
.ends

.section ".rodata_spr" superfree
chr_tiles:  .incbin "res/hero.pic"
chr_tiles_end:
chr_pal:    .incbin "res/hero.pal"
chr_pal_end:
.ends
```

Each section < 32 KB. The soundbank is included via the build rules (next step),
not hand-incbin'd here.

---

## 4. Makefile additions

Graphics conversion rules (adapt sizes to your art):

```make
# Background: 16-color tiles + map + palette.
# NOTE: no -R here. -R disables tile reduction (use it only for fonts/HUDs that
# need every tile kept). For a normal background you WANT reduction to save VRAM.
res/title.pic: res/title.png
	gfx4snes -s 8 -o 16 -u 16 -m -t png -i $<

# Sprite sheet: 16x16, one 16-color palette
res/hero.pic: res/hero.png
	gfx4snes -s 16 -o 16 -u 16 -t png -i $<
```

Music (before the `snes_rules` include):

```make
AUDIOFILES   := res/song.it
export SOUNDBANK := res/soundbank
SMCONVFLAGS  := -s -o $(SOUNDBANK) -V -b 5    # music/sfx start at bank 5
musics: $(SOUNDBANK).obj
all: musics $(ROMNAME).sfc
```

> Two things people get wrong here. (1) **Build ordering:** the converted
> `.pic`/`.map`/`.pal` must exist *before* `data.asm` is assembled, or the
> `.incbin` fails / uses a stale file. Make the ROM depend on the graphics —
> e.g. add a `graphics: res/title.pic res/hero.pic` target and list it as a
> prerequisite (like `musics`) so `make` converts first. (2) **Flags vary by
> asset** — confirm `gfx4snes` options against the Sprites / Backgrounds wiki;
> wrong color/size flags give corrupted tiles, the classic "fine in emulator,
> broken on hardware" bug.

---

## 5. `src/main.c`

```c
#include <snes.h>

// --- assets from data.asm ---
extern char bg_tiles, bg_tiles_end, bg_map, bg_map_end, bg_pal, bg_pal_end;
extern char chr_tiles, chr_tiles_end, chr_pal, chr_pal_end;

// --- soundbank (generated by smconv into soundbank.asm) ---
extern char SOUNDBANK__0, SOUNDBANK__1;
// MOD_SONG comes from the generated soundbank.h — include or match its name.

// VRAM layout from step 1
#define BG1_TILES  0x0000
#define BG1_MAP    0x4000
#define SPR_TILES  0x6000

// Per-frame gfx-offset stride for the sprite animation. VERIFY this against
// your sheet/example before trusting it (see note in the loop). Placeholder:
#define FRAME_STRIDE 4

// Sprite state (8.8 fixed point for smooth, region-correct motion)
s16 hero_x = 100 << 8;
s16 hero_y =  90 << 8;
u16 anim_timer = 0;
u8  anim_frame = 0;

int main(void) {
    consoleInit();

    // --- Audio: boot SPC700, register banks (REVERSE order), load + play ---
    spcBoot();
    spcSetBank(&SOUNDBANK__1);
    spcSetBank(&SOUNDBANK__0);
    spcLoad(MOD_SONG);     // name from generated soundbank.h
    spcPlay(0);

    // --- Background: load tileset + map into VRAM, then enable ---
    bgInitTileSet(0, &bg_tiles, &bg_pal, 0,
                  (&bg_tiles_end - &bg_tiles),
                  (&bg_pal_end - &bg_pal),
                  BG_16COLORS, BG1_TILES);   // confirm constant name (BG_16COLORS
                                             // vs BG_4COLORS/BG_256COLORS) in background.h
    bgInitMapSet(0, &bg_map, (&bg_map_end - &bg_map), SC_32x32, BG1_MAP);
    // ^ confirm bgInitMapSet arg order/constants against the Mode1 example.

    // --- Sprite: load gfx (7-arg current signature) ---
    oamInitGfxSet(&chr_tiles, (&chr_tiles_end - &chr_tiles),
                  &chr_pal,   (&chr_pal_end - &chr_pal),
                  0, SPR_TILES, OBJ_SIZE16_L32);

    // sprite id 0 (ids must be multiples of 4)
    oamSet(0, hero_x >> 8, hero_y >> 8, 2, 0, 0, 0, 0);
    oamSetEx(0, OBJ_SMALL, OBJ_SHOW);

    setMode(BG_MODE1, 0);
    bgSetDisable(1);
    bgSetDisable(2);
    setScreenOn();

    // Region-correct speeds: tune in "subpixels per frame".
    // Slower per-frame on 60Hz, faster on 50Hz => same wall-clock speed.
    s16 speed       = snes_50hz ? 180 : 150;   // 8.8 subpixels/frame
    u8  anim_period = snes_50hz ? 8   : 10;     // frames per animation step

    while (1) {
        scanPads();
        u16 pad = padsCurrent(0);

        if (pad & KEY_LEFT)  hero_x -= speed;
        if (pad & KEY_RIGHT) hero_x += speed;
        if (pad & KEY_UP)    hero_y -= speed;
        if (pad & KEY_DOWN)  hero_y += speed;

        // Animate: advance frame every anim_period frames.
        if (++anim_timer >= anim_period) {
            anim_timer = 0;
            anim_frame = (anim_frame + 1) & 3;          // 4 frames
            // Animate by pointing the sprite at the next frame's tiles via its
            // gfx offset. The STRIDE between frames depends on sprite size and
            // how gfx4snes laid out the sheet — it is NOT always 4. Verify the
            // real per-frame stride from the SimpleSprite / Mode1 example (or by
            // inspecting VRAM in Mesen2), then set FRAME_STRIDE accordingly.
            oamSetGfxOffset(0, anim_frame * FRAME_STRIDE);
        }

        oamSetXY(0, hero_x >> 8, hero_y >> 8);

        spcProcess();      // MUST run every frame or music stops
        WaitForVBlank();   // OAM/VRAM commit happens here
    }
    return 0;
}
```

---

## 6. Why each region tweak matters

- **Movement** uses 8.8 fixed point and a per-region `speed` so the hero crosses
  the screen in the same number of seconds on NTSC and PAL. (Alternative: a
  fixed-timestep accumulator — cleaner once you have many moving things.)
- **Animation** cadence scales the same way, so the walk cycle looks the same
  speed on both.
- Everything still advances **once per `WaitForVBlank()`**, so OAM updates land
  during VBlank — never mid-frame.

---

## 7. Build, run, test

```
make
```

In Mesen2:
1. Title background fills the screen, hero sprite visible and animating.
2. D-pad moves the hero smoothly; music loops without stutter.
3. **Region test:** force NTSC, then PAL. Movement speed and walk-cycle speed
   should look identical; music should still play (tempo handled by the driver).
4. Event Viewer: no VRAM writes outside VBlank; no sprite tile corruption.

Then flash to the FXPak Pro / EverDrive and confirm on real hardware — sprite
animation and audio are the two things most likely to behave differently on
silicon than in an emulator.

---

## 8. Definition of done

- [ ] Background renders correctly (no corrupt/garbled tiles on hardware).
- [ ] Sprite animates and moves; ID is a multiple of 4; updates only in VBlank.
- [ ] Music loops cleanly; `spcProcess()` called every frame.
- [ ] ARAM within budget (smconv reported free space ≥ 0).
- [ ] VRAM regions (BG tiles/map, sprite tiles) don't overlap.
- [ ] Movement + animation equal wall-clock speed on NTSC and PAL.
- [ ] ROM still LoROM, ≤512 KB, no SRAM, no special chips.
- [ ] Boots and plays on real hardware.

---

## 9. Common pitfalls

- **Silent music** → forgot `spcProcess()` in the loop, or registered banks in
  the wrong order (must be **reverse**: `__1` then `__0`).
- **"Module too big"** → ARAM overflow; shrink samples / sample rate, or turn
  off IT compression (smconv can't read compressed samples).
- **Corrupt sprite/BG tiles only on hardware** → overlapping VRAM addresses, or
  wrong `gfx4snes` color/size flags.
- **Sprite invisible** → wrong palette slot, OAM id not a multiple of 4, or
  `oamSetEx` size/show not set.
- **Animation garbled** → wrong `gfxoffset` stride; verify tiles-per-frame from
  a working example before trusting the multiplier.
- **PAL runs slow** → some per-frame movement/timer not scaled by region.
