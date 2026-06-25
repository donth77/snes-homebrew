# snes-homebrew

[![Platform: SNES](https://img.shields.io/badge/platform-SNES-8b5cf6)](https://en.wikipedia.org/wiki/Super_Nintendo_Entertainment_System)
[![Language: C](https://img.shields.io/badge/language-C-00599c?logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C_(programming_language))
[![PVSnesLib 4.5.0](https://img.shields.io/badge/PVSnesLib-4.5.0-f7882f)](https://github.com/alekmaul/pvsneslib)

Original Super Nintendo games, written from scratch in **C** with
**[PVSnesLib](https://github.com/alekmaul/pvsneslib)**. These are real ROMs: each one is a single
LoROM cartridge image under 512 KB, uses no enhancement chips, and boots on actual SNES hardware as
well as emulators. A runtime region check keeps them running correctly on both NTSC (60 Hz) and PAL
(50 Hz) consoles.

Inside you'll find a complete action-platformer demo and a smaller project that exercises the full
build pipeline end to end.

---

## Gothicvania (`gothicvania/`)

A SNES action-platformer demo set in a haunted graveyard.

![Gothicvania — the hero faces a risen skeleton under a blood moon](media/gothicvania.png)

- **Flow:** title screen → play → "Game Over" (on death) or "Thanks for Playing" (on finish),
  driven by a `TITLE → PLAY → END / GAMEOVER` state machine.
- **World:** a 4800 px horizontally-scrolling level with 3-depth HDMA parallax, a per-scanline
  colour-math sky gradient, and a moon — rendered by a custom 64×32 page-streaming engine that
  keeps the whole level resident in under 16 KB of VRAM.
- **Hero:** run / jump / attack / hurt, gravity + per-tile collision (solid ground + one-way
  platforms), with movement speeds that stay equal on NTSC and PAL.
- **Enemies:** three types sharing two streamed sprite slots — **skeleton** (rises from the ground,
  chases), **hell-gato** (paces back and forth), **ghost** (floats, bobs, turns to face you) — each
  with sword-kill + contact-knockback combat and a shared fiery death poof.
- **Hazards & life:** spike pits (−1 HP), a 3-HP limit → game over, fall-out respawn.
- **Audio:** original looping music (separate in-game and title tracks) + sound effects, via snesmod.

> **Art:** the graphics are adapted from the **[GothicVania Cemetery](https://ansimuz.itch.io/gothicvania-cemetery)**
> pack by **[ansimuz](https://ansimuz.itch.io/)**, released under CC0, reworked to fit SNES VRAM and
> palette limits. (Full credits below.)

## hello-world-region (`hello-world-region/`)

A region-aware "hello world" that walks the whole pipeline end to end
(C → 816-tcc → WLA-DX → ROM → Mesen2 → hardware) and shows identical behaviour at 50 Hz and 60 Hz.

![hello-world-region — region detection (NTSC / 60 Hz) and a per-second counter](media/hello-world-region.png)

---

## Building it yourself

### Toolchain

| Tool | Version | Installed at | Source |
|------|---------|--------------|--------|
| PVSnesLib | **4.5.0** (released 2025-12-28) | `~/pvsneslib` → `$PVSNESLIB_HOME` | [release `4.5.0`](https://github.com/alekmaul/pvsneslib/releases/tag/4.5.0) (`pvsneslib_450_64b_darwin.zip`) |
| Mesen2 | **2.1.1** (released 2025-07-06) | `/Applications/Mesen.app` | [release `2.1.1`](https://github.com/SourMesen/Mesen2/releases/tag/2.1.1) (`macOS_ARM64_AppleSilicon`) |

Built and tested on **macOS 26.4.1, Apple Silicon (arm64)**. Both the PVSnesLib tool binaries and
Mesen are native arm64 — no Rosetta required.

### Setup

PVSnesLib finds all its tools through a single environment variable, exported from `~/.zshrc`:

```sh
export PVSNESLIB_HOME="$HOME/pvsneslib"
```

That's all the build needs — `devkitsnes/snes_rules` references every tool (816-tcc, wla-65816,
wlalink, gfx4snes, smconv, snesbrr) by absolute path under `$PVSNESLIB_HOME`. `make` comes from the
Xcode Command Line Tools. Open a new terminal (or `source ~/.zshrc`) so the variable is set.

### Build

From a project directory (`gothicvania/` or `hello-world-region/`):

```sh
make            # produces <ROMNAME>.sfc (+ .sym for the debugger)
make clean      # remove build artifacts
```

> The committed `gothicvania/res/*.png` and `*.bin` are the source of truth for the art, and `make`
> builds the ROM straight from them. The original source-art pack isn't in the repo, so the
> `tools/adapt_*.py` / `build_*.py` converters are kept as readable references only — they no longer
> run (see the Makefile's "FROZEN ART" note).

### Run it in Mesen

```sh
mesen gothicvania/gothicvania.sfc      # helper defined in ~/.zshrc
```

The `mesen` helper passes the ROM as a CLI argument (`open -na Mesen --args <abs-path>`). Plain
`open -a Mesen file.sfc` won't work — Mesen.app declares no `.sfc` document type, so it launches
without loading the ROM. You can also drag the `.sfc` onto the window, or use **File → Open** (⌘O).
To see the NTSC/PAL behaviour, force each region in Mesen's emulation settings.

### Building on macOS

PVSnesLib's shared build rules post-process the linker's `.sym` file with GNU-style
`sed -i '<script>'`. macOS ships **BSD sed**, which misparses that form and fails the final build
step (`sed: ... extra characters at the end of h command`) even though the ROM itself links fine.

The fix is to rewrite the two in-place edits in `$PVSNESLIB_HOME/devkitsnes/snes_rules` (around lines
178 and 184) to a portable temp-file form:

```make
# before (GNU-only, breaks on macOS BSD sed):
	@sed -i '<script>' $(ROMNAME).sym
# after (portable, works with BSD and GNU sed):
	@sed '<script>' $(ROMNAME).sym > $(ROMNAME).sym.tmp && mv -f $(ROMNAME).sym.tmp $(ROMNAME).sym
```

The two scripts are `s/://` and `/ SECTIONSTART_/d;/ SECTIONEND_/d;/ RAM_USAGE_SLOT_/d;`. This file
lives in the PVSnesLib install (outside this repo) and is shared by every PVSnesLib project on the
machine, so re-apply the change after reinstalling or upgrading PVSnesLib. The original is preserved
as `snes_rules.orig`.

---

## Credits & acknowledgements

- **Art** — [GothicVania Cemetery](https://ansimuz.itch.io/gothicvania-cemetery) by
  [ansimuz](https://ansimuz.itch.io/), released under CC0. Adapted to fit SNES VRAM and palette
  limits.
- **[PVSnesLib](https://github.com/alekmaul/pvsneslib)** by Alekmaul and contributors — the C
  toolchain and SNES library these games are built on.
- **[Mesen2](https://github.com/SourMesen/Mesen2)** by SourMesen — emulator and debugger.
