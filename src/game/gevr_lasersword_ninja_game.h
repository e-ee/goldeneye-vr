#ifndef GEVR_LASERSWORD_NINJA_GAME_H
#define GEVR_LASERSWORD_NINJA_GAME_H

#include <ultra64.h>
#include <bondtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void gevrLnGameReset(void);
void gevrLnGameTick(s32 ticks60);
Gfx *gevrLnDrawWorld(Gfx *gdl);
Gfx *gevrLnDrawFirstPerson(Gfx *gdl);
s32 gevrLnInterceptPlayerDamage(f32 damageAmount, f32 vecX, f32 vecZ);
s32 gevrLnSuppressNormalFire(void);
s32 gevrLnHideNormalGun(void);
s32 gevrLnLasersActive(void);
s32 gevrLnCanPickupProp(PropRecord *prop);
s32 gevrLnAllyThink(ChrRecord *ally);
s32 gevrLnIsMeleeNpc(ChrRecord *chr);
void gevrLnNoteNpcHand(ChrRecord *chr, s32 hand, const Mtxf *handMtxCam);

#ifdef __cplusplus
}
#endif

#endif
