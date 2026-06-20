/*---------------------------------------------------------------------------------
    gothicvania — walkable level with full hero animation.

    The level is TWO backgrounds, exactly the way the demo is layered (no flattening -> no
    lossy tile merge -> pixel-exact): BG0 = ground+grass (front), BG1 = decorations (behind).
    Both stream the 4800px level in lockstep (64x32 two-page ping-pong; pages DMA'd from ROM),
    and Mode-1 priority keeps BG0's grass in front of BG1's decoration bases while their tops
    show through the transparent sky. The hero (OBJ) has gravity + collision + a full animation
    set; we DMA only the current 4KB frame when it changes. Collision: level_collision.c.
---------------------------------------------------------------------------------*/
#include <snes.h>
#include "hero_anim.h"

extern unsigned char groundtiles, groundtilesend, groundpal, groundpalend;
extern unsigned char groundpagesA, groundpagesB;
extern unsigned char decotiles, decotilesend, decopal, decopalend;
extern unsigned char decopagesA, decopagesB;
extern unsigned char parallaxtiles, parallaxtilesend, parallaxpal, parallaxpalend, parallaxmap;
extern unsigned char sky_gradient;
extern unsigned char moontiles, moontilesend, moonpal, moonpalend;
extern unsigned char hero_a, hero_b, hero_c, hero_pal, hero_palend;
extern const unsigned char levelCollision[];

#define LVL_COLS   300
#define LVL_ROWS   14
#define LVL_PXW    4800
#define CAM_MAX    (LVL_PXW - 256)
#define N_PAGES    19
#define PAGE_SPLIT 15

// VRAM layout (word addresses), tightly packed so NOTHING overlaps. Char bases are 0x1000-aligned,
// map bases 0x400-aligned. Each tile region is immediately followed by its OWN map inside the slack
// of its char-block, so BG2's tiles can never land under BG0's streamed map again (the old bug that
// garbled the clouds: BG2 tiles 0x6000-0x7E20 swallowed BG0_MAP at 0x6400). Budget: parallax must
// stay <=768 tiles (it's 710) so BG2 tiles+map fit one 2-block pair. Total ends exactly at 0x8000.
//  0x0000 hero | 0x0800 moon | 0x1000 BG0 tiles | 0x1800 BG0 map | 0x2000 BG2 tiles(710)
//  | 0x3800 BG2 map | 0x4000 BG1 tiles(deco 880) | 0x7800 BG1 map.
#define HERO_VRAM  0x0000
#define MOON_VRAM  0x0800                  // moon OBJ band (tiles 128+)
#define BG0_TILES  0x1000                  // ground tiles (112)  -> 0x1000..0x1700
#define BG0_MAP    0x1800                  // ground (Main) tilemap, streamed
#define BG2_TILES  0x2000                  // parallax tiles (2bpp, 710) -> 0x2000..0x3630
#define BG2_MAP    0x3800                  // parallax tilemap
#define BG1_TILES  0x4000                  // deco tiles (880)  -> 0x4000..0x7700
#define BG1_MAP    0x7800                  // deco (Back) tilemap, streamed

// player hitbox / sprite mapping. feetX/feetY = bottom-centre of the hitbox; the sprite's
// feet sit at frame-local (26,58) (set by tools/adapt_hero.py), 37 after an hflip.
#define HALF_W     6
#define BODY_HI    24
#define BODY_LO    4
#define SPR_OY       HERO_FEET_Y          // feet position inside the 64-tall sprite
#define HERO_LEFT_DX 64                   // the LEFT 64x64 sprite sits 64px left of the head
// 2-sprite hero: the strip is 128px wide (L|R), so gfx4snes packs each frame's two 64x64
// halves into one 4KB band -- L at gfxoffset 0, R at gfxoffset 8. One DMA per frame.
#define HERO_BANDSZ 4096
#define HBANK       7                     // 7 frames/bank: a(0..6) b(7..13) c(14..19)

#define JUMP_KEYS   (KEY_B | KEY_A)
#define ATTACK_KEYS (KEY_Y | KEY_X)

#define PPU_CLEAN_INIT 1   // clear the PPU's power-on garbage (colour math / windows / CGRAM); see main()

u16 camX = 0;
u16 curA = 0, curB = 1;

s32 feetX, feetY;                                      // 8.8 fixed-point pixels
s16 vx, vy;
u8  onGround = 0, facing = 0;
u16 jumpDir = 0;                                       // horizontal dir held at takeoff -> momentum through the jump arc
// DEBUG capture (remove once flicker is closed): wrapping per-frame log, auto-frozen on a mid-level
// scroll stall while moving (the INPUT-freeze signature) or on SELECT, so the glitch window is
// caught with no timing pressure and the harness can dump camX/feetX/input/stream state.
#define CAPN 512
u16 capCamX[CAPN]; u16 capFeet[CAPN]; u8 capFlag[CAPN];
u16 capHead = 0, capCount = 0, capStop = 0, capPrevCam = 0; u8 capDone = 0;

// ROM address of streaming page p for each background (pages 0..14 in A, 15..18 in B).
static u8 *groundPage(u16 p)
{
    if (p < PAGE_SPLIT) return (u8 *)(&groundpagesA) + (u32)p * 2048;
    return (u8 *)(&groundpagesB) + (u32)(p - PAGE_SPLIT) * 2048;
}
static u8 *decoPage(u16 p)
{
    if (p < PAGE_SPLIT) return (u8 *)(&decopagesA) + (u32)p * 2048;
    return (u8 *)(&decopagesB) + (u32)(p - PAGE_SPLIT) * 2048;
}

// Address in ROM of hero frame f's 4KB band (its L+R 64x64 halves), split into three banks.
static u8 *heroFrameSrc(u8 f)
{
    if (f < HBANK)     return (u8 *)(&hero_a) + (u32)f * HERO_BANDSZ;
    if (f < 2 * HBANK) return (u8 *)(&hero_b) + (u32)(f - HBANK) * HERO_BANDSZ;
    return (u8 *)(&hero_c) + (u32)(f - 2 * HBANK) * HERO_BANDSZ;
}

// 0 = empty, 1 = full solid (walls + floor), 2 = one-way platform (lands from the top only,
// like the demo's setCollision(false,false,true,false)). Off the sides/below = solid wall.
static u8 cellv(s16 col, s16 row)
{
    if (col < 0 || col >= LVL_COLS || row >= LVL_ROWS) return 1;
    if (row < 0) return 0;
    return levelCollision[row * LVL_COLS + col];
}

int main(void)
{
    u16 animTimer = 0;
    u8  animFrame = 0, lastAnimF = 255, lastGfx = 255, heroGfx = 0;
    u8  attackTimer = 0;
    u16 prevPad = 0;
    s16 WALK, GRAV, JUMP, VYMAX;

#if PPU_CLEAN_INIT
    // The SNES powers on with GARBAGE in CGRAM and in the colour-math / subscreen / window
    // registers, and nothing clears them implicitly. Without this, the palettes get blended
    // with random colour math -- which is exactly why the colours glitched DIFFERENTLY on
    // every reload on real hardware (the testrunner's clean power-on state hid it). Force a
    // known-clean colour pipeline before anything is drawn:
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

    bgSetMapPtr(0, BG0_MAP, SC_64x32);
    bgSetMapPtr(1, BG1_MAP, SC_64x32);
    bgSetMapPtr(2, BG2_MAP, SC_64x32);
    bgInitTileSet(0, &groundtiles, &groundpal, 2,                 // ground -> palette 2 (CGRAM 32..47)
                  (&groundtilesend - &groundtiles), (&groundpalend - &groundpal),
                  BG_16COLORS, BG0_TILES);
    bgInitTileSet(1, &decotiles, &decopal, 1,                     // deco   -> palette 1 (CGRAM 16..31)
                  (&decotilesend - &decotiles), (&decopalend - &decopal),
                  BG_16COLORS, BG1_TILES);
    bgInitTileSet(2, &parallaxtiles, &parallaxpal, 0,            // mountains -> palette 0 (CGRAM 0..3)
                  (&parallaxtilesend - &parallaxtiles), (&parallaxpalend - &parallaxpal),
                  BG_4COLORS, BG2_TILES);

    WaitForVBlank();                                              // screen still off -> DMA unlimited
    dmaCopyVram(groundPage(0), BG0_MAP,         2048);
    dmaCopyVram(groundPage(1), BG0_MAP + 0x400, 2048);
    dmaCopyVram(decoPage(0),   BG1_MAP,         2048);
    dmaCopyVram(decoPage(1),   BG1_MAP + 0x400, 2048);
    dmaCopyVram(&parallaxmap,           BG2_MAP,         2048);   // static far layer: two 32x32 screens
    dmaCopyVram((&parallaxmap) + 2048,  BG2_MAP + 0x400, 2048);

    setMode(BG_MODE1, 0);                                         // BG0 > BG1 > BG2 (mountains at back)
    bgSetEnable(2);                                              // turn the parallax layer on
    bgSetScroll(2, 0, 0);                                        // VOFS 0; HOFS is driven per-scanline by HDMA
    // Backdrop (CGRAM 0) now comes FROM the parallax palette's entry 0 = the mountains' darkest colour
    // (adapt_parallax.py), so the area below the cut-off mountains reads as the mountain mass continuing
    // down to the horizon (no chopped flat edge). So we DON'T override CGRAM 0 here anymore.

    // Load hero palette + size + frame 0 into the sprite slot; later frames DMA over it.
    oamInitGfxSet(heroFrameSrc(0), HERO_BANDSZ, &hero_pal, (&hero_palend - &hero_pal),
                  0, HERO_VRAM, OBJ_SIZE32_L64);
    oamSetEx(0, OBJ_LARGE, OBJ_SHOW);                 // OBJ0 = left half
    oamSetEx(4, OBJ_LARGE, OBJ_SHOW);                 // OBJ4 = right half

    // Moon + GLOW: a 128x64 sprite = two 64x64 OBJs, so the bright glow is SMOOTH (16-colour OBJ,
    // not the blocky 4-colour BG). Fixed in the sky at OBJ priority 0 -- LOWEST -- so the mountains
    // (BG2 prio 1) pass in front of it AND it's the first thing to drop if a scanline ever overruns
    // 34 sprite-tiles (it never does: moon 16 + hero 16 = 32). Mountain BG tiles have transparent
    // gaps so this smooth moon+glow shows through at pixel resolution -> no tile-stepped moon edge.
    dmaCopyVram(&moontiles, MOON_VRAM, (&moontilesend - &moontiles));
    setPalette(&moonpal, 128 + 16, (&moonpalend - &moonpal));
    oamSet(8,  64,  28, 0, 0, 0, 128, 1);            // moon+glow LEFT  64x64 (gfxoffset 128)
    oamSet(12, 128, 28, 0, 0, 0, 136, 1);            // moon+glow RIGHT 64x64 (gfxoffset 136)
    oamSetEx(8,  OBJ_LARGE, OBJ_SHOW);
    oamSetEx(12, OBJ_LARGE, OBJ_SHOW);

    setScreenOn();

    // NOTE: the per-scanline sky-gradient HDMA (setModeHdmaColor, CGRAM channel 6) is REMOVED.
    // On real hardware that HDMA scrambled CGRAM with boot-varying garbage (every colour wrong,
    // different each reload) -- the Mesen testrunner emulated it cleanly so the bug stayed hidden
    // for a long time. We use the flat dark-purple backdrop (setPaletteColor(0,...) above); the
    // clouds (BG2) cover almost all of the sky, so the gradient was barely visible anyway.

    // Matched to the Phaser demo (game.js): move 150 px/s, gravity 300 px/s^2, jump 170 px/s
    // -> ~48px high, ~68-frame (1.13s) hang. Converted to 8.8 px/frame, scaled x1.2 / x1.44 at 50Hz.
    WALK  = snes_50hz ? 768  : 640;
    GRAV  = snes_50hz ? 30   : 21;
    JUMP  = snes_50hz ? -870 : -725;
    VYMAX = snes_50hz ? 1229 : 1024;

    feetX = (s32)80 << 8;
    feetY = (s32)192 << 8;                            // start standing on the ground
    vx = vy = 0;

    while (1)
    {
        u16 pad = padsCurrent(0);
        s16 ix, iy, c, fcol, frow, prevFeetPx;
        u16 L, wantE, wantO, loadPage = 0, slot = 0, mv;
        u8  need = 0, attacking, crouching, animF, animN, animSpd, sub, cv, dmaHero = 0, loop = 1;
        s8  fixedSub = -1;

        // --- input ---
        // Horizontal JUMP MOMENTUM + rollover bridge. A keyboard can drop a HELD direction for
        // a frame or two while the jump+direction chord is pressed (n-key rollover); that made
        // vx=0 and FROZE the scroll for those frames -- the jump-while-running "hitch".
        // (1) While JUMP is held, bridge a 1-frame directional dropout from last frame. This
        //     covers the grounded TAKEOFF/LANDING frames (where momentum hasn't engaged yet).
        //     Pure ground running (no jump held) is untouched -- you still stop on a dime.
        // (2) Airborne, keep the takeoff direction for the whole arc -- covers longer dropouts.
        // Jumping straight up doesn't drift (jumpDir=0); steering mid-air still works. A real
        // SNES pad can't drop a held button, so this only ever helps the emulator.
        mv = pad;
        if ((pad & JUMP_KEYS) && !(pad & (KEY_LEFT | KEY_RIGHT)))
            mv |= (prevPad & (KEY_LEFT | KEY_RIGHT));
        if (onGround) jumpDir = mv & (KEY_LEFT | KEY_RIGHT);
        else if (jumpDir && !(mv & (KEY_LEFT | KEY_RIGHT))) mv |= jumpDir;
        vx = 0;
        // Attack triggers on a fresh press while grounded. Once attacking, the d-pad is
        // ignored -- movement and facing are locked until the swing finishes (so pressing
        // attack while running stops you, and holding a direction mid-swing does nothing).
        if ((pad & ATTACK_KEYS) && !(prevPad & ATTACK_KEYS) && !attackTimer && onGround)
            attackTimer = A_ATTACK_N * 5;
        if (attackTimer) attackTimer--;
        attacking = (attackTimer != 0);

        crouching = (onGround && (pad & KEY_DOWN) && !attacking);
        if (!attacking && !crouching) {
            if (mv & KEY_LEFT)  { vx = -WALK; facing = 1; }
            if (mv & KEY_RIGHT) { vx =  WALK; facing = 0; }
        }
        if ((pad & JUMP_KEYS) && !(prevPad & JUMP_KEYS) && onGround && !attacking) { vy = JUMP; onGround = 0; }
        prevPad = pad;   // record AFTER every edge check, or the edges never fire

        // gravity
        vy += GRAV;
        if (vy > VYMAX) vy = VYMAX;

        // --- horizontal move + wall collision (only value-1 walls block; one-way platforms don't) ---
        feetX += vx;
        ix = feetX >> 8; iy = feetY >> 8;
        if (vx > 0) {
            c = (ix + HALF_W) >> 4;
            if (cellv(c, (iy - BODY_LO) >> 4) == 1 || cellv(c, (iy - BODY_HI) >> 4) == 1)
                feetX = (s32)((c << 4) - HALF_W) << 8;
        } else if (vx < 0) {
            c = (ix - HALF_W) >> 4;
            if (cellv(c, (iy - BODY_LO) >> 4) == 1 || cellv(c, (iy - BODY_HI) >> 4) == 1)
                feetX = (s32)(((c + 1) << 4) + HALF_W) << 8;
        }

        // --- vertical move + ground collision ---
        prevFeetPx = feetY >> 8;                       // feet before the fall, for one-way platforms
        feetY += vy;
        ix = feetX >> 8; iy = feetY >> 8;
        fcol = ix >> 4;
        if (vy >= 0) {
            frow = iy >> 4;
            cv = cellv(fcol, frow);
            // land on a full-solid tile always; on a one-way platform only when dropping onto its top
            if (cv == 1 || (cv == 2 && prevFeetPx <= (frow << 4))) {
                feetY = (s32)(frow << 4) << 8; vy = 0; onGround = 1;
            } else onGround = 0;
        } else onGround = 0;

        // --- camera follows the player ---
        ix = feetX >> 8;
        c = ix - 128;
        if (c < 0) c = 0;
        if (c > CAM_MAX) c = CAM_MAX;
        camX = c;

        // --- stream pages (both backgrounds in lockstep: same page index, same slot offset) ---
        L = camX >> 8;
        wantE = (L & 1) ? (L + 1) : L;
        wantO = (L & 1) ? L : (L + 1);
        if (wantE != curA && wantE < N_PAGES)      { need = 1; loadPage = wantE; slot = 0;     curA = wantE; }
        else if (wantO != curB && wantO < N_PAGES) { need = 1; loadPage = wantO; slot = 0x400; curB = wantO; }

        // --- pick animation (attack > airborne > crouch > run > idle) ---
        // loop=1 cycles forever (idle/run/fall); loop=0 plays once then holds the last frame.
        // The demo's jump RISE and the attack swing are one-shots -- looping them is what made
        // the sword wobble "back and forth" on the way up; only the FALL loops (hair waves).
        if (attacking)        { animF = A_ATTACK_F; animN = A_ATTACK_N; animSpd = 5; loop = 0; }
        else if (!onGround)   { animSpd = 7;
                                if (vy < 0) { animF = A_JUMP_F;     animN = 2;            loop = 0; }   // rise: 0->1 once, then hold
                                else        { animF = A_JUMP_F + 2; animN = A_JUMP_N - 2; } }           // fall: loop 2,3 (hair waves)
        else if (crouching)   { animF = A_CROUCH_F; animN = A_CROUCH_N; animSpd = 1; fixedSub = 0; }
        else if (vx != 0)     { animF = A_RUN_F;    animN = A_RUN_N;    animSpd = 6; }
        else                  { animF = A_IDLE_F;   animN = A_IDLE_N;   animSpd = 12; }

        if (animF != lastAnimF) { animFrame = 0; animTimer = 0; lastAnimF = animF; }
        if (fixedSub >= 0) sub = fixedSub;
        else {
            if (++animTimer >= animSpd) {
                animTimer = 0;
                if (loop) animFrame = (animFrame + 1) % animN;        // looping anims wrap
                else if (animFrame < animN - 1) animFrame++;          // one-shot: advance, then hold last
            }
            sub = animFrame;
        }
        if (sub >= animN) sub = animN - 1;

        heroGfx = animF + sub;
        // VBlank DMA budget: at a page seam BOTH backgrounds load a 2KB page (4KB total) which,
        // with the 544B OAM copy, nearly fills the ~6.4KB an NTSC VBlank can move; adding the 4KB
        // hero frame would overrun into active display and corrupt a tilemap (transient half-
        // objects / a ground gap that heals when you scroll back). The pages are display-critical
        // (resident before the camera reveals them), so they win; the hero waits one frame
        // (invisible -- seams are ~85 frames apart at walk speed).
        if (heroGfx != lastGfx && !need) { dmaHero = 1; lastGfx = heroGfx; }

        // Two 64x64 OBJs make the full hero. Facing right: OBJ0 = left half (gfxoffset 0) at
        // head-64, OBJ4 = right half (gfxoffset 8) at the head. Facing left the halves swap
        // and both hflip, mirroring around the head so the sword points the other way.
        {
            s16 hx = (feetX >> 8) - camX;
            s16 hy = (feetY >> 8) - SPR_OY;
            oamSet(0, hx - HERO_LEFT_DX, hy, 3, facing, 0, facing ? 8 : 0, 0);
            oamSet(4, hx,                hy, 3, facing, 0, facing ? 0 : 8, 0);
        }

        bgSetScroll(0, camX, 0);
        bgSetScroll(1, camX, 0);                         // both level layers at the same depth
        // BG2 carries TWO parallax bands at different rates via HDMA on its scroll register:
        // the mountains (top 104 scanlines) at 0.055x -- stays <256px so its native 384 pattern
        // never hits the 512 wrap -- and the graveyard (rest) at 0.25x (it's 512-tiled, wraps clean).
        {
            // Sky band (clouds + DETAILED mountains) is STATIC; only the graveyard scrolls. Moving the
            // silhouette mountains needs a featureless sky behind them, which flattens them into solid
            // blobs -- so the far background stays put and the mountains keep their detail.
            u16 grv = (u16)(((u32)camX * 64) >> 8);          // graveyard ~0.25x
            HDMATable16[0] = 104; HDMATable16[1] = 125; HDMATable16[2] = 0;                 // SKY band FIXED
            HDMATable16[3] = 120; HDMATable16[4] = grv & 0xFF; HDMATable16[5] = grv >> 8;   // GRAVEYARD 0.25x
            HDMATable16[6] = 0;                                                              // terminator
            setParallaxScrolling(2);                 // BG2 scroll banding (HDMA channel 3)
            REG_HDMAEN = 0x08;                        // ONLY ch3 (scroll). ch6 (the CGRAM palette HDMA) is
                                                      // GONE -- it was scrambling CGRAM on real hardware.
        }
        WaitForVBlank();
        if (need) {                                      // 2 page DMAs (4KB) -- hero is deferred this frame
            dmaCopyVram(groundPage(loadPage), BG0_MAP + slot, 2048);
            dmaCopyVram(decoPage(loadPage),   BG1_MAP + slot, 2048);
        }
        if (dmaHero) dmaCopyVram(heroFrameSrc(heroGfx), HERO_VRAM, HERO_BANDSZ);
        // DEBUG capture: log this frame; freeze the buffer 24 frames after a mid-level scroll
        // stall while moving (input-freeze), or 2 frames after SELECT, for the harness to read.
        if (!capDone) {
            capCamX[capHead] = camX;
            capFeet[capHead] = (u16)(feetX >> 8);
            capFlag[capHead] = ((pad & KEY_RIGHT) ? 1 : 0) | ((pad & KEY_LEFT) ? 2 : 0)
                             | (onGround ? 4 : 0) | (jumpDir ? 8 : 0) | (need ? 16 : 0) | (dmaHero ? 32 : 0);
            if (++capHead >= CAPN) capHead = 0;
            capCount++;
            if (!capStop) {
                if ((pad & KEY_SELECT) && capCount > 4) capStop = capCount + 2;
                else if (capCount > 12 && camX == capPrevCam && camX > 8 && camX < (u16)(CAM_MAX - 8)
                         && (jumpDir || (pad & (KEY_LEFT | KEY_RIGHT)))) capStop = capCount + 24;
            } else if (capCount >= capStop) capDone = 1;
            capPrevCam = camX;
        }
    }
    return 0;
}
