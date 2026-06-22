/*---------------------------------------------------------------------------------
    enemy.c — (64x64) enemies: 3 types sharing 2 streamed VRAM slots.

    The dynamic-sprite engine can't do 64x64, so enemies upload manually like the hero: each slot is a
    128-wide 4KB band uploaded as two contiguous 2KB halves in VBlank (one DMA setup each -> fits the
    music-shortened VBlank; per-row DMAs starved -> dropped rows). Budgeted AT MOST one band-half per
    VBlank (round-robin), DEFERRED while a level page streams. See [[gothicvania-scroll-latch-flicker]].

    Three enemy types, all sharing the 2 slots (a slot streams whichever type spawned
    in it):
      skeleton  - proximity-rises from the ground (6f + RISE sfx), then shuffles TOWARD the player. 1 OBJ.
      hell-gato - paces back and forth (flips on a timer / at walls + pits). ~85px wide -> 2-OBJ metasprite
                  (spans the full band, left+right like the hero). Doesn't track the player.
      ghost     - floats at a fixed X, bobs vertically, turns to FACE the player. 1 OBJ (left 64x64).
    Combat: the hero's sword kills any type (-> shared death poof + KILL sfx); contact hurts the hero.
---------------------------------------------------------------------------------*/
#include "game.h"

// Per slot: OBJ tile NAME (left 64x64 of the band; the gato's right half = name+8) + band VRAM word base.
static const u16 enemyName[ENEMY_SLOTS]    = {256, 384};
static const u16 enemyDstBase[ENEMY_SLOTS] = {0x1000, 0x1800};

// enemy type
#define ET_SKEL  0
#define ET_GATO  1
#define ET_GHOST 2
static const u8 enemyPal[3]   = {ENEMY_PAL, GATO_PAL, GHOST_PAL};  // CGRAM palette per type
static const u8 enemyHalfW[3] = {22, 38, 14};                       // hittable half-width per type (px)

// per-enemy state machine
#define EN_RISE  1   // skeleton only: emerging from the ground
#define EN_WALK  2   // active: skel shuffles, gato paces, ghost bobs
#define EN_DYING 3   // playing the shared death poof, then despawns

typedef struct {
    u8  active;
    u8  type;        // ET_*
    u8  state;       // EN_*
    u8  frame;       // animation frame: per-type strip index, OR absolute skel index while EN_DYING
    u8  uploaded;    // frame resident in this slot's VRAM (255 = none yet -> stay hidden)
    u8  timer;       // animation tick counter
    u8  facing;      // 0 = no hflip, 1 = hflip
    u8  wsub;        // sub-pixel accumulator (gato X move / ghost Y bob)
    s16 wx, wy;      // world position (px). skel/gato: feet on ground. ghost: current (bobbing) centre-ish.
    s16 baseY;       // ghost: bob origin (top of the bob); unused by skel/gato
    u16 aux;         // gato: turn-timer countdown.  ghost: low bit = bob direction (0 down / 1 up)
} Enemy;
static Enemy en[ENEMY_SLOTS];

static u8  upRR;            // round-robin cursor for the upload budget
static u8  upSlot = 255;    // slot whose band is mid-upload (255 = none)
static u8 *upSrc;           // ROM src of the band being uploaded (latched at the top half so both halves match)

// Combined spawn table: each fires ONCE when the player gets near. tx = tile X.
typedef struct { u16 tx; u8 ty, type; } Spawn;   // tx is a tile col 0..299, so u16 (not u8 -> 284 wraps)
static const Spawn spawns[] = {
    {10,12,ET_SKEL},{17,12,ET_SKEL},{80,12,ET_SKEL},{147,12,ET_SKEL},{162,12,ET_SKEL},
    {200,12,ET_SKEL},{210,12,ET_SKEL},{244,12,ET_SKEL},{254,12,ET_SKEL},{270,12,ET_SKEL},
    {53,11,ET_GATO},{86,11,ET_GATO},{147,11,ET_GATO},{201,11,ET_GATO},
    {111,7,ET_GHOST},{173,6,ET_GHOST},{220,7,ET_GHOST},{263,7,ET_GHOST},{284,7,ET_GHOST},
};
#define N_SPAWN (sizeof(spawns) / sizeof(spawns[0]))
static u8 spawned[N_SPAWN];

#define GROUND_Y      192    // tile row 12 top: skeleton/gato feet line
#define TRIG_TILES    8      // a spawner fires when the player is within this many tiles
#define DESPAWN_PX    200    // free a slot once its enemy is this far from the player (left behind)
#define SKEL_REACH_PX 10     // skeleton stops shuffling once this close to the player
#define DEATH_RISE_PX 16     // the death flame draws this much HIGHER than the feet (enemy.y-16)
#define GHOST_DRAW_OY 46     // ghost box-top = wy - this (so the floating body sits around wy)
#define GHOST_BOB_AMP 50     // ghost vertical bob range (px), down from baseY (y -> y+50)
static s16 SKEL_WALK_SPD, GATO_SPD, GHOST_BOB_SPD;   // 8-bit sub-pixel speeds, region-scaled (enemyInit)
static u16 GATO_TURN;                                 // gato patrol half-period (frames), region-scaled

// ROM address of skeleton frame f (also the shared death poof, frames 14..18). 128-wide 4KB bands, split
// into 3 banks so no 4KB frame crosses a bank (a DMA can't).
u8 *skeletonFrameSrc(u8 f)
{
    if (f < 7)  return (u8 *)(&skel_a) + (u32)f * 4096;
    if (f < 14) return (u8 *)(&skel_b) + (u32)(f - 7) * 4096;
    return (u8 *)(&skel_c) + (u32)(f - 14) * 4096;
}

// ROM address of the band to upload for slot i: the death poof (skeleton strip) while dying, else the
// enemy's own type strip. gato/ghost are single 16KB banks (4 frames x 4KB).
static u8 *enemyBandSrc(u8 i)
{
    if (en[i].state == EN_DYING) return skeletonFrameSrc(en[i].frame);  // frame = absolute skel death index
    if (en[i].type == ET_GATO)   return (u8 *)(&gato)  + (u32)en[i].frame * 4096;
    if (en[i].type == ET_GHOST)  return (u8 *)(&ghost) + (u32)en[i].frame * 4096;
    return skeletonFrameSrc(en[i].frame);
}

static s8 freeSlot(void)
{
    u8 i;
    for (i = 0; i < ENEMY_SLOTS; i++) if (!en[i].active) return (s8)i;
    return -1;
}

static void hideSlot(u8 i)
{
    u8 id = ENEMY_OAM_BASE + i * 8;
    oamSetVisible(id, OBJ_HIDE); oamSetVisible(id + 4, OBJ_HIDE);
}

static void spawnEnemy(u8 slot, const Spawn *s)
{
    en[slot].active = 1;
    en[slot].type = s->type;
    en[slot].uploaded = 255;                 // hidden until its first frame is uploaded
    en[slot].timer = 0;
    en[slot].wsub = 0;
    en[slot].aux = 0;
    en[slot].wx = (s16)(s->tx * 16 + 8);
    if (s->type == ET_SKEL) {
        en[slot].state = EN_RISE; en[slot].frame = SK_RISE_F; en[slot].facing = 1;
        en[slot].wy = GROUND_Y;
        spcEffect(4, SFX_RISE, 15 * 16 + 8);
    } else if (s->type == ET_GATO) {
        en[slot].state = EN_WALK; en[slot].frame = GATO_WALK_F; en[slot].facing = 0;
        en[slot].wy = GROUND_Y; en[slot].aux = GATO_TURN;
    } else {  // ET_GHOST
        en[slot].state = EN_WALK; en[slot].frame = GHOST_FLOAT_F; en[slot].facing = 0;
        en[slot].baseY = (s16)(s->ty * 16); en[slot].wy = en[slot].baseY;
    }
}

// Load the three enemy palettes, clear the pool + spawn flags. Enemies stream in their first frame via
// enemyVBlankUpload during play, so nothing is uploaded here.
void enemyInit(void)
{
    u8 i;
    setPalette(&skelpal,  128 + ENEMY_PAL * 16, (&skelpalend  - &skelpal));
    setPalette(&gatopal,  128 + GATO_PAL  * 16, (&gatopalend  - &gatopal));
    setPalette(&ghostpal, 128 + GHOST_PAL * 16, (&ghostpalend - &ghostpal));
    for (i = 0; i < ENEMY_SLOTS; i++) {          // BOTH OBJs per slot must be OBJ_LARGE (64x64): without
        u8 id = ENEMY_OAM_BASE + i * 8;          // this the OBJ defaults to 32x32 and shows only the sprite's
        en[i].active = 0;                        // top-left quarter (the gato rendered as a thin sliver).
        oamSetEx(id,     OBJ_LARGE, OBJ_HIDE);
        oamSetEx(id + 4, OBJ_LARGE, OBJ_HIDE);
    }
    for (i = 0; i < N_SPAWN; i++) spawned[i] = 0;
    upRR = 0;
    upSlot = 255;
    SKEL_WALK_SPD = snes_50hz ? 154 : 128;   // ~0.5 px/frame shamble (region-equal wall-clock)
    GATO_SPD      = snes_50hz ? 461 : 384;   // ~1.5 px/frame pace 
    GHOST_BOB_SPD = snes_50hz ? 256 : 213;   // ~0.83 px/frame bob 
    GATO_TURN     = snes_50hz ? 167 : 200;   // ~3.3s patrol half-period 
}

// Spawn (proximity) + tick AI/animation. Main-loop context, so SFX (spcEffect) is fine here.
void enemyUpdate(void)
{
    s16 px = (s16)(feetX >> 8);
    s16 ptile = px >> 4;                       // tile col 0..299 -> s16, NOT u8 (the level is 300 wide)
    u8  i;

    // 1. one-shot proximity spawn
    for (i = 0; i < N_SPAWN; i++) {
        s16 d;
        if (spawned[i]) continue;
        d = (s16)spawns[i].tx - ptile; if (d < 0) d = -d;
        if (d <= TRIG_TILES) {
            s8 slot = freeSlot();
            if (slot >= 0) { spawnEnemy((u8)slot, &spawns[i]); spawned[i] = 1; }
        }
    }

    // 2. per-enemy state machine
    for (i = 0; i < ENEMY_SLOTS; i++) {
        s16 ex, dx;
        if (!en[i].active) continue;

        if (en[i].state == EN_DYING) {                 // play the death poof once, then free the slot
            if (++en[i].timer >= 6) {
                u8 rel = (u8)(en[i].frame - SK_DEATH_F + 1);
                en[i].timer = 0;
                if (rel >= SK_DEATH_N) { en[i].active = 0; hideSlot(i); }
                else en[i].frame = (u8)(SK_DEATH_F + rel);
            }
            continue;
        }

        ex = en[i].wx;
        dx = ex - px; if (dx < 0) dx = -dx;
        if (dx > DESPAWN_PX) { en[i].active = 0; hideSlot(i); continue; }   // wandered off -> free the slot

        if (en[i].type == ET_SKEL) {
            if (en[i].state == EN_RISE) {
                if (++en[i].timer >= 6) {
                    u8 rel = (u8)(en[i].frame - SK_RISE_F + 1);
                    en[i].timer = 0;
                    if (rel >= SK_RISE_N) { en[i].state = EN_WALK; en[i].frame = SK_WALK_F; }
                    else en[i].frame = (u8)(SK_RISE_F + rel);
                }
            } else {  // EN_WALK: shuffle toward the player (sprite faces LEFT by default -> flip to face right)
                en[i].facing = (px < ex) ? 0 : 1;
                if (dx > SKEL_REACH_PX) {
                    u16 t = (u16)en[i].wsub + (u16)SKEL_WALK_SPD;
                    if (t > 0xFF) en[i].wx += (px < ex) ? -1 : 1;
                    en[i].wsub = (u8)t;
                }
                if (++en[i].timer >= 6) { en[i].timer = 0;
                    en[i].frame = (u8)(SK_WALK_F + ((en[i].frame - SK_WALK_F + 1) & (SK_WALK_N - 1))); }
            }
        } else if (en[i].type == ET_GATO) {
            // pace back and forth: flip on the turn timer OR when a wall / pit is just ahead. The gato art
            // faces LEFT by default (no hflip), so facing 0 (no flip) = moving LEFT, facing 1 = moving right.
            // (Had this inverted -> the cat moonwalked, moving one way while facing/animating the other.)
            s16 dir = en[i].facing ? 1 : -1;
            s16 aheadCol = (en[i].wx + dir * 36) >> 4;
            if (en[i].aux == 0 ||
                cellv(aheadCol, 11) == 1 ||            // wall ahead at body height
                cellv(aheadCol, 12) != 1) {            // no floor ahead (pit / spikes / level edge)
                en[i].facing ^= 1; dir = -dir; en[i].aux = GATO_TURN;
            } else en[i].aux--;
            { u16 t = (u16)en[i].wsub + (u16)GATO_SPD;
              if (t > 0xFF) en[i].wx += dir;
              en[i].wsub = (u8)t; }
            if (++en[i].timer >= 6) { en[i].timer = 0;
                en[i].frame = (u8)(GATO_WALK_F + ((en[i].frame - GATO_WALK_F + 1) & (GATO_WALK_N - 1))); }
        } else {  // ET_GHOST: bob vertically, turn to face the player (faces RIGHT by default -> flip if player left)
            en[i].facing = (px < ex) ? 1 : 0;
            { u16 t = (u16)en[i].wsub + (u16)GHOST_BOB_SPD;
              if (t > 0xFF) en[i].wy += (en[i].aux & 1) ? -1 : 1;
              en[i].wsub = (u8)t; }
            if (en[i].wy >= en[i].baseY + GHOST_BOB_AMP) en[i].aux |= 1;        // hit bottom -> rise
            else if (en[i].wy <= en[i].baseY)            en[i].aux &= (u16)~1u;  // hit top    -> fall
            if (++en[i].timer >= 8) { en[i].timer = 0;
                en[i].frame = (u8)(GHOST_FLOAT_F + ((en[i].frame - GHOST_FLOAT_F + 1) & (GHOST_FLOAT_N - 1))); }
        }
    }
}

// Combat, both directions. While the hero swings, any enemy within sword reach IN FRONT of him dies (+ KILL
// sfx). Otherwise an enemy OVERLAPPING the hero (and he's not invulnerable) hurts him -> returns the knockback
// dir (+1 right / -1 left / 0 = no hit). Reach is EDGE-based: the sword span vs the enemy's [wx +- halfW]
// body (per type), matching the hitbox-vs-body overlap. facing 0 = right (reaches +dx), 1 = left.
#define SWORD_FWD  56     // sword-tip reach ahead of the hero centre (px)
#define SWORD_BACK  8     // slight coverage just behind the hero centre
s8 enemyCombat(u8 attacking, u8 invuln)
{
    s16 px = (s16)(feetX >> 8), py = (s16)(feetY >> 8);
    s8  hurt = 0;
    u8  i;
    for (i = 0; i < ENEMY_SLOTS; i++) {
        s16 dx, adx, dy, ady; u8 hw;
        if (!en[i].active || en[i].state == EN_DYING) continue;   // a dying enemy is harmless + not re-killable
        hw = enemyHalfW[en[i].type];
        dx = en[i].wx - px; adx = dx < 0 ? -dx : dx;
        dy = en[i].wy - py; ady = dy < 0 ? -dy : dy;
        if (attacking && ady <= 30 &&
            (facing ? (dx > -(SWORD_FWD + hw) && dx < (SWORD_BACK + hw))
                    : (dx > -(SWORD_BACK + hw) && dx < (SWORD_FWD + hw)))) {
            en[i].state = EN_DYING; en[i].frame = SK_DEATH_F; en[i].timer = 0;
            spcEffect(4, SFX_KILL, 15 * 16 + 8);
            continue;
        }
        if (!invuln && adx < (hw + 4) && ady < 44)            // overlapping the hero -> contact damage
            hurt = (dx > 0) ? -1 : 1;                          // shove the hero away from the enemy
    }
    return hurt;
}

// Position the enemy OBJs (main loop). Hidden until the first frame is in VRAM, and when off-screen.
void enemyDraw(void)
{
    u8 i;
    for (i = 0; i < ENEMY_SLOTS; i++) {
        u8  id0 = ENEMY_OAM_BASE + i * 8, id1 = id0 + 4;
        u8  fc, pal, twoObj;
        s16 sy;
        if (!en[i].active || en[i].uploaded == 255) { hideSlot(i); continue; }
        fc  = en[i].facing;
        pal = (en[i].state == EN_DYING) ? ENEMY_PAL : enemyPal[en[i].type];
        twoObj = (en[i].type == ET_GATO && en[i].state != EN_DYING);

        if (en[i].state == EN_DYING)     sy = en[i].wy - SK_FEET_BY - DEATH_RISE_PX;
        else if (en[i].type == ET_GHOST) sy = en[i].wy - GHOST_DRAW_OY;
        else                             sy = en[i].wy - SK_FEET_BY;   // skel/gato feet on the ground

        if (twoObj) {                                  // gato = 2-OBJ metasprite (128 wide, centred on wx)
            s16 lx = en[i].wx - (s16)camX - 64;
            if (lx < -64 || lx > 256) { hideSlot(i); continue; }
            oamSetVisible(id0, OBJ_SHOW); oamSetVisible(id1, OBJ_SHOW);
            oamSet(id0, (u16)lx,        (u16)sy, 2, fc, 0, enemyName[i] + (fc ? 8 : 0), pal);
            oamSet(id1, (u16)(lx + 64), (u16)sy, 2, fc, 0, enemyName[i] + (fc ? 0 : 8), pal);
        } else {                                       // skel / ghost / death poof = 1 OBJ (left 64x64)
            s16 sx = en[i].wx - (s16)camX - 32;
            if (sx < -64 || sx > 256) { hideSlot(i); continue; }
            oamSetVisible(id0, OBJ_SHOW); oamSetVisible(id1, OBJ_HIDE);
            oamSet(id0, (u16)sx, (u16)sy, 2, fc, 0, enemyName[i], pal);
        }
    }
}

// Upload one 2KB half (hh=0 top 4 grid-rows / 1 bottom) of a slot's 128-wide band as a SINGLE contiguous DMA.
static void enemyUploadHalf(u8 slot, u8 *src, u8 hh)
{
    dmaCopyVram(src + (u32)hh * 2048, enemyDstBase[slot] + (u16)hh * 0x400, 2048);
}

// One half per VBlank: finish the slot in progress (bottom half), else start the next dirty slot (top half).
// Skipped when streaming/hero own this VBlank. The SRC pointer is latched at the top half so both halves come
// from the SAME frame even if the enemy changes frame/type/dies between the two VBlanks (no torn band).
void enemyVBlankUpload(u8 busy)
{
    u8 n, i;
    if (busy) return;
    if (upSlot != 255) {                          // second (bottom) half of the band in progress
        if (en[upSlot].active) enemyUploadHalf(upSlot, upSrc, 1);
        en[upSlot].uploaded = en[upSlot].frame;   // done (mark even if despawned -> not stuck dirty)
        upSlot = 255;
        return;
    }
    for (n = 0; n < ENEMY_SLOTS; n++) {           // first (top) half of the next dirty slot
        i = upRR;
        upRR = (u8)((upRR + 1) & (ENEMY_SLOTS - 1));
        if (en[i].active && en[i].uploaded != en[i].frame) {
            upSrc = enemyBandSrc(i);
            enemyUploadHalf(i, upSrc, 0);
            upSlot = i;
            return;
        }
    }
}
