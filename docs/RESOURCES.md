# RESOURCES.md — SNES Homebrew (PVSnesLib / C)

Companion to `CLAUDE.md`. Curated, vetted links for an original SNES game in C.
Caveats are honest on purpose — note which are C vs assembly, and which are
**study-only** (never copy into an original-work compo entry).

---

## Core toolkit (your actual toolchain)

- **PVSnesLib** — the C SDK (compiler, linker, tools, library).
  https://github.com/alekmaul/pvsneslib
- **PVSnesLib wiki** — primary documentation. Start here.
  https://github.com/alekmaul/pvsneslib/wiki
- Key wiki pages:
  - Compiling Hello World — https://github.com/alekmaul/pvsneslib/wiki/Compiling-helloworld-example
  - Pal and NTSC — https://github.com/alekmaul/pvsneslib/wiki/Pal-and-NTSC  *(region handling — critical for the compo)*
  - PVSneslib and Mesen2 — https://github.com/alekmaul/pvsneslib/wiki/PVSneslib-and-Mesen2
  - Sounds and Musics — https://github.com/alekmaul/pvsneslib/wiki/Sounds-and-Musics
  - Backgrounds / Sprites / Dynamic Sprites — see the wiki sidebar
- **snes-examples** — official per-feature C samples (joypad, modes, sound,
  Mills32's Mode 7). Ships with the PVSnesLib release.

---

## Alternative engines / toolchains (compo-approved, not your main path)

These are the other officially-listed SNESdev jam toolchains. Useful to know
about; you're committed to PVSnesLib (C), but worth a look if you hit a wall.

- **libSFX** — mature SNES framework in **assembly** (ca65-based). The jam's
  recommended asm option. https://github.com/Optiroc/libSFX
- **SNDK** — SDK using **Higueul**, Kannagi's own low-level C-like language
  (compiles near-asm efficiency, C-ish syntax — *not* C, single-author,
  niche). Includes a RetroTracker for audio. https://github.com/Kannagi/SNDK
- **64tass** — 6502/65816 macro cross-assembler (raw asm tool).
  https://sourceforge.net/projects/tass64/
- **vbcc** — retargetable C compiler with a 65816 backend; a more DIY C path
  than PVSnesLib (you bring your own SNES runtime). http://www.compilers.de/vbcc.html

---

## Tutorials & concepts

- **nesdoug — SNES Programming Guide** — the best free structured course
  (65816 basics → DMA, NMI, backgrounds, sprites, HDMA, color math, audio,
  modes, IRQs). https://nesdoug.com/2020/03/19/snes-projects/
  ⚠️ Assembly / ca65, **not** PVSnesLib C. Use for *hardware understanding*;
  don't lift code.

---

## Open-source games to read (C — safe to study & adapt)

All 100% C with PVSnesLib, by Dr. Ludos. Heavily commented.

- **Keeping SNES Alive!** — simplest; template for Mode 1 graphics, controllers,
  music/SFX. ROM is ~256 KB. https://drludos.itch.io/keeping-snes-alive
  *(2020-era — cross-check APIs against current headers.)*
- **The Last Super** — a bit larger; bigger maps.
  https://drludos.itch.io/the-last-super
- **Yo-Yo Shuriken** — the game PVSnesLib cites in its own readme.
  https://drludos.itch.io/yo-yo-shuriken

---

## Hardware reference (authoritative)

- **NoCash fullsnes** — register-level hardware bible. Dense but complete.
  https://problemkaputt.de/fullsnes.htm
- **Super Famicom Dev Wiki** — https://wiki.superfamicom.org/
- **SNESdev Wiki (nesdev)** — https://snes.nesdev.org/wiki/
- **Audio drivers overview (nesdev wiki)** — survey of SPC700 sound drivers,
  useful background even though you'll use PVSnesLib's SNESMod.
  https://snes.nesdev.org/wiki/Audio_drivers

---

## Asset tools

Graphics
- **gfx4snes** — bitmap → SNES format. Ships with PVSnesLib.
- **nesdoug M1TE2** — Mode 1 tile editor (cross-platform via MONO).
  https://github.com/nesdoug/M1TE2
- **nesdoug M8TE** — Mode 3/7 (8bpp) tile editor.
  https://github.com/nesdoug/M8TE
- **nesdoug SPEZ** — SNES sprite editor. https://github.com/nesdoug/SPEZ
- Any indexed-palette editor (Aseprite, GIMp indexed mode) works too — keep
  ≤16 colors/palette, transparency color first.

Audio
- **smconv** + **snesbrr** — `.it`/`.wav` → SNES soundbank. Ship with PVSnesLib.
- **OpenMPT** — Impulse Tracker composing (Windows). https://openmpt.org/
  *(turn OFF IT sample compression — smconv can't read it.)*
- **Schism Tracker** — cross-platform IT tracker. https://schismtracker.org/

---

## Emulators & hardware testing

- **Mesen2** — accurate, excellent debugger. Primary dev/debug target.
  https://github.com/SourMesen/Mesen2
- **ares** — second accuracy cross-check.
  https://github.com/ares-emulator/ares
- **Real hardware:** FXPak Pro (sd2snes firmware — https://sd2snes.de/) or
  Super EverDrive (https://krikzz.com/). Non-negotiable before "done".

---

## Community / help

- **SNES Development Discord** — the main hub; fast help.
  https://discord.com/invite/3K2EAFBF84
- **PVSnesLib Discord** — library-specific; invite is in the PVSnesLib README.
- **NESdev SNESdev subforum** — https://forums.nesdev.org/viewforum.php?f=12
- **SMW Central — ASM & Related** — good for raw 65816 questions.
  https://www.smwcentral.net/

## The compo itself

- **SNESdev game jam (itch.io)** — the jam these tools are recommended for;
  judged on gameplay and correct hardware use. Check its page for the exact
  edition's rules (size limit, deadline). Your constraints match this family.

---

## Study-only — DO NOT copy (breaks "original work")

Reference for curiosity at most; never lift code, data, or assets. Many also
use banned enhancement chips.

- Commercial disassemblies — e.g. Super Mario RPG (SA-1), Super Mario Kart
  (DSP-1). github.com/Yoshifanatic1/…
- Reimplementations — e.g. snesrev/zelda3 (C, but a PC port — abstracts the
  hardware away, so not useful for learning SNES rendering/timing anyway).
- Any leaked retail source (Gigaleak, etc.) — legally radioactive.
