/*---------------------------------------------------------------------------------
    play.c — the PLAY state: the walkable graveyard level.

    The level is TWO backgrounds (no flattening ->
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
    u8  hurtFlag = 0;                           // enemy contact staggers you (pop-up + shove) until you land
    s8  hurtDir = 0;                            // knockback push direction, away from the enemy (-1 L / +1 R)
    u8  health = PLAYER_HP;                     // 3 hits -> GAME OVER (the hurtFlag i-frame = 1 hit per stagger)
    u16 prevPad = 0;
    s16 WALK, GRAV, JUMP, VYMAX, HSHOVE, HPOP;  // HSHOVE/HPOP = enemy-contact knockback (region-scaled)
    // --- deco tile-streaming state (persists across frames; fresh each playState entry) ---
    u8  streaming = 0, streamMapPending = 0, streamPage = 0;
    u16 streamOff = 0, streamLen = 0, streamTileDst = 0, streamMapSlot = 0;
    u8 *streamSrc = (u8 *)0;

    setScreenOff();                              // force blank: free DMA while we (re)load the level VRAM
    camX = 0; curA = 0; curB = 1;                // reset streaming/camera (so a replay after END starts clean)
    facing = 0; jumpDir = 0; onGround = 0;

    bgSetMapPtr(0, BG0_MAP, SC_64x32);
    bgSetMapPtr(1, BG1_MAP, SC_64x32);
    bgSetMapPtr(2, BG2_MAP, SC_64x32);
    bgInitTileSet(0, &groundtiles, &groundpal, 2,                 // ground -> palette 2 (CGRAM 32..47)
                  (&groundtilesend - &groundtiles), (&groundpalend - &groundpal),
                  BG_16COLORS, BG0_TILES);
    bgInitTileSet(1, decoPageTiles(0), &decopal, 1,             // deco STREAMED: page 0 tiles -> slot 0,
                  decoPageTileBytes(0), (&decopalend - &decopal),  // palette block 1, char base = window
                  BG_16COLORS, BG1_TILES);
    bgInitTileSet(2, &parallaxtiles, &parallaxpal, 0,            // mountains -> palette 0 (CGRAM 0..3)
                  (&parallaxtilesend - &parallaxtiles), (&parallaxpalend - &parallaxpal),
                  BG_4COLORS, BG2_TILES);

    WaitForVBlank();                                              // screen still off -> DMA unlimited
    dmaCopyVram(groundPage(0), BG0_MAP,         2048);
    dmaCopyVram(groundPage(1), BG0_MAP + 0x400, 2048);
    dmaCopyVram(decoPageTiles(1), BG1_TILES + DECO_SLOT1, decoPageTileBytes(1));  // page 1 tiles -> slot 1
    dmaCopyVram(decoSmap(0),   BG1_MAP,         2048);           // window-local deco maps for pages 0, 1
    dmaCopyVram(decoSmap(1),   BG1_MAP + 0x400, 2048);
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
    enemyInit();                                 // enemy.c: skeleton palette + (milestone 1) static test skeletons

    // Graveyard parallax: arm the BG2 scroll-banding HDMA (ch3) FIRST. Two bands: the mountains (top 104
    // lines) at a fixed offset, the graveyard (rest) at 0.25x. Per frame we then update ONLY the graveyard's
    // scroll value in HDMATable16 (the HDMA re-reads the table every frame) instead of re-running
    // setParallaxScrolling -- that per-frame churn perturbed the colour HDMA's timing (the 1-line flash).
    HDMATable16[0] = 104; HDMATable16[1] = 125; HDMATable16[2] = 0;   // mountains band: fixed offset 125
    HDMATable16[3] = 120; HDMATable16[4] = 0;   HDMATable16[5] = 0;   // graveyard band: scroll (set per frame)
    HDMATable16[6] = 0;                                               // terminator
    setParallaxScrolling(2);                   // arm ch3 from HDMATable16

    armSkyGradient();   // level.c: ch6 COLOR-MATH sky -- MUST be after setParallaxScrolling (bank-clobber)

    // Physics + start position set BEFORE the screen comes on, so we can place the hero in the OAM now.
    // move 150 px/s, gravity 300 px/s^2, jump 170 px/s
    // -> ~48px high, ~68-frame (1.13s) hang. Converted to 8.8 px/frame, scaled x1.2 / x1.44 at 50Hz.
    WALK  = snes_50hz ? 768  : 640;
    GRAV  = snes_50hz ? 30   : 21;
    JUMP  = snes_50hz ? -870 : -725;
    VYMAX = snes_50hz ? 1229 : 1024;
    HSHOVE = snes_50hz ? 512  : 427;             // enemy knockback shove (demo vx = +-100 px/s)
    HPOP   = snes_50hz ? -768 : -640;            // enemy knockback pop-up (demo vy = -150 px/s)
    feetX = (s32)80 << 8;
    feetY = (s32)192 << 8;                            // start standing on the ground
    vx = vy = 0;
    // Place the hero at its start NOW so it doesn't flash at the OAM's init spot (top-left) for a frame
    // when the screen turns on -- spcLoad's internal VBlank would otherwise upload the un-positioned hero.
    {
        s16 hx = (feetX >> 8) - camX;
        s16 hy = (feetY >> 8) - SPR_OY;
        oamSet(HERO_OAM,     hx - HERO_LEFT_DX, hy, 3, facing, 0, facing ? 8 : 0, 0);
        oamSet(HERO_OAM + 4, hx,                hy, 3, facing, 0, facing ? 0 : 8, 0);
    }
    enemyDraw();                                  // place the test skeletons too, before the screen turns on

    // On a death respawn (PLAY->PLAY) the Baroque track + SFX are already in ARAM and the song is mid-play;
    // re-running spcLoad would re-upload the whole module (the multi-frame stall behind the death's black
    // flash) AND restart the song. So load only on a FRESH entry; on respawn keep the music running.
    if (!respawn) {
        spcLoad(MOD_BAROQUE);                     // load the in-game Baroque track (during force blank)
        spcLoadEffect(SFX_JUMP);                  // (re)load the player SFX into the sound region (loading
        spcLoadEffect(SFX_ATTACK);                // a module resets it, so this must follow spcLoad)
        spcLoadEffect(SFX_HURT);
        spcLoadEffect(SFX_RISE);                  // skeleton rise (enemyUpdate fires it on spawn)
        spcLoadEffect(SFX_KILL);                  // enemy death (enemyCombat fires it on a kill)
    }
    WaitForVBlank();                              // upload the OAM (hero placed + moon) BEFORE the screen on
    setScreenOn();
    if (!respawn) spcPlay(0);                     // play the music (loops); SFX fire via spcEffect()

    while (1)
    {
        u16 pad = padsCurrent(0);
        s16 ix, iy, c, fcol, frow, prevFeetPx;
        u16 L, wantE, wantO, mv;
        u8  attacking, crouching, animF, animN, animSpd, sub, cv, loop = 1;
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
        if ((pad & ATTACK_KEYS) && !(prevPad & ATTACK_KEYS) && !attackTimer && onGround && !hurtFlag) {
            attackTimer = A_ATTACK_N * 5;
            spcEffect(4, SFX_ATTACK, 15 * 16 + 8);   // pitch 4 = 16kHz, vol 15, pan centre
        }
        if (attackTimer) attackTimer--;
        attacking = (attackTimer != 0);

        crouching = (onGround && (pad & KEY_DOWN) && !attacking);
        if (!attacking && !crouching && !hurtFlag) {
            if (mv & KEY_LEFT)  { vx = -WALK; facing = 1; }
            if (mv & KEY_RIGHT) { vx =  WALK; facing = 0; }
        }
        if (hurtFlag) vx = (s16)hurtDir * HSHOVE;     // staggered: coast away from the enemy, no input control
        if ((pad & JUMP_KEYS) && !(prevPad & JUMP_KEYS) && onGround && !attacking && !hurtFlag) {
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
                feetY = (s32)(frow << 4) << 8; vy = 0; onGround = 1; hurtFlag = 0;   // landing ends the stagger
            } else onGround = 0;
        } else onGround = 0;

        // --- camera follows the player ---
        ix = feetX >> 8;
        c = ix - 128;
        if (c < 0) c = 0;
        if (c > CAM_MAX) c = CAM_MAX;
        camX = c;

        // Pit / fall-out: dropped below the level floor -> respawn at the level START (the demo does the same:
        // y>172 -> initX/initY). Re-enters playState; the respawn flag keeps the music playing and skips the
        // heavy audio reload, so it's a quick blink rather than a full restart.
        if ((feetY >> 8) > 240) {
            respawns++;
            spcEffect(4, SFX_HURT, 15 * 16 + 8);
            WaitForVBlank(); setBrightness(0);
            return ST_PLAY;
        }

        // Level-end trigger: the hero reaches the right edge of the 4800px level -> "Thanks for Playing".
        if ((feetX >> 8) >= LVL_PXW - 40) return ST_END;

        // --- stream the next page (ground map + window-local deco map + deco TILES) into its OFF-screen
        //     slot, well before the camera reveals it (~85 frames of runway at walk speed). The deco
        //     tiles are spread over a few VBlanks so per-frame DMA never overruns -> no flicker. The slot
        //     is off-screen until fully loaded. Only start a new load once the previous one finished. ---
        L = camX >> 8;
        wantE = (L & 1) ? (L + 1) : L;
        wantO = (L & 1) ? L : (L + 1);
        if (!streaming) {
            u16 wp = 0xFFFF, ms = 0;
            if (wantE != curA && wantE < N_PAGES)      { wp = wantE; ms = 0;     curA = wantE; }
            else if (wantO != curB && wantO < N_PAGES) { wp = wantO; ms = 0x400; curB = wantO; }
            if (wp != 0xFFFF) {
                streamPage = (u8)wp; streamMapSlot = ms;
                streamSrc = decoPageTiles(wp); streamLen = decoPageTileBytes(wp); streamOff = 0;
                streamTileDst = BG1_TILES + ((wp & 1) ? DECO_SLOT1 : 0);
                streaming = 1; streamMapPending = 1;
            }
        }

        // --- pick animation (hurt > attack > airborne > crouch > run > idle) ---
        // loop=1 cycles forever (idle/run/fall); loop=0 plays once then holds the last frame.
        // The jump RISE and the attack swing are one-shots -- looping them is what made
        // the sword wobble "back and forth" on the way up; only the FALL loops (hair waves).
        if (hurtFlag)         { animF = A_HURT_F;   animN = A_HURT_N;   animSpd = 2; loop = 0; }   // staggered recoil
        else if (attacking)   { animF = A_ATTACK_F; animN = A_ATTACK_N; animSpd = 5; loop = 0; }
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
        if (heroGfx != lastGfx && !streaming) { heroDmaQueue = 2; heroQueuedGfx = heroGfx; lastGfx = heroGfx; }

        // Two 64x64 OBJs make the full hero. Facing right: OBJ0 = left half (gfxoffset 0) at
        // head-64, OBJ4 = right half (gfxoffset 8) at the head. Facing left the halves swap
        // and both hflip, mirroring around the head so the sword points the other way.
        {
            s16 hx = (feetX >> 8) - camX;
            s16 hy = (feetY >> 8) - SPR_OY;
            oamSet(HERO_OAM,     hx - HERO_LEFT_DX, hy, 3, facing, 0, facing ? 8 : 0, 0);
            oamSet(HERO_OAM + 4, hx,                hy, 3, facing, 0, facing ? 0 : 8, 0);
        }
        enemyUpdate();                               // tick enemy animation/AI
        {
            // Demo hurtPlayer(): touching an enemy pops the hero up + shoves it away, plays the hurt SFX, and
            // staggers it (no movement control) until it lands again. Enemies do NOT deplete health or kill --
            // the only death is a pit fall. hurtFlag (passed in) blocks a re-hit while already staggered.
            s8 kb = enemyCombat(attacking, hurtFlag);
            if (kb) {
                hurtFlag = 1; hurtDir = kb; onGround = 0;
                vy = HPOP; vx = (s16)kb * HSHOVE;
                attackTimer = 0;                     // a hit cancels any swing
                spcEffect(4, SFX_HURT, 15 * 16 + 8);
                if (--health == 0) {                 // out of HP -> GAME OVER (bounds the bounce-loop)
                    WaitForVBlank(); setBrightness(0);
                    return ST_GAMEOVER;
                }
            }
        }
        enemyDraw();                                 // position the enemy OBJs (camera-relative)

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
        // Streaming the next page into its OFF-screen slot: frame 0 loads the ground map + window-local
        // deco map (4KB); the following frames DMA the deco TILES ~4KB/frame into the tile slot until done.
        // All within the ~85-frame runway before the slot is revealed, so nothing shows half-loaded.
        if (streamMapPending) {
            dmaCopyVram(groundPage(streamPage), BG0_MAP + streamMapSlot, 2048);
            dmaCopyVram(decoSmap(streamPage),   BG1_MAP + streamMapSlot, 2048);
            streamMapPending = 0;
        } else if (streaming) {
            u16 chunk = streamLen - streamOff; if (chunk > 4096) chunk = 4096;
            dmaCopyVram(streamSrc + streamOff, streamTileDst + (streamOff >> 1), chunk);
            streamOff += chunk;
            if (streamOff >= streamLen) streaming = 0;
        }
        // Upload ONE 2KB half of the queued hero frame this VBlank (top half, then bottom), deferred on
        // page-stream frames (those already move 4KB). Halving the per-frame VBlank DMA stops the overrun.
        {
            // The hero half (2KB) and an enemy frame (2KB, 8 per-row DMAs) in the SAME VBlank overran it
            // when the VBlank was even slightly short (e.g. the redundant idle-frame upload on the first
            // play frames) -> the enemy's later rows spilled into active display = a half-drawn skeleton.
            // So never upload a hero frame AND an enemy frame in one VBlank: the enemy waits a frame.
            u8 heroUp = (heroDmaQueue && !streaming);
            if (heroUp) {
                u8 hh = (heroDmaQueue == 2) ? 0 : 1;
                dmaCopyVram(heroFrameSrc(heroQueuedGfx) + (u32)hh * (HERO_BANDSZ / 2),
                            HERO_VRAM + hh * (HERO_BANDSZ / 4), HERO_BANDSZ / 2);
                heroDmaQueue--;
            }
            enemyVBlankUpload(streaming || heroUp);   // one pending enemy frame, unless streaming/hero own this VBlank
        }

        // Stream the music AFTER the VBlank DMA, in active display. snesmod's spcProcess can be heavy when
        // the song (re)loads; running it BEFORE WaitForVBlank pushed the loop past VBlank start, shortening
        // the VBlank so the enemy's 8-row DMA spilled into active display -> a half-drawn skeleton. In
        // active display its delay is harmless (the next WaitForVBlank re-syncs).
        spcProcess();
    }
    return ST_END;
}
