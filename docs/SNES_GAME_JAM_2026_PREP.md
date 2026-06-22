# Preparing for the SNES Dev Game Jam 2026

- Jam page: https://itch.io/jam/snes-dev-game-jam-2026
- Q&A thread: https://itch.io/jam/snes-dev-game-jam-2026/topic/5736496/questions-and-answers
- **Announced January, starts August, runs ~3 months.**

## The one rule that shapes everything
From the jam FAQ:

> The SNES is a difficult beast to code on. You may use the time [before the jam] to figure
> out how stuff works, set up a development toolchain, create a game-engine (music-player,
> sprite-routines, DMA-handler, whatever), write tools (gfx-converter, level-editors, whatever),
> build a team with other pixel-artists, musicians, coders and so on. **The game itself should
> be done in the Game Jam timeframe.**

So the prep window is for **everything except the game**: engine, tools, pipeline, team, and
hardware know-how. The 3 months of the jam are for *content + design*, not for re-learning the
hardware. The single biggest predictor of a finished jam game is **how little hardware-fighting
you have left to do once it starts.** This Gothicvania port already did most of that fighting —
mine it.

---

## 1. You already have a working engine — package it as a reusable starter

This repo (`gothicvania/`) is, in effect, a small SNES engine. Before August, extract the
reusable parts into a clean **template project** you can `cp -r` and start a game from:

- **State machine** (`game.c/.h`, `main.c`): `TITLE → PLAY → END/GAMEOVER` dispatch, each state
  owning its VRAM/palette/audio setup. Generalize to N states.
- **Page-streaming level renderer** (`level.c`, `play.c`): 64×32 two-slot page window with
  bidirectional camera + per-tile collision (`cellv`), streaming a 4800px level in <16 KB of
  VRAM. This is the hard part and it works — but it is **platformer-shaped**, not genre-neutral
  (see "Primitives vs. genre-specific systems" below before assuming it carries over).
- **Per-frame sprite DMA** (`player.c`, `enemy.c`): 64×64 metasprites uploaded as two 2 KB halves
  per VBlank, round-robined and deferred behind page streams. Includes the OAM z-order rules.
- **HDMA parallax + colour-math sky gradient** (`level.c`): 3-depth scroll banding on one BG +
  a per-scanline `$2132` gradient that can't corrupt CGRAM.
- **Audio**: snesmod soundbank (`smconv`), MIDI→IT pipeline, `spcLoadEffect`/`spcEffect` SFX,
  and the "skip the reload on respawn so music keeps playing" pattern.
- **Region handling**: every velocity/timer scaled by `snes_50hz` so NTSC and PAL run at equal
  wall-clock speed from one ROM. Bake this in from day one of any new project.

**Action:** make `snes-starter/` — the above with the Gothicvania-specific content stripped out,
a `hello` level, and a README. Starting a jam game from a known-good force-blank/DMA/VBlank
skeleton saves the first painful week.

### Primitives vs. genre-specific systems (can this become an any-genre toolkit?)

Tempting question: could this become a *general* SNES engine for any genre? Mostly **no — and for
a deeper reason than gameplay**: SNES genres differ at the **PPU/hardware level**, not just in
rules. A Mode-7 racer, a top-down RPG, a horizontal shmup, and this platformer use BG modes,
scrolling, HDMA, and sprite budgets *completely differently*. A renderer general enough to do all
of them is either bloated or lowest-common-denominator. Even pro SNES engines were genre-specialized.

So the right target is **not** a general engine — it's a **genre-agnostic primitives layer** you
specialize per game. That layer is most of the painful hardware-fighting, and it *does* carry over:

**Genre-agnostic — lift into `snes-starter/` as-is:**
- **State machine** (`game.c/.h`, `main.c`) — every game has states.
- **Sprite DMA engine** (`player.c`, `enemy.c`) — the real gold: per-frame metasprite upload in
  VBlank halves, round-robin, deferred behind other DMA. The VBlank-budget discipline is universal.
- **Audio** (snesmod soundbank, `mid2it.py`, `spcEffect`), **region handling** (`snes_50hz`
  scaling), the **raw HDMA mechanism**, the **Mesen headless test harness**, the **build/toolchain
  setup**, and the **hardware-gotcha knowledge** — all genre-independent, and the highest-leverage
  things you own.

**Genre-specific — rebuilt per genre, NOT in the starter:**
- **The level renderer** (`level.c`, `play.c`) — assumes *horizontal* page-streaming, a wide 1D
  level, and gravity + feet-anchored tile collision. None of that serves top-down (2D scroll),
  shmup (vertical scroll + wave scripting), single-screen puzzle (no streaming), or Mode 7.
- **Combat** (`enemyCombat`, knockback) — action-game gameplay.
- **The level data format** + `build_level.py` / `build_stream.py` — platformer-shaped.

**Level authoring is the most genre-specific part of all** (and splits further than "needs an editor"):
- **Platformer / top-down / RPG** → tile-based, so **Tiled** covers all three — but with *different
  export configs* (1D-horizontal vs 2D collision, object/spawn layers). `build_level.py` is one such config.
- **Shmup** → barely has level geometry; it's **enemy waves + bullet paths** → a wave/path *scripting*
  tool, not a tile editor.
- **Puzzle** → board state, often no scrolling → a board editor or just code.
- **Mode-7 racer** → track + heightmap authoring → a wholly different pipeline.

**Recommendation:** extract the primitives starter, **pick the genre early**, then build the *one*
genre-specific renderer + level pipeline you actually need on top of it. Don't build the general
toolkit — it's a time sink, and the jam rewards a finished game, not an engine. The two tools worth
generalizing *regardless* of genre: a **parameterized sprite importer** (the converters here hardcode
each sprite's anchor/paths) and a **shared-palette manager** (palette juggling across assets is a
universal SNES pain).

## 2. Keep the tools — they're your real advantage

The Python converters here are reusable and are exactly the "gfx-converter / level tools" the FAQ
says to build in advance:

- `adapt_hero.py` / `adapt_enemy.py` — sheet → feet-anchored 64×64 metasprite strip, 16-colour
  index, bank-split for per-frame DMA. Generalize the anchor logic (it chooses boots vs centre
  of mass per animation).
- `build_level.py` / `build_stream.py` — Tiled-ish map → 8×8 tileset + pre-expanded streaming
  pages + collision. **This is your level pipeline.** For the jam, wire it to **Tiled** directly
  so you (or an artist) can author levels visually.
- `adapt_title.py` / `adapt_parallax.py` / `gen_sky_gradient.py` / `adapt_moon.py` — screen
  overlays, 2-sub-palette parallax, HDMA gradient tables.
- `mid2it.py` / `adapt_sfx.py` — MIDI → tracker module, OGG → BRR SFX bank.
- **Mesen headless harness** (`tools/mesen/*.lua`): drive the ROM with scripted input, read
  WRAM/OAM/VRAM, capture screenshots. This let me verify behaviour without a controller —
  invaluable for regression-checking. Keep and extend it (it occasionally crashes on rapid
  screenshots during force-blank; space single captures out).

## 3. The asset lesson — this is the big one

The Gothicvania pack was drawn for a **Phaser** demo, not for SNES, and it cost us real pain:
90 px-tall heroes and 64×64 enemies blow the VRAM/OAM budget so only **2 enemies fit on screen**.

For the jam, decide assets at **SNES sizes from the start**:
- **Sprites:** design at 16×16 / 32×32, not 48–64. A 32×32 enemy is 128 B/frame and dozens fit;
  a 64×64 is 2 KB/frame and ~2–4 fit. Small resident sprites animated by swapping OAM tile-index
  (zero DMA) is how the real games did crowds. (See `docs/ENEMIES_4_ONSCREEN_PLAN.md`.)
- **Palettes:** ≤16 colours per sprite/BG, index 0 = transparent. Plan shared palettes.
- **Source the art deliberately:** either build a small team (the FAQ explicitly encourages
  recruiting pixel artists + musicians now), or pick CC0 packs **made for SNES/PCE-era specs**
  (e.g. tile-based, low-colour). Don't repeat the "repurpose a non-SNES pack" mistake.

## 4. Hardware gotchas already paid for (don't relearn these mid-jam)

- **No OBJ scaling.** Sprites are 1:1; only Mode 7 (a BG) scales/rotates. Display size = VRAM size.
- **OBJ VRAM is 16 KB / 512 tiles, hard cap.** Budget it on paper before drawing.
- **Sprite-vs-sprite z-order is OAM index, not the priority bits** (lower index = in front).
  The priority bits only order sprites vs BG layers.
- **VBlank DMA budget** (~6–7 KB NTSC). A page seam + OAM + a sprite frame can overrun into
  active display and corrupt a tilemap. Split big uploads across VBlanks; defer non-critical ones.
- **Write scroll registers in VBlank only** — they share a latch with HDMA; mid-frame writes flicker.
- **Bank boundaries:** a DMA can't cross a bank; `lda.w #label` overflows if a section's label
  lands at offset `$10000`. Keep per-frame blobs bank-internal; pass literal sizes for tiny
  sections. After adding a `.c`/section, do a **clean rebuild** — incremental builds reuse stale
  objects and hide section errors.
- **RAM boots as garbage** — initialise everything explicitly.
- **Verify on real hardware** (flashcart: SD2SNES/FXPak or a repro) before calling anything done;
  emulators are forgiving about timing the real PPU is not.

## 5. Suggested pre-jam timeline (Feb → Aug)

1. **Now–Mar:** finish this port (it's the reference game), then extract `snes-starter/`.
2. **Mar–Apr:** harden the toolchain — Tiled→level pipeline, a sprite-import GUI/script, a
   one-command "new project" generator. Document every gotcha in the starter README.
3. **Apr–Jun:** build/recruit a team; lock an **art style + sizes** and make a palette/sprite-size
   style guide. Prototype the *mechanic* you'll build the jam game around (not the content).
4. **Jun–Jul:** real-hardware test loop (flashcart + the headless harness). A music + SFX bank
   you're happy with. A "vertical slice" template level proving scroll+collision+enemies+audio.
5. **Aug (jam start):** you should be *making a game*, not configuring WLA-DX. Scope small —
   one tight mechanic, 3–5 levels, a boss — and polish. Finished + small beats ambitious + broken.

## 6. Scope advice
A solo/small-team SNES game in ~3 months is doable **only** if the engine and pipeline are done.
Pick a genre that reuses what you have here: a **side-scrolling action-platformer** is the path of
least resistance (you already have scrolling, collision, combat, enemies, parallax, audio). Save
Mode 7 / raster tricks / large bosses for *one* showpiece moment, not the whole game.
