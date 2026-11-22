#ifndef GEVR_LASERSWORD_GAME_H
#define GEVR_LASERSWORD_GAME_H

#include <ultra64.h>
#include <bondtypes.h>

void gevrLsGameReset(void);
void gevrLsGameTick(s32 ticks60);
s32 gevrLsHandActive(s32 hand);
Gfx *gevrLsDrawHand(Gfx *gdl, s32 hand);
Gfx *gevrLsDrawWorld(Gfx *gdl);
s32 gevrLsInterceptPlayerDamage(f32 damageAmount, f32 vecX, f32 vecZ);
s32 gevrLsNpcLasersActive(void);

#endif
