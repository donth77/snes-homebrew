/*---------------------------------------------------------------------------------
    game.c — shared world/player state for the gothicvania state machine.

    These globals are written by the play loop (play.c) and read across the renderer
    helpers (level.c) and the title/end screens. Defined here once; declared in game.h.
---------------------------------------------------------------------------------*/
#include "game.h"

u16 camX = 0;
u16 curA = 0, curB = 1;

u16 respawns = 0;                                      // pit-fall respawns (hero dropped off the level -> back to start)
u8  respawn = 0;                                       // set by main.c when re-entering PLAY after a respawn

s32 feetX, feetY;                                      // 8.8 fixed-point pixels
s16 vx, vy;
u8  onGround = 0, facing = 0;
u16 jumpDir = 0;                                       // horizontal dir held at takeoff -> jump-arc momentum
