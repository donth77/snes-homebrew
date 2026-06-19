# CLAUDE.md — SNES Homebrew Project (PVSnesLib / C)

## What this project is

An **original SNES game written in C using PVSnesLib**, targeting a homebrew
competition with hard technical constraints (below). I'm an experienced
full-stack engineer but **new to SNES, 65816, and retro-console hardware**.
Assume I know C and software architecture well, but not the hardware. Teach the
hardware as we go.

---

## Project docs — read these first

- **`RESOURCES.md`** — the curated link list: toolchain, wiki pages, hardware
  references, asset tools, open-source games to study, community, and the
  explicit *do-not-copy* list. Use it instead of guessing where to look.
- **`SLICE_1_GUIDE.md`** — step-by-step first build: a region-aware hello-world
  that proves the full pipeline (compile → Mesen2 → hardware). Start here.
- **`SLICE_2_GUIDE.md`** — step-by-step advanced slice: title screen + one music
  track + an animated, moving character sprite on a background, region-correct.

When a task maps to one of these, follow that doc's steps and conventions rather
than improvising. If a guide and an installed header disagree, the **header
wins** (the guides are version-pinned to when they were written — see below).

---

## Hard constraints — NEVER violate these

These are competition rules. Breaking any one disqualifies the entry.

1. **LoROM mapping only.** No HiROM, no ExHiROM.
2. **ROM ≤ 512 KB.**
3. **No special / enhancement chips** — no SA-1, Super FX (GSU), DSP-1, CX4,
   S-DD1, OBC-1, SPC7110, etc.
4. **No SRAM / battery save.** Header must be `CARTRIDGETYPE $00`,
   `SRAMSIZE $00`. (Note: this means no *cartridge save RAM*. The console's
   128 KB WRAM is fine and unrelated.)
5. **Single ROM that runs on NTSC (60 Hz) AND PAL (50 Hz)** via *runtime*
   region detection — not two builds.
6. **Must run on real hardware**, not just emulators.
7. **100% original work.** No code, graphics, music, or data copied from
   commercial ROMs, disassemblies, decompilations, or leaked source. Free /
   CC0 / permissively-licensed assets are allowed *only* if I've confirmed the
   license.

**If a request or your proposed approach would break any of these, stop and
flag it before writing code.**

---

## Toolchain — don't substitute without asking

- **Language:** C (PVSnesLib). 65816 assembly only where genuinely needed.
- **Compiler / linker:** 816-tcc + WLA-DX, driven by PVSnesLib's `snes_rules`
  makefiles. `$PVSNESLIB_HOME` is set.
- **Graphics:** `gfx4snes`. **Audio:** `smconv` (Impulse Tracker `.it`),
  `snesbrr` (wav → brr).
- **Dev / debug emulator:** Mesen2 (accurate, strong debugger). Cross-check on
  ares / bsnes. Never treat ZSNES or Snes9x as ground truth.
- **Hardware test:** FXPak Pro or Super Everdrive flashcart.

---

## How you (Claude Code) must work in this repo

### 1. Accuracy over fluency — this is a low-resource domain
- PVSnesLib is niche; your training data on it is thin and partly outdated.
- **Do not invent** function names, signatures, struct fields, or header
  directives. Before using any PVSnesLib API, **verify it against the real
  headers** (under `$PVSNESLIB_HOME/.../include/snes/` — the exact prefix is
  `include/snes` or `pvsneslib/include/snes` depending on the install) **and
  against `snes-examples/`.** Grep first, then write.
- When you introduce an API I haven't used before, name the header or example
  you took it from. If you can't find it, say so — don't guess.
- **Pin the version.** Record the installed PVSnesLib version/commit in the repo
  README. The API surface drifts between releases (e.g. `oamInitGfxSet` gained a
  `paletteSize` argument), so an old tutorial may not match. When in doubt,
  trust the installed header over any doc, guide, or memory.

### 2. You can't see the screen — I'm your eyes
- You cannot run the ROM or observe rendering, timing glitches, or hardware
  crashes. Mesen2 output and my reports are the ground truth.
- After any change affecting rendering or timing, tell me **exactly what to
  look for** (e.g. "player sprite top-left, no flicker, BG scrolls smoothly")
  and, where possible, a **debugger check** (memory watch, breakpoint, or
  Mesen2's event viewer for illegal mid-frame VRAM writes).
- For visual/timing bugs, propose hypotheses + diagnostics, not just edits.

### 3. C first; assembly only when justified
- Default to C. Write 65816 asm only when I ask, or when a *profiled* hot path
  needs it.
- Flag all asm as needing verification — never assert it's correct untested.
  Explicitly track and comment the M/X (8/16-bit accumulator/index) flags and
  any bank boundaries.

### 4. Respect the hardware rules in code
- **VRAM / CGRAM / OAM writes only during VBlank or forced blank.** Use
  PVSnesLib's `WaitForVBlank()` / NMI conventions; never push graphics
  mid-frame.
- **Never hardcode 60 Hz timing.** Detect region with PVSnesLib's globals:
  `snes_50hz` (1 on a PAL/50 Hz console) and `snes_fps` (50 or 60). There's also
  `snes_vblank_count`. Either run game logic on a fixed timestep or scale
  movement / timers / animation cadence per region so wall-clock speed matches
  on both. (Underneath, the flag comes from PPU `$213F`/STAT78 bit 4, but use
  the library globals, not the raw register.)
- **PAL has more visible scanlines** than NTSC — don't assume a 224-line
  layout; center the playfield so PAL doesn't show a black band.
- **Initialize RAM explicitly.** Real hardware boots with garbage RAM;
  emulators often zero it and hide the bug.

### 5. Stay inside the budgets — warn early
- ROM ≤ **512 KB**. VRAM **64 KB**. SPC700 ARAM is **64 KB total**, but the
  driver takes a chunk — realistically **~58–60 KB** for your music + SFX
  samples (after BRR compression) combined; `smconv` prints the exact free
  space, so watch its output. Each WLA-DX section ≤ **32 KB** — split into named
  sections when needed.
- Sprites: sizes are multiples of 8 (8/16/32/64); mind per-scanline OAM limits.
- Palettes: **16 colors each** (15 + transparency), transparency color **first**
  (magenta `255,0,255` by convention).
- If an approach risks blowing any budget, flag it and offer a leaner option
  *before* implementing.

### 6. Asset pipeline
- **Graphics:** indexed art, ≤16 colors per palette, dimensions multiples of 8.
  Convert with `gfx4snes`; link into C via `data.asm` `.incbin` + `extern`
  declarations.
- **Music:** `.it` modules, **≤8 channels**, IT sample compression **OFF**
  (smconv can't read it), low sample rates to fit ARAM. Convert with `smconv`.
  SFX are also `.it`.
- Never pull assets from commercial games. Surface any external asset's license
  before we use it.

---

## Workflow & cadence (I'm new to this)

- **Teach as you build.** When you introduce a hardware concept (DMA, OAM,
  CGRAM, HDMA, NMI, Mode 1 vs Mode 7), explain briefly *why* it works and link
  the relevant PVSnesLib wiki page.
- **Small, verifiable slices.** One feature at a time: build → run → I confirm
  in Mesen2 → then move on. No big-bang multi-system changes.
- **Edit real files in the repo**, don't paste large code blocks into chat.
- **Keep the build green** — `make` must succeed before any slice is "done".

## Definition of done (per feature)

1. Compiles clean, no new warnings.
2. Runs correctly in Mesen2.
3. Stays within ROM / VRAM / ARAM / section budgets.
4. Behaves correctly at **both** 50 Hz and 60 Hz.
5. Confirmed on real hardware before it counts as done for the compo.

---

## References — use these

**Full curated list: `RESOURCES.md`.** Quick hits:

- **PVSnesLib wiki** (canonical): Introduction, Hello World, Backgrounds,
  Sprites, Sounds and Musics, **Pal and NTSC**, **PVSneslib and Mesen2**.
- **PVSnesLib `snes-examples/`** — per-feature C samples (incl. Mills32's
  Mode 7 example).
- **Dr. Ludos**, 100% C / PVSnesLib, open source: *Keeping SNES Alive!* and
  *The Last Super* (drludos.itch.io). Read for real-game structure.
  Caveat: ~2020-era — cross-check APIs against current headers.
- **nesdoug.com / SNES_xx repos** — hardware concepts (DMA, VRAM, compression).
  Caveat: 65816 asm via cc65, **conceptual reference only**, not C.
- **NoCash fullsnes** (problemkaputt.de/fullsnes.htm) — authoritative hardware
  reference for register-level detail.

## Do NOT use as code sources

- Commercial game disassemblies / decompilations (e.g. Super Mario RPG,
  Super Mario Kart) or leaked retail source. Reference-only at most, never
  copied — and most use banned chips anyway. Copying any of it breaks the
  "original work" rule.
