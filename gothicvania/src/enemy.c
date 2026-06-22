/*---------------------------------------------------------------------------------
    enemy.c — demo-size (64x64) enemies: VRAM packing, per-row upload, pool + AI.

    The dynamic-sprite engine can't do 64x64 (sprite.h: "64pix size is not supported"),
    so enemies upload manually like the hero. A 64x64 sprite reads its 8x8 tiles across the
    16-wide OBJ tile grid (stride 16), so two 64x64 sprites share each 4KB grid band -- a LEFT
    one (cols 0..7) and a RIGHT one (cols 8..15). gfx4snes -s 8 frame data is plain 8-wide
    raster (64 tiles = 2KB, rows contiguous), so we DMA it a row at a time (8 x 256 bytes) into
    the slot's half of its band -- 4 slots in the 0x0C00..0x1C00 band, no blank-pad waste.

    Frame upload is the flicker-risk part (a 2KB per-row DMA in VBlank): budgeted exactly like
    the hero -- AT MOST ONE enemy frame per VBlank (round-robin), DEFERRED while a level page
    streams. See [[gothicvania-scroll-latch-flicker]] for the overrun class.

    Skeleton AI (the demo's game.js): proximity-triggered one-shot spawners along the level rise
    (6f + RISE sfx) from the ground, then shuffle toward the player (8f walk loop, flips to face).
    Combat (attack kills / contact hurts) + the ghost & hell-gato types layer in next.
---------------------------------------------------------------------------------*/
#include "game.h"

// Per slot: OBJ tile NAME (the LEFT 64x64 of a 128-wide band) + the band's VRAM word base. Each band is a
// contiguous 0x800-word (4KB) block -> two 2KB halves DMA into it (dst base + hh*0x400). Names >= 256
// (OBSEL gap 0 -> tiles 256..511 contiguous; oamSet routes the 9th name bit to the OAM high table).
static const u16 enemyName[ENEMY_SLOTS]    = {256, 384};
static const u16 enemyDstBase[ENEMY_SLOTS] = {0x1000, 0x1800};

#define EN_RISE  1
#define EN_WALK  2

typedef struct {
    u8  active;
    u8  state;
    u8  frame;       // current animation frame (absolute index into the skeleton strip)
    u8  uploaded;    // frame resident in this slot's VRAM (255 = none yet -> keep hidden until uploaded)
    u8  timer;       // animation tick counter
    u8  facing;      // 0 = right, 1 = left (hflip)
    u8  wsub;        // sub-pixel X accumulator -- 16-bit + a byte, NOT 32-bit fixed point, so the per-frame
    s16 wx, wy;      // loop stays light enough that a single-shot per-row upload fits the VBlank. wx/wy = world feet px.
} Enemy;
static Enemy en[ENEMY_SLOTS];
static u8 upRR;            // round-robin cursor for the upload budget
static u8 upSlot = 255;    // slot whose frame is mid-upload (255 = none); its frame latched in upFrame
static u8 upFrame;         // the frame being uploaded across its two halves (so they don't tear)

// Skeleton spawners: the demo's hardcoded tile-X list, each fires ONCE when the player gets near.
#define N_SKEL_SPAWN 10
static const u16 skelSpawnTX[N_SKEL_SPAWN] = {10, 17, 80, 147, 162, 200, 210, 244, 254, 270};
static u8 skelSpawned[N_SKEL_SPAWN];

#define SKEL_GROUND_Y   192    // tile row 12 (the demo's skeleton spawn y); main graveyard ground line
#define SKEL_TRIG_TILES 8      // a spawner rises when the player is within this many tiles
#define SKEL_DESPAWN_PX 200    // free a slot once its skeleton is this far from the player (left behind)
#define SKEL_REACH_PX   10     // stop shuffling once this close to the player (no combat yet -> don't overlap)
static s16 SKEL_WALK_SPD;      // 8.8 px/frame, region-scaled (set in enemyInit)

// ROM address of skeleton frame f: a 128-wide band = 4096 bytes (16-wide from gfx4snes -s 64 -R, skeleton
// in the left 64x64, right 64 blank). Split into two banks (a = frames 0..6, b = 7..13) so no 4KB frame
// crosses a bank boundary (a DMA can't).
u8 *skeletonFrameSrc(u8 f)
{
    if (f < 7) return (u8 *)(&skel_a) + (u32)f * 4096;
    return (u8 *)(&skel_b) + (u32)(f - 7) * 4096;
}

static s8 freeSlot(void)
{
    u8 i;
    for (i = 0; i < ENEMY_SLOTS; i++) if (!en[i].active) return (s8)i;
    return -1;
}

static void spawnSkeleton(u8 slot, s16 wx)
{
    en[slot].active = 1;
    en[slot].state = EN_RISE;
    en[slot].frame = SK_RISE_F;
    en[slot].uploaded = 255;              // hidden until its first frame is uploaded
    en[slot].timer = 0;
    en[slot].facing = 1;
    en[slot].wx = wx;
    en[slot].wy = SKEL_GROUND_Y;
    en[slot].wsub = 0;
}

// Load the enemy palette, clear the pool + spawn flags. Enemies now appear dynamically (proximity), so
// nothing is uploaded here -- the first frame of each spawn streams in via enemyVBlankUpload during play.
void enemyInit(void)
{
    u8 i;
    setPalette(&skelpal, 128 + ENEMY_PAL * 16, (&skelpalend - &skelpal));
    for (i = 0; i < ENEMY_SLOTS; i++) {
        en[i].active = 0;
        oamSetEx(ENEMY_OAM_BASE + i * 4, OBJ_LARGE, OBJ_HIDE);
    }
    for (i = 0; i < N_SKEL_SPAWN; i++) skelSpawned[i] = 0;
    upRR = 0;
    upSlot = 255;
    SKEL_WALK_SPD = snes_50hz ? 154 : 128;   // ~0.5 px/frame shamble (region-equal wall-clock)
}

// Spawn (proximity) + tick AI/animation. Runs in the main loop, so SFX (spcEffect) is fine here.
void enemyUpdate(void)
{
    s16 px = (s16)(feetX >> 8);            // player world X (px)
    u8  ptile = (u8)(px >> 4);
    u8  i;

    // 1. one-shot proximity spawn
    for (i = 0; i < N_SKEL_SPAWN; i++) {
        s16 d;
        if (skelSpawned[i]) continue;
        d = (s16)skelSpawnTX[i] - (s16)ptile;
        if (d < 0) d = -d;
        if (d <= SKEL_TRIG_TILES) {
            s8 slot = freeSlot();
            if (slot >= 0) {               // (no free slot -> leave un-triggered, retry next frame)
                spawnSkeleton((u8)slot, (s16)(skelSpawnTX[i] * 16 + 8));
                skelSpawned[i] = 1;
                spcEffect(4, SFX_RISE, 15 * 16 + 8);
            }
        }
    }

    // 2. per-enemy state machine
    for (i = 0; i < ENEMY_SLOTS; i++) {
        s16 ex, dx;
        if (!en[i].active) continue;
        ex = en[i].wx;
        dx = ex - px; if (dx < 0) dx = -dx;
        if (dx > SKEL_DESPAWN_PX) {        // wandered far from the player -> free the slot (one-shot, won't respawn)
            en[i].active = 0;
            oamSetEx(ENEMY_OAM_BASE + i * 4, OBJ_LARGE, OBJ_HIDE);
            continue;
        }
        if (en[i].state == EN_RISE) {
            if (++en[i].timer >= 6) {
                u8 rel = (u8)(en[i].frame - SK_RISE_F + 1);
                en[i].timer = 0;
                if (rel >= SK_RISE_N) { en[i].state = EN_WALK; en[i].frame = SK_WALK_F; }
                else en[i].frame = (u8)(SK_RISE_F + rel);
            }
        } else {  // EN_WALK
            // the skeleton sprite faces LEFT by default, so hflip (facing=1) = faces RIGHT. Face the player:
            // player on the left (px < ex) -> face left = NO flip (0); player on the right -> flip (1).
            en[i].facing = (px < ex) ? 0 : 1;
            if (dx > SKEL_REACH_PX) {                    // accumulate sub-pixels, carry to a whole pixel
                u16 t = (u16)en[i].wsub + (u16)SKEL_WALK_SPD;
                if (t > 0xFF) en[i].wx += (px < ex) ? -1 : 1;
                en[i].wsub = (u8)t;
            }
            if (++en[i].timer >= 6) {
                en[i].timer = 0;
                en[i].frame = (u8)(SK_WALK_F + ((en[i].frame - SK_WALK_F + 1) & (SK_WALK_N - 1)));
            }
        }
    }
}

// Combat, both directions. While the hero swings, any enemy within sword reach IN FRONT of him dies (+ kill
// SFX). Otherwise, an enemy OVERLAPPING the hero (and the hero not currently invulnerable) damages him --
// returns the knockback direction (+1 push right, -1 push left, 0 = no hit) for play.c to apply. facing 0 =
// right, 1 = left. Called each frame from play.c with the attack + invuln state.
s8 enemyCombat(u8 attacking, u8 invuln)
{
    s16 px = (s16)(feetX >> 8), py = (s16)(feetY >> 8);
    s8  hurt = 0;
    u8  i;
    for (i = 0; i < ENEMY_SLOTS; i++) {
        s16 dx, adx, dy, ady;
        if (!en[i].active) continue;
        dx = en[i].wx - px; adx = dx < 0 ? -dx : dx;
        dy = en[i].wy - py; ady = dy < 0 ? -dy : dy;
        if (attacking && ady <= 24 && (facing ? (dx > -50 && dx < 8) : (dx > -8 && dx < 50))) {
            en[i].active = 0;                                   // killed by the swing
            oamSetEx(ENEMY_OAM_BASE + i * 4, OBJ_LARGE, OBJ_HIDE);
            spcEffect(4, SFX_KILL, 15 * 16 + 8);
            continue;
        }
        if (!invuln && adx < 22 && ady < 44)                   // overlapping the hero -> contact damage
            hurt = (dx > 0) ? -1 : 1;                           // shove the hero away from the enemy
    }
    return hurt;
}

// Position the enemy OBJs (main loop). Hidden until the first frame is in VRAM, and when off-screen, so a
// freshly-spawned or culled enemy never shows stale tiles.
void enemyDraw(void)
{
    u8 i;
    for (i = 0; i < ENEMY_SLOTS; i++) {
        u8 id = ENEMY_OAM_BASE + i * 4;
        s16 sx, sy;
        if (!en[i].active || en[i].uploaded == 255) { oamSetVisible(id, OBJ_HIDE); continue; }
        sx = en[i].wx - (s16)camX - 32;   // centre the 64-box on the world x
        sy = en[i].wy - SK_FEET_BY;        // feet on the ground line
        if (sx < -64 || sx > 256) { oamSetVisible(id, OBJ_HIDE); continue; }
        oamSetVisible(id, OBJ_SHOW);
        oamSet(id, (u16)sx, (u16)sy, 2, en[i].facing, 0, enemyName[i], ENEMY_PAL);
    }
}

// Upload one 2KB half (hh = 0 top 4 grid-rows, 1 bottom) of a 128-wide frame band as a SINGLE contiguous
// DMA -- the hero's trick. One DMA setup (not 8 strided per-row ones), so it fits the music-shortened
// VBlank. No per-row starvation -> no head-cut / missing parts.
static void enemyUploadHalf(u8 slot, u8 *src, u8 hh)
{
    dmaCopyVram(src + (u32)hh * 2048, enemyDstBase[slot] + (u16)hh * 0x400, 2048);
}

// One half per VBlank: finish the slot in progress (bottom half), else start the next dirty slot (top
// half). Skipped when streaming/hero own this VBlank. The frame is latched (upFrame) so both halves are
// the SAME frame -> at most a 1-VBlank top/bottom seam, like the hero (invisible). Round-robin over slots.
void enemyVBlankUpload(u8 busy)
{
    u8 n, i;
    if (busy) return;
    if (upSlot != 255) {                          // second (bottom) half of the frame in progress
        if (en[upSlot].active) enemyUploadHalf(upSlot, skeletonFrameSrc(upFrame), 1);
        en[upSlot].uploaded = upFrame;            // done (mark even if despawned -> not stuck dirty)
        upSlot = 255;
        return;
    }
    for (n = 0; n < ENEMY_SLOTS; n++) {           // first (top) half of the next dirty slot
        i = upRR;
        upRR = (u8)((upRR + 1) & (ENEMY_SLOTS - 1));
        if (en[i].active && en[i].uploaded != en[i].frame) {
            upFrame = en[i].frame;
            enemyUploadHalf(i, skeletonFrameSrc(upFrame), 0);
            upSlot = i;
            return;
        }
    }
}
