# snes-homebrew

An original SNES game written in **C with [PVSnesLib](https://github.com/alekmaul/pvsneslib)**,
targeting a homebrew competition with hard hardware constraints (LoROM, ≤512 KB,
no enhancement chips, no SRAM, single ROM that runs on both NTSC **and** PAL,
must work on real hardware, 100% original work).

The full brief, hardware rules, and build plan live in [`docs/`](docs/):

| Doc | What it is |
|-----|------------|
| [`docs/CLAUDE.md`](docs/CLAUDE.md) | Project charter — constraints, toolchain, working conventions. **Read first.** |
| [`docs/RESOURCES.md`](docs/RESOURCES.md) | Curated, vetted links (wiki, hardware refs, asset tools, study-only list). |
| [`docs/SLICE_1_GUIDE.md`](docs/SLICE_1_GUIDE.md) | Build slice 1 — region-aware "hello world" that proves the pipeline. |
| [`docs/SLICE_2_GUIDE.md`](docs/SLICE_2_GUIDE.md) | Build slice 2 — title screen + music + animated, moving sprite. |

---

## Toolchain (pinned)

> Per `docs/CLAUDE.md`, the installed PVSnesLib version is pinned here because the
> API surface drifts between releases. **When a guide and an installed header
> disagree, the header wins.**

| Tool | Version | Installed at | Source |
|------|---------|--------------|--------|
| PVSnesLib | **4.5.0** (`PVSnesLib V4.5.0`, released 2025-12-28) | `~/pvsneslib` → `$PVSNESLIB_HOME` | [release `4.5.0`](https://github.com/alekmaul/pvsneslib/releases/tag/4.5.0) (`pvsneslib_450_64b_darwin.zip`) |
| Mesen2 | **2.1.1** (released 2025-07-06) | `/Applications/Mesen.app` | [release `2.1.1`](https://github.com/SourMesen/Mesen2/releases/tag/2.1.1) (`macOS_ARM64_AppleSilicon`) |

Host this was set up on: **macOS 26.4.1, Apple Silicon (arm64)**. Both the
PVSnesLib tool binaries and Mesen are native arm64 — no Rosetta required.

---

## Environment

`$PVSNESLIB_HOME` is exported from `~/.zshrc`:

```sh
export PVSNESLIB_HOME="$HOME/pvsneslib"
```

That single variable is all the build needs — `devkitsnes/snes_rules` references
every tool (816-tcc, wla-65816, wlalink, gfx4snes, smconv, snesbrr) by absolute
path under `$PVSNESLIB_HOME`, so nothing extra goes on `PATH`. `make` (from
Xcode Command Line Tools) is already on `PATH`.

> Open a **new terminal** (or `source ~/.zshrc`) before building, so the variable
> is set in your shell.

---

## macOS patch applied to the toolchain

PVSnesLib's shared build rules (`$PVSNESLIB_HOME/devkitsnes/snes_rules`) post-process
the linker's `.sym` file with GNU-style `sed -i '<script>'`. macOS ships **BSD sed**,
which misparses that form and fails the build at the final step (the ROM links fine,
then `make` errors with `sed: ... extra characters at the end of h command`).

Fixed by rewriting the two in-place edits to a portable `sed > tmp && mv` form
(works with both BSD and GNU sed, no extra dependency):

- File patched: `$PVSNESLIB_HOME/devkitsnes/snes_rules` (lines ~178 and ~184)
- Original preserved as: `$PVSNESLIB_HOME/devkitsnes/snes_rules.orig`

This file is **outside the repo** (it lives in the PVSnesLib install), and it's
shared by every PVSnesLib project on this machine. **If you reinstall or upgrade
PVSnesLib, re-apply this patch** (or `make` will fail on the `.sym` step again).

In `$PVSNESLIB_HOME/devkitsnes/snes_rules`, change each of the two `sed -i` lines
from the in-place form to a temp-file form:

```make
# before (GNU-only, breaks on macOS BSD sed):
	@sed -i '<script>' $(ROMNAME).sym
# after (portable):
	@sed '<script>' $(ROMNAME).sym > $(ROMNAME).sym.tmp && mv -f $(ROMNAME).sym.tmp $(ROMNAME).sym
```

The two affected scripts are `s/://` and
`/ SECTIONSTART_/d;/ SECTIONEND_/d;/ RAM_USAGE_SLOT_/d;`.

---

## Verify the toolchain

Build the stock `hello_world` example that ships with PVSnesLib (this exercises
the whole chain: gfx4snes → 816-tcc → wla-65816 → wlalink → `.sym` post-process):

```sh
cp -R "$PVSNESLIB_HOME/snes-examples/hello_world" /tmp/hw && cd /tmp/hw
make clean && make
```

A clean run ends with `Build finished successfully !` and produces a 256 KB
`hello_world.sfc`. ✅ Verified working on this setup (2026-06-19).

---

## Build & run a project

From a project directory (one with a `Makefile` that `include`s `snes_rules`):

```sh
make            # produces <ROMNAME>.sfc (+ .sym for the debugger)
make clean      # remove build artifacts
```

Open the ROM in Mesen2 for debugging:

```sh
mesen build/slice1.sfc      # helper defined in ~/.zshrc
```

> **Why not `open -a Mesen file.sfc`?** Mesen.app declares no `.sfc` document
> type, so that command launches Mesen *without* loading the ROM — you get a
> black screen. The `mesen` helper passes the ROM as a command-line argument
> (`open -na Mesen --args <abs-path>`) instead. You can also just drag the `.sfc`
> onto the Mesen window, or use **File → Open** (⌘O).

> **First launch of Mesen:** it's self-signed (not Apple-notarized), so if macOS
> refuses to open it, right-click the app → **Open** → **Open**, or allow it under
> **System Settings → Privacy & Security**. It was installed via `curl` (no
> quarantine flag), so it should open directly.

### Region testing (required by the compo)

Every slice must behave identically at 50 Hz and 60 Hz. In Mesen2, force the
console region (NTSC, then PAL) via the emulation settings and confirm timing
matches. Use the **Event Viewer** to confirm there are no VRAM writes outside
VBlank. See `docs/SLICE_1_GUIDE.md` §7.

---

## Hard constraints (do not violate)

LoROM only · ROM ≤ 512 KB · no special/enhancement chips · no SRAM
(`CARTRIDGETYPE $00`, `SRAMSIZE $00`) · single ROM, runtime NTSC/PAL detection ·
must run on real hardware · 100% original assets & code. Full detail in
[`docs/CLAUDE.md`](docs/CLAUDE.md).
