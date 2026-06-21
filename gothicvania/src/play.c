/*---------------------------------------------------------------------------------
    play.c — the PLAY state: the walkable graveyard level.

    The level is TWO backgrounds, exactly the way the demo is layered (no flattening ->
    no lossy tile merge -> pixel-exact): BG0 = ground+grass (front), BG1 = decorations
    (behind). Both stream the 4800px level in lockstep (64x32 two-page ping-pong; pages
    DMA'd from ROM), and Mode-1 priority keeps BG0's grass in front of BG1's decoration
    bases while their tops show through the transparent sky. The hero (OBJ) has gravity +
    collision + a full animation set; we DMA only the current 4KB frame when it changes.

    Data helpers live in level.c (groundPage/decoPage/cellv) and player.c (heroFrameSrc);
    the moon + sky gradient (shared with the title) are set up via level.c. Reaching the
    right edge returns ST_END.
---------------------------------------------------------------------------------*/
#include "game.h"

GameState playState(void)
{
    u16 animTimer = 0;
    u8  animFrame = 0, lastAnimF = 255, lastGfx = 255, heroGfx = 0;
    u8  heroDmaQueue = 0, heroQueuedGfx = 0;   // hero frame uploaded as 2x 2KB halves (one per VBlank)
    u8  attackTimer = 0;
    u16 prevPad = 0;
    s16 WALK, GRAV, JUMP, VYMAX;

    setScreenOff();                              // force blank: free DMA while we (re)load the level VRAM
    camX = 0; curA = 0; curB = 1;                // reset streaming/camera (so a replay after END starts clean)
    facing = 0; jumpDir = 0; onGround = 0;

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
    bgSetScroll(2, 0, 0);                                       // VOFS 0; HOFS driven per band by HDMA ch3
    // Backdrop (CGRAM 0) comes FROM the parallax palette's entry 0 = the mountains' darkest colour
    // (adapt_parallax.py), so the area below the cut-off mountains reads as the mountain mass continuing
    // down to the horizon (no chopped flat edge), then armSkyGradient() overwrites it with the sky floor.

    setupHero();                                 // player.c: hero palette + frame 0 into OBJ0/OBJ4
    setupMoon(0);                                // level.c: moon+glow OBJ, BEHIND the mountains

    // Graveyard parallax: arm the BG2 scroll-banding HDMA (ch3) FIRST. Two bands: the mountains (top 104
    // lines) at a fixed offset, the graveyard (rest) at 0.25x. Per frame we then update ONLY the graveyard's
    // scroll value in HDMATable16 (the HDMA re-reads the table every frame) instead of re-running
    // setParallaxScrolling -- that per-frame churn perturbed the colour HDMA's timing (the 1-line flash).
    HDMATable16[0] = 104; HDMATable16[1] = 125; HDMATable16[2] = 0;   // mountains band: fixed offset 125
    HDMATable16[3] = 120; HDMATable16[4] = 0;   HDMATable16[5] = 0;   // graveyard band: scroll (set per frame)
    HDMATable16[6] = 0;                                               // terminator
    setParallaxScrolling(2);                   // arm ch3 from HDMATable16

    armSkyGradient();   // level.c: ch6 COLOR-MATH sky -- MUST be after setParallaxScrolling (bank-clobber)

    spcLoad(MOD_BAROQUE);                         // load the in-game Baroque track (during force blank)
    spcLoadEffect(SFX_JUMP);                      // (re)load the player SFX into the sound region (loading
    spcLoadEffect(SFX_ATTACK);                    // a module resets it, so this must follow spcLoad)
    spcLoadEffect(SFX_HURT);
    setScreenOn();
    spcPlay(0);                                   // play the music (loops); SFX fire via spcEffect()

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
        u8  need = 0, attacking, crouching, animF, animN, animSpd, sub, cv, loop = 1;
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
        if ((pad & ATTACK_KEYS) && !(prevPad & ATTACK_KEYS) && !attackTimer && onGround) {
            attackTimer = A_ATTACK_N * 5;
            spcEffect(4, SFX_ATTACK, 15 * 16 + 8);   // pitch 4 = 16kHz, vol 15, pan centre
        }
        if (attackTimer) attackTimer--;
        attacking = (attackTimer != 0);

        crouching = (onGround && (pad & KEY_DOWN) && !attacking);
        if (!attacking && !crouching) {
            if (mv & KEY_LEFT)  { vx = -WALK; facing = 1; }
            if (mv & KEY_RIGHT) { vx =  WALK; facing = 0; }
        }
        if ((pad & JUMP_KEYS) && !(prevPad & JUMP_KEYS) && onGround && !attacking) {
            vy = JUMP; onGround = 0;
            spcEffect(4, SFX_JUMP, 15 * 16 + 8);     // pitch 4 = 16kHz, vol 15, pan centre
        }
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

        // Level-end trigger: the hero reaches the right edge of the 4800px level -> "Thanks for Playing".
        if ((feetX >> 8) >= LVL_PXW - 40) return ST_END;

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
        // Queue a hero frame upload when the displayed frame changes (still deferred past page-stream
        // frames). The upload is then split into TWO 2KB halves, one per VBlank (see below) -- a single
        // 4KB hero DMA, landing on a frame where the hero is ALSO moving (jump), overran VBlank and made
        // the BG1 decoration tiles drop out for a frame ("object flicker"). 2KB/frame can't overrun.
        if (heroGfx != lastGfx && !need) { heroDmaQueue = 2; heroQueuedGfx = heroGfx; lastGfx = heroGfx; }

        // Two 64x64 OBJs make the full hero. Facing right: OBJ0 = left half (gfxoffset 0) at
        // head-64, OBJ4 = right half (gfxoffset 8) at the head. Facing left the halves swap
        // and both hflip, mirroring around the head so the sword points the other way.
        {
            s16 hx = (feetX >> 8) - camX;
            s16 hy = (feetY >> 8) - SPR_OY;
            oamSet(0, hx - HERO_LEFT_DX, hy, 3, facing, 0, facing ? 8 : 0, 0);
            oamSet(4, hx,                hy, 3, facing, 0, facing ? 0 : 8, 0);
        }

        spcProcess();                                // stream the music each frame
        WaitForVBlank();
        // CPU scroll-register writes MUST land in VBlank, NOT active display. The parallax HDMA (ch3)
        // writes BG3's offset every visible scanline, and ALL BG-offset registers ($210D/$210F/$2113...)
        // share ONE internal write latch. If an HDMA scanline-transfer fires between bgSetScroll's two
        // byte writes (low then high), it clobbers that shared latch and the scroll takes a garbage high
        // byte -- the whole BG snaps +256px (a different part of the level) for one frame, then back: the
        // "object flicker". It tracked the jump because the hero-animation DMA made those frames heavy
        // enough to push the OLD pre-VBlank scroll write out into active display, into the HDMA's path.
        // In VBlank the HDMA is idle, so the latch is ours alone. Do this BEFORE the page/hero DMA so a
        // DMA that overruns VBlank can't shove the scroll write back into active display either.
        bgSetScroll(0, camX, 0);
        bgSetScroll(1, camX, 0);                         // both level layers at the same depth
        {
            u16 grv = (u16)(((u32)camX * 64) >> 8);  // graveyard ~0.25x (BG2 band scroll; HDMA re-reads it)
            HDMATable16[4] = grv & 0xFF; HDMATable16[5] = grv >> 8;
        }
        REG_HDMAEN = 0x48;                           // ch3 (scroll banding) + ch6 (sky COLOR-MATH gradient)
        if (need) {                                      // 2 page DMAs (4KB) -- hero is deferred this frame
            dmaCopyVram(groundPage(loadPage), BG0_MAP + slot, 2048);
            dmaCopyVram(decoPage(loadPage),   BG1_MAP + slot, 2048);
        }
        // Upload ONE 2KB half of the queued hero frame this VBlank (top half, then bottom), deferred on
        // page-stream frames (those already move 4KB). Halving the per-frame VBlank DMA stops the overrun.
        if (heroDmaQueue && !need) {
            u8 hh = (heroDmaQueue == 2) ? 0 : 1;
            dmaCopyVram(heroFrameSrc(heroQueuedGfx) + (u32)hh * (HERO_BANDSZ / 2),
                        HERO_VRAM + hh * (HERO_BANDSZ / 4), HERO_BANDSZ / 2);
            heroDmaQueue--;
        }
    }
    return ST_END;
}
