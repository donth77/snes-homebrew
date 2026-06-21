/*---------------------------------------------------------------------------------
    game.c — shared world/player state for the gothicvania state machine.

    These globals are written by the play loop (play.c) and read across the renderer
    helpers (level.c) and the title/end screens. Defined here once; declared in game.h.
---------------------------------------------------------------------------------*/
#include "game.h"

u16 camX = 0;
u16 curA = 0, curB = 1;

s32 feetX, feetY;                                      // 8.8 fixed-point pixels
s16 vx, vy;
u8  onGround = 0, facing = 0;
u16 jumpDir = 0;                                       // horizontal dir held at takeoff -> jump-arc momentum
