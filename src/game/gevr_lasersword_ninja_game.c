#include "gevr_lasersword_ninja_game.h"
#include "gevr_lasersword_ninja.h"
#include "gevr_lasersword.h"
#include "gevr_ragdoll.h"
#include "gevr_ally.h"
#include "pdphysics.h"

#include <math.h>
#include <string.h>
#include <bondgame.h>
#include <bondconstants.h>
#include <bondaicommands.h>
#include "chr.h"
#include "chraction.h"
#include "model.h"
#include "player.h"
#include "lv.h"
#include "bg.h"
#include "dyn.h"
#include "bondview.h"
#include "stan.h"
#include "matrixmath.h"
#include "random.h"
#include "chrai.h"

extern int VrLaserswordNinjaMode, VrLaserswordHand, VrLaserswordColor, VrLaserswordStyle, VrNinjaSword, VrNinjaBoomerang;
extern int VrLaserswordForcePowers, VrLaserswordCatchProjectiles, VrLnPerfectParry, VrLnBladeTrails, VrLnAudio;
extern int VrNinjaShield, VrNinjaRicochet, VrNinjaShadowStep, VrNinjaBulletSlice;
extern int VrRivalNpc, VrRivalInNormalMode, VrRivalScale, VrNinjaNpc, VrNinjaInNormalMode, VrAllyLasersword;
extern int VrLaserswordLasers, VrLaserswordPickupGuns, VrNinjaPickupGuns;
extern s32 g_gevrStereo, g_gevrExtraPass, g_StageNum, g_ClockTimer, g_GlobalTimer;
extern f32 D_800364CC;
extern s32 g_PlayerIsInTank;
extern s32 gevrLnSelectedPower, gevrLnSelectedNinjaSlot;

extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern int getPlayerCount(void);
extern s32 get_cur_playernum(void);
extern s32 lvlGetControlsLockedFlag(void);
extern s32 gevrSpectating(void);
extern s32 gevrCoopLocalDowned(void);
extern s32 gevrLnGetGripAxes(s32 ctrl, f32 pos[3], f32 right[3], f32 up[3], f32 back[3]);
extern s32 gevrLnGetHandPoseWorld(s32 ctrl, f32 posW[3], f32 rightW[3], f32 upW[3], f32 fwdW[3]);
extern s32 gevrLnGetHandVelocityWorld(s32 ctrl, f32 velCmS[3]);
extern int bondinvAddInvItem(ITEM_IDS item);
extern s32 currentPlayerEquipWeaponWrapper(GUNHAND hand, s32 next_weapon);
extern s32 getCurrentPlayerWeaponId(GUNHAND hand);
extern float get_analog_value(int hand_index, const char *input_name);
extern int vr_haptics_ready(void);
extern s32 trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
extern s32 plot_course_for_actor(ChrRecord *chr, coord3d *pos, StandTile *tile, SPEED speed);
extern StandTile *stanFindGroundAtCyl(coord3d *pos, f32 radius, u8 *rooms, StandTile *prefer, f32 *groundy, s32 *incentre);
extern Gfx *combiner_bayer_lod_perspective(Gfx *gdl);

/* -------------------------------------------------------------------------- */
/* State                                                                      */
/* -------------------------------------------------------------------------- */

#define LN_MAX_CUTS      8
#define LN_MAX_SPARKS    12
#define LN_MAX_TRACERS   8
#define LN_MAX_LIGHTNING 3
#define LN_MAX_SPECIAL   8
#define LN_MAX_SMOKE     12
#define LN_TRAIL_SAMPLES 7
#define LN_VR_HIT_SLOTS  16

enum {
    LN_NPC_RIVAL = 1,
    LN_NPC_NINJA = 2,
    LN_NPC_ALLY_LASERSWORD = 3
};

/* World-space motion ribbon samples pushed once per tick, transformed per eye */
typedef struct {
    int count;
    float hilt[LN_TRAIL_SAMPLES][3];
    float tip[LN_TRAIL_SAMPLES][3];
    float age[LN_TRAIL_SAMPLES];
} LnBladeTrail;

typedef struct {
    int used;
    float pos[3];
    float vel[3];
    float radius;
    float age;
    float maxAge;
} LnSmokeCloud;

typedef struct {
    int used;
    float pos[3];
    float vel[3];
    int cutsLeft;
    float ttl;
} LnPendingCut;

typedef struct {
    int used;
    float pos[3];
    unsigned int rgba;
    float radius;
    float age;
    float maxAge;
} LnSpark;

typedef struct {
    int used;
    float from[3];
    float to[3];
    unsigned int rgba;
    float age;
    float maxAge;
} LnTracer;

typedef struct {
    int used;
    ChrRecord *chr;
    s16 chrnum;
    int kind;
    int color;
    int doubleBladed;
    float scale;
    int scaleApplied;
    GevrLnIgnition ignite;
    float meleeCd;
    float powerCd;
    float swingTimer;
    int handValid;
    s32 handTick;
    float handPosW[3];
    float handDirW[3];
    GevrLnThrown boomerang;
} LnSpecialNpc;

static GevrLnIgnition s_fpIgnite;
static GevrLnThrown s_thrown;
static int s_blocking;
static float s_blockHoldSec;
static int s_ninjaShieldHeld;
static int s_thrownBounces;
static float s_bladeLockTimer;
static float s_bladeLockPulse;
static ChrRecord *s_bladeLockChr;
static s16 s_bladeLockChrnum;
static float s_shadowDashTimer;
static float s_shadowDashCd;
static int s_shadowApplied;
static int s_caughtProjIdx = -1;
static float s_wallSparkCd;
static float s_swingAudio01;
static float s_time;
static float s_vrPowerCd;
static int s_vrTrigWas[2];
static float s_vrThrowGuard;
static float s_vrWhooshCd[2];

/* Cached world-space tracked hand/blade state for damage interception & swept VR melee */
static int s_vrBladeValid;
static float s_vrBladeHiltW[3], s_vrBladeDirW[3], s_vrBladeAimW[3], s_vrBladeTipW[3];
static float s_vrBladeBackW[3];
static int s_vrOffBladeValid;
static float s_vrOffHiltW[3], s_vrOffDirW[3], s_vrOffTipW[3];
static int s_vrForceValid;
static float s_vrForcePosW[3], s_vrForceAimW[3];
static int s_prevTipValid[2];
static float s_prevTipW[2][3];
static ChrRecord *s_vrHitChr[2][LN_VR_HIT_SLOTS];
static float s_vrHitCd[2][LN_VR_HIT_SLOTS];

static ChrRecord *s_chokeChr;
static s16 s_chokeChrnum;
static int s_chokeActive;
static float s_chokeHoldSec;

static int s_grabBodySlot = -1;
static int s_thrownBodySlot = -1;
static float s_thrownBodyAge;
static float s_thrownBodyStart[3];
static GevrLnThrownBody s_thrownBody;
static ChrRecord *s_thrownBodyHit[8];
static int s_thrownBodyHitCount;

static int s_limbBallSuspended;
static float s_limbBallPos[3];

static int s_lightningActive;
static ChrRecord *s_lightningChrs[LN_MAX_LIGHTNING];
static s16 s_lightningChrnums[LN_MAX_LIGHTNING];
static int s_lightningCount;
static float s_lightningTickAccum;

static ChrRecord *s_throwHitChr[12];
static float s_throwHitCd[12];
static int s_throwHitCount;

static LnPendingCut s_cuts[LN_MAX_CUTS];
static LnSpark s_sparks[LN_MAX_SPARKS];
static LnTracer s_tracers[LN_MAX_TRACERS];
static LnSmokeCloud s_smoke[LN_MAX_SMOKE];
static LnBladeTrail s_trails[3];
static LnSpecialNpc s_specials[LN_MAX_SPECIAL];
static int s_specialsSpawned;
static int s_laserGranted;
static int s_lastStage = -1;
static float s_rivalLightningShow;
static float s_rivalLightningFrom[3], s_rivalLightningTo[3];

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static float lnViewScale(void)
{
    return (D_800364CC > 1e-6f) ? D_800364CC : 1.0f;
}

static float lnNorm3(const float v[3], float out[3])
{
    const float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l2 < 1e-10f) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f;
        return 0.0f;
    }
    const float l = sqrtf(l2);
    const float inv = 1.0f / l;
    out[0] = v[0] * inv; out[1] = v[1] * inv; out[2] = v[2] * inv;
    return l;
}

static void lnCross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float lnDot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void lnHaptic(int ctrl, float amp, float dur)
{
    if (vr_haptics_ready()) {
        trigger_haptic_vibration_c(ctrl, amp, dur);
    }
}

static void lnSfx(int eventId, float intensity)
{
    if (VrLnAudio) {
        gevrLnAudioTrigger(eventId, intensity);
    }
}

static void lnRestoreShadowSpeed(void)
{
    if (s_shadowApplied && g_CurrentPlayer) {
        g_CurrentPlayer->speedboost = 1.0f;
        s_shadowApplied = 0;
    }
}

static void lnTrailClear(void)
{
    memset(s_trails, 0, sizeof(s_trails));
}

static void lnTrailStepAge(float dt)
{
    for (int t = 0; t < 3; t++) {
        LnBladeTrail *tr = &s_trails[t];
        int keep = 0;
        for (int i = 0; i < tr->count; i++) {
            tr->age[i] += dt;
            if (tr->age[i] <= 0.14f) {
                keep = i + 1;
            } else {
                break;
            }
        }
        tr->count = keep;
    }
}

static void lnTrailPushWorld(int idx, const float hiltW[3], const float tipW[3])
{
    if (idx < 0 || idx >= 3 || !VrLnBladeTrails) return;
    LnBladeTrail *tr = &s_trails[idx];
    if (tr->count > 0) {
        const float dx = tipW[0] - tr->tip[0][0];
        const float dy = tipW[1] - tr->tip[0][1];
        const float dz = tipW[2] - tr->tip[0][2];
        if (dx * dx + dy * dy + dz * dz < 4.0f * 4.0f) return;
    }
    const int n = tr->count < LN_TRAIL_SAMPLES ? tr->count : LN_TRAIL_SAMPLES - 1;
    for (int i = n; i > 0; i--) {
        memcpy(tr->hilt[i], tr->hilt[i - 1], sizeof(tr->hilt[0]));
        memcpy(tr->tip[i], tr->tip[i - 1], sizeof(tr->tip[0]));
        tr->age[i] = tr->age[i - 1];
    }
    memcpy(tr->hilt[0], hiltW, sizeof(tr->hilt[0]));
    memcpy(tr->tip[0], tipW, sizeof(tr->tip[0]));
    tr->age[0] = 0.0f;
    if (tr->count < LN_TRAIL_SAMPLES) tr->count++;
}

static int lnProjValidAirborne(int idx)
{
    if (idx < 0 || idx >= PROJECTILES_ARR_MAX) return 0;
    const Projectile *pr = &g_Projectiles[idx];
    if ((pr->flags & PROJECTILEFLAG_FREE) || !(pr->flags & PROJECTILEFLAG_AIRBORNE)) return 0;
    if ((pr->flags & PROJECTILEFLAG_STICKY) || !pr->obj || !pr->obj->prop) return 0;
    return 1;
}

static int lnModeActive(void)
{
    return gevrIsQuestVrApk()
        && g_gevrStereo
        && VrLaserswordNinjaMode != 0
        && gevrLnPackGet() != NULL
        && g_CurrentPlayer != NULL
        && (!netIsActive() || getPlayerCount() == 1);
}

static int lnUsingGunsOverride(void)
{
    return VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && gevrLnSelectedPower == GEVR_LN_FORCE_GUNS;
}

static int lnInGameplay(void)
{
    if (!g_CurrentPlayer || g_CurrentPlayer->bonddead || g_PlayerIsInTank == 1) return 0;
    if (g_StageNum == LEVELID_TITLE || g_CurrentPlayer->pause_state != 0) return 0;
    if (g_CurrentPlayer->watch_animation_state != 0 || lvlGetControlsLockedFlag()) return 0;
    if (gevrSpectating() || gevrCoopLocalDowned()) return 0;
    return 1;
}

static int lnEffectiveNinjaSword(void)
{
    if (VrLaserswordNinjaMode != GEVR_LN_NINJA) return VrNinjaSword;
    const int gold = gevrLnIsGoldSword(VrNinjaSword);
    switch (gevrLnSelectedNinjaSlot) {
        case GEVR_LN_NINJA_SLOT_SHORT:
            return gold ? GEVR_LN_SWORD_GOLD_SHORT : GEVR_LN_SWORD_SHORT;
        case GEVR_LN_NINJA_SLOT_DUAL:
            return gold ? GEVR_LN_SWORD_DUAL_GOLD_SHORT : GEVR_LN_SWORD_DUAL_SHORT;
        case GEVR_LN_NINJA_SLOT_KATANA:
        case GEVR_LN_NINJA_SLOT_SHIELD:
        case GEVR_LN_NINJA_SLOT_BOOMERANG:
        default:
            return VrNinjaSword;
    }
}

static int lnHasNinjaShield(void)
{
    return VrLaserswordNinjaMode == GEVR_LN_NINJA && VrNinjaShield != 0
        && gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_SHIELD;
}

static int lnChrAliveValid(ChrRecord *c, s16 num)
{
    if (!c || !c->prop || !c->model || !c->prop->stan) return 0;
    if (num >= 0 && c->chrnum != num) return 0;
    if (c->chrnum < 0 || c->prop->type != PROP_TYPE_CHR) return 0;
    if (chrIsDead(c) || (c->hidden & CHRHIDDEN_REMOVE) || (c->chrflags & CHRFLAG_HIDDEN)) return 0;
    return 1;
}

static int lnIsEnemyGuard(ChrRecord *c)
{
    if (!lnChrAliveValid(c, -1)) return 0;
    if (gevrAllyIs(c)) return 0;
    if (c->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN) return 0;
    if (c->actiontype == ACT_SURRENDER) return 0;
    return 1;
}

static LnSpecialNpc *lnFindSpecial(ChrRecord *c)
{
    if (!c) return NULL;
    for (int i = 0; i < LN_MAX_SPECIAL; i++) {
        if (s_specials[i].used && s_specials[i].chr == c && s_specials[i].chrnum == c->chrnum) {
            return &s_specials[i];
        }
    }
    return NULL;
}

/* Wall, door, and pathblocker/glass line-of-sight verification */
static int lnHasLos(const float from[3], StandTile *fromStan, const float tileOrigin[3], const float to[3])
{
    if (!from || !to) return 0;
    if (fromStan != NULL) {
        StandTile *stan = fromStan;
        const float *o = tileOrigin ? tileOrigin : from;
        if (stanTestLineUnobstructed(&stan, o[0], o[2], to[0], to[2],
                                     CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                     from[1], from[1], to[1], to[1]) == 0) {
            return 0;
        }
    }
    if (pdphysIsReady() && pdphysRaycastAnyRoom(from, to, NULL, NULL, NULL)) {
        return 0;
    }
    return 1;
}

static void lnAddSpark(const float pos[3], unsigned int rgba, float radius, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < LN_MAX_SPARKS; i++) {
        if (!s_sparks[i].used) { slot = i; break; }
        if (s_sparks[i].age > oldest) { oldest = s_sparks[i].age; slot = i; }
    }
    s_sparks[slot].used = 1;
    memcpy(s_sparks[slot].pos, pos, sizeof(s_sparks[slot].pos));
    s_sparks[slot].rgba = rgba;
    s_sparks[slot].radius = radius;
    s_sparks[slot].age = 0.0f;
    s_sparks[slot].maxAge = maxAge;
}

static void lnAddTracer(const float from[3], const float to[3], unsigned int rgba, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < LN_MAX_TRACERS; i++) {
        if (!s_tracers[i].used) { slot = i; break; }
        if (s_tracers[i].age > oldest) { oldest = s_tracers[i].age; slot = i; }
    }
    s_tracers[slot].used = 1;
    memcpy(s_tracers[slot].from, from, sizeof(s_tracers[slot].from));
    memcpy(s_tracers[slot].to, to, sizeof(s_tracers[slot].to));
    s_tracers[slot].rgba = rgba;
    s_tracers[slot].age = 0.0f;
    s_tracers[slot].maxAge = maxAge;
}

static void lnAddSmoke(const float pos[3], const float vel[3], float radius, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < LN_MAX_SMOKE; i++) {
        if (!s_smoke[i].used) { slot = i; break; }
        if (s_smoke[i].age > oldest) { oldest = s_smoke[i].age; slot = i; }
    }
    s_smoke[slot].used = 1;
    memcpy(s_smoke[slot].pos, pos, sizeof(s_smoke[slot].pos));
    memcpy(s_smoke[slot].vel, vel, sizeof(s_smoke[slot].vel));
    s_smoke[slot].radius = radius;
    s_smoke[slot].age = 0.0f;
    s_smoke[slot].maxAge = maxAge;
}

static void lnActivateShadowStep(const float origin[3], const float fwd[3])
{
    if (!VrNinjaShadowStep || s_shadowDashCd > 0.0f) return;
    for (int i = 0; i < 6; i++) {
        const float a = (float)i * (6.2831853f / 6.0f);
        const float sp[3] = {
            origin[0] + cosf(a) * 45.0f + fwd[0] * 25.0f,
            origin[1] - 55.0f + (float)(i & 1) * 30.0f,
            origin[2] + sinf(a) * 45.0f + fwd[2] * 25.0f
        };
        const float sv[3] = { cosf(a) * 35.0f, 32.0f + (float)(i % 3) * 10.0f, sinf(a) * 35.0f };
        lnAddSmoke(sp, sv, 68.0f + (float)(i & 1) * 20.0f, 3.4f);
    }
    s_shadowDashTimer = 0.65f;
    s_shadowDashCd = 2.2f;
    if (g_CurrentPlayer) {
        g_CurrentPlayer->speedboost = 1.65f;
        s_shadowApplied = 1;
    }
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!lnIsEnemyGuard(c)) continue;
        const float dx = c->prop->pos.x - origin[0];
        const float dy = (c->prop->pos.y + 90.0f) - origin[1];
        const float dz = c->prop->pos.z - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 280.0f * 280.0f || d2 < 1.0f) continue;
        c->chrflags |= CHRFLAG_WAS_DAMAGED;
        if (c->accuracyrating > 25) c->accuracyrating -= 20;
    }
    lnSfx(GEVR_LN_SFX_SHADOW_STEP, 1.15f);
    lnHaptic(VrLaserswordHand ? 1 : 0, 0.7f, 0.08f);
}

static void lnQueueCut(const float pos[3], const float vel[3], int maxCuts)
{
    if (gevrRagdollForceCutNear(pos, 95.0f, vel, maxCuts) > 0) {
        return;
    }
    for (int i = 0; i < LN_MAX_CUTS; i++) {
        if (!s_cuts[i].used) {
            s_cuts[i].used = 1;
            memcpy(s_cuts[i].pos, pos, sizeof(s_cuts[i].pos));
            memcpy(s_cuts[i].vel, vel, sizeof(s_cuts[i].vel));
            s_cuts[i].cutsLeft = maxCuts;
            s_cuts[i].ttl = 0.25f;
            return;
        }
    }
}

static void lnGetPlayerCameraWorld(float eye[3], float right[3], float up[3], float fwd[3])
{
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    if (!v2w) {
        eye[0] = eye[1] = eye[2] = 0.0f;
        right[0] = 1.0f; right[1] = 0.0f; right[2] = 0.0f;
        up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
        fwd[0] = 0.0f; fwd[1] = 0.0f; fwd[2] = -1.0f;
        return;
    }
    eye[0] = v2w->m[3][0]; eye[1] = v2w->m[3][1]; eye[2] = v2w->m[3][2];
    right[0] = v2w->m[0][0]; right[1] = v2w->m[0][1]; right[2] = v2w->m[0][2];
    up[0] = v2w->m[1][0]; up[1] = v2w->m[1][1]; up[2] = v2w->m[1][2];
    fwd[0] = -v2w->m[2][0]; fwd[1] = -v2w->m[2][1]; fwd[2] = -v2w->m[2][2];
    lnNorm3(right, right);
    lnNorm3(up, up);
    lnNorm3(fwd, fwd);
}

static void lnGetPlayerTileOrigin(float outOrigin[3], StandTile **outStan)
{
    PropRecord *me = getCurrentPlayerProp();
    if (me && me->stan) {
        outOrigin[0] = me->pos.x;
        outOrigin[1] = me->pos.y;
        outOrigin[2] = me->pos.z;
        *outStan = me->stan;
    } else {
        outOrigin[0] = outOrigin[1] = outOrigin[2] = 0.0f;
        *outStan = NULL;
    }
}

static void lnMoveChrProp(ChrRecord *c, const float w[3])
{
    PropRecord *prop = c->prop;
    StandTile *tile = prop->stan;
    if (tile && walkTilesBetweenPoints_NoCallback(&tile, prop->pos.x, prop->pos.z, w[0], w[2]) == TRUE && tile) {
        prop->stan = tile;
    } else {
        coord3d q;
        q.x = w[0]; q.y = w[1]; q.z = w[2];
        StandTile *below = stanFindTileBelowPos(&q, NULL, NULL);
        if (below) prop->stan = below;
    }
    prop->pos.x = w[0]; prop->pos.y = w[1]; prop->pos.z = w[2];
    c->prevpos = prop->pos;
    c->ground = c->manground = w[1];
    c->sumground = w[1] * 0.1f;
    c->fallspeed.x = c->fallspeed.y = c->fallspeed.z = 0.0f;
    if (prop->stan) {
        prop->rooms[0] = prop->stan->room;
        prop->rooms[1] = 0xFF;
    }
}

static void lnHitGuardWithBlade(ChrRecord *c, const float dir[3], float dmgAmount, int forceCutCount, int isBlade,
                                const float hitPt[3])
{
    if (!lnChrAliveValid(c, -1)) return;
    coord3d d;
    d.x = dir[0]; d.y = dir[1]; d.z = dir[2];
    LnSpecialNpc *sp = lnFindSpecial(c);
    const s32 wep = (isBlade || forceCutCount > 0) ? ITEM_KNIFE : ITEM_FIST;
    const f32 targetDmg = c->damage + dmgAmount;

    if (targetDmg < c->maxdamage) {
        /* Non-lethal hit: raise maxdamage temporarily so handles_shot_actors runs
         * flinch, blood and audio without falsely triggering lethal death or stats. */
        const f32 savedMax = c->maxdamage;
        c->maxdamage = targetDmg + 1000.0f;
        handles_shot_actors(c, HIT_CHEST, &d, wep, FALSE);
        c->maxdamage = savedMax;
        if (!chrIsDead(c)) {
            c->damage = targetDmg;
        }
    } else {
        c->damage = c->maxdamage;
        handles_shot_actors(c, HIT_CHEST, &d, wep, TRUE);
    }

    const float fallbackPos[3] = { c->prop->pos.x, c->prop->pos.y + 110.0f, c->prop->pos.z };
    const float *cutPt = hitPt ? hitPt : fallbackPos;
    unsigned int core = 0xFFFFFFFFu, glow = 0xFFD040FFu;
    if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) gevrLnLaserswordPalette(VrLaserswordColor, &core, &glow);
    lnAddSpark(cutPt, sp ? 0xFFE860FFu : glow, sp ? 28.0f : 22.0f, 0.22f);
    if (forceCutCount > 0 && chrIsDead(c)) {
        const float cutVel[3] = { dir[0] * 420.0f, dir[1] * 260.0f + 160.0f, dir[2] * 420.0f };
        lnQueueCut(cutPt, cutVel, forceCutCount);
    }
}

static int lnVrCanHitChr(int handSlot, ChrRecord *c)
{
    for (int i = 0; i < LN_VR_HIT_SLOTS; i++) {
        if (s_vrHitChr[handSlot][i] == c && s_vrHitCd[handSlot][i] > 0.0f) {
            return 0;
        }
    }
    return 1;
}

static void lnVrMarkHitChr(int handSlot, ChrRecord *c, float cd)
{
    int freeIdx = 0;
    float minCd = 1e9f;
    for (int i = 0; i < LN_VR_HIT_SLOTS; i++) {
        if (s_vrHitChr[handSlot][i] == c) {
            s_vrHitCd[handSlot][i] = cd;
            return;
        }
        if (s_vrHitCd[handSlot][i] <= 0.0f) {
            freeIdx = i;
            break;
        }
        if (s_vrHitCd[handSlot][i] < minCd) {
            minCd = s_vrHitCd[handSlot][i];
            freeIdx = i;
        }
    }
    s_vrHitChr[handSlot][freeIdx] = c;
    s_vrHitCd[handSlot][freeIdx] = cd;
}

static int lnVrSweepBladeAgainstGuards(int handSlot, const float hiltW[3], const float tipW[3],
                                       const float backTipW[3], int hasBackTip,
                                       const float prevTipW[3], int hasPrevTip,
                                       const float velDirW[3], float dmg, int hapticCtrl)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    lnGetPlayerTileOrigin(pOrigin, &pStan);

    int hits = 0;
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!lnIsEnemyGuard(c) || !lnVrCanHitChr(handSlot, c)) continue;

        LnSpecialNpc *sp = lnFindSpecial(c);
        const float r = (c->chrwidth > 12.0f ? c->chrwidth : 20.0f) + 18.0f;
        const float h = 185.0f * (sp ? sp->scale : 1.0f);
        float hitPt[3];
        int hit = gevrLnSegmentHitCylinder(hiltW, tipW, c->prop->pos.f, r, h, hitPt);
        if (!hit && hasBackTip) {
            hit = gevrLnSegmentHitCylinder(hiltW, backTipW, c->prop->pos.f, r, h, hitPt);
        }
        if (!hit && hasPrevTip) {
            hit = gevrLnSegmentHitCylinder(prevTipW, tipW, c->prop->pos.f, r, h, hitPt);
        }
        if (!hit) continue;

        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
        if (!lnHasLos(hiltW, pStan, pOrigin, tgt)) continue;

        lnVrMarkHitChr(handSlot, c, 0.24f);
        lnHitGuardWithBlade(c, velDirW, dmg, 2, 1, hitPt);
        lnAddSpark(hitPt, 0xFFF480FFu, 22.0f, 0.18f);
        lnHaptic(hapticCtrl, 0.85f, 0.08f);
        hits++;
    }
    return hits;
}

/* -------------------------------------------------------------------------- */
/* Lifecycle & NPC Hand Hooks                                                 */
/* -------------------------------------------------------------------------- */

void gevrLnGameReset(void)
{
    memset(&s_fpIgnite, 0, sizeof(s_fpIgnite));
    memset(&s_thrown, 0, sizeof(s_thrown));
    memset(s_cuts, 0, sizeof(s_cuts));
    memset(s_sparks, 0, sizeof(s_sparks));
    memset(s_tracers, 0, sizeof(s_tracers));
    memset(s_smoke, 0, sizeof(s_smoke));
    memset(s_specials, 0, sizeof(s_specials));
    memset(s_vrHitChr, 0, sizeof(s_vrHitChr));
    memset(s_vrHitCd, 0, sizeof(s_vrHitCd));
    lnTrailClear();
    gevrLnAudioReset();
    lnRestoreShadowSpeed();
    s_blocking = 0;
    s_blockHoldSec = 0.0f;
    s_ninjaShieldHeld = 0;
    s_thrownBounces = 0;
    s_bladeLockTimer = 0.0f;
    s_bladeLockPulse = 0.0f;
    s_bladeLockChr = NULL;
    s_bladeLockChrnum = -1;
    s_shadowDashTimer = 0.0f;
    s_shadowDashCd = 0.0f;
    s_caughtProjIdx = -1;
    s_wallSparkCd = 0.0f;
    s_swingAudio01 = 0.0f;
    s_time = 0.0f;
    s_vrPowerCd = 0.0f;
    s_vrTrigWas[0] = s_vrTrigWas[1] = 0;
    s_vrThrowGuard = 0.0f;
    s_vrWhooshCd[0] = s_vrWhooshCd[1] = 0.0f;
    s_vrBladeValid = 0;
    s_vrOffBladeValid = 0;
    s_vrForceValid = 0;
    s_prevTipValid[0] = s_prevTipValid[1] = 0;
    s_chokeChr = NULL;
    s_chokeChrnum = -1;
    s_chokeActive = 0;
    s_chokeHoldSec = 0.0f;
    s_grabBodySlot = -1;
    s_thrownBodySlot = -1;
    s_thrownBodyAge = 0.0f;
    s_thrownBodyHitCount = 0;
    s_limbBallSuspended = 0;
    s_lightningActive = 0;
    s_lightningCount = 0;
    s_throwHitCount = 0;
    s_specialsSpawned = 0;
    s_laserGranted = 0;
    s_rivalLightningShow = 0.0f;
    s_lastStage = g_StageNum;
}

s32 gevrLnSuppressNormalFire(void)
{
    return lnModeActive() && !lnUsingGunsOverride() && lnInGameplay();
}

s32 gevrLnHideNormalGun(void)
{
    return lnModeActive() && !lnUsingGunsOverride() && lnInGameplay();
}

s32 gevrLnLasersActive(void)
{
    return lnModeActive() && VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLaserswordLasers != 0;
}

s32 gevrLnIsMeleeNpc(ChrRecord *chr)
{
    return (lnFindSpecial(chr) != NULL) ? 1 : 0;
}

void gevrLnNoteNpcHand(ChrRecord *chr, s32 hand, const Mtxf *handMtxCam)
{
    LnSpecialNpc *sp = lnFindSpecial(chr);
    Mtxf *v2w;
    coord3d c, upC, fwdC;

    if (!sp || !handMtxCam || g_gevrExtraPass || (v2w = currentPlayerGetViewToWorldMtxf()) == NULL) {
        return;
    }
    if (hand != GUNRIGHT && sp->handValid) {
        return;
    }
    c.x = handMtxCam->m[3][0];
    c.y = handMtxCam->m[3][1];
    c.z = handMtxCam->m[3][2];
    if (!isfinite(c.x) || !isfinite(c.y) || !isfinite(c.z)
        || fabsf(c.x) > 1.0e6f || fabsf(c.y) > 1.0e6f || fabsf(c.z) > 1.0e6f) {
        return;
    }
    mtx4TransformVecInPlace(v2w, &c);
    sp->handPosW[0] = c.x;
    sp->handPosW[1] = c.y;
    sp->handPosW[2] = c.z;

    upC.x = handMtxCam->m[1][0]; upC.y = handMtxCam->m[1][1]; upC.z = handMtxCam->m[1][2];
    fwdC.x = handMtxCam->m[2][0]; fwdC.y = handMtxCam->m[2][1]; fwdC.z = handMtxCam->m[2][2];
    mtx4RotateVecInPlace(v2w, &upC);
    mtx4RotateVecInPlace(v2w, &fwdC);
    const float mix[3] = {
        upC.x * 0.72f + fwdC.x * 0.68f,
        upC.y * 0.72f + fwdC.y * 0.68f,
        upC.z * 0.72f + fwdC.z * 0.68f
    };
    lnNorm3(mix, sp->handDirW);
    sp->handValid = 1;
    sp->handTick = g_GlobalTimer;
}

s32 gevrLnCanPickupProp(PropRecord *prop)
{
    if (!lnModeActive() || prop == NULL || prop->obj == NULL) return 1;
    if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLaserswordPickupGuns != 0) return 1;
    if (VrLaserswordNinjaMode == GEVR_LN_NINJA && VrNinjaPickupGuns != 0) return 1;

    ObjectRecord *obj = prop->obj;
    switch (obj->type) {
        case PROPDEF_KEY:
        case PROPDEF_ARMOUR:
            return 1;
        case PROPDEF_MAGAZINE:
        case PROPDEF_AMMO:
            return 0;
        case PROPDEF_COLLECTABLE: {
            const s32 w = GEVR_ITEM_AS(((WeaponObjRecord *)obj)->weaponnum);
            if (w == ITEM_GOLDENGUN && g_StageNum == LEVELID_EGYPT) {
                return 1;
            }
            if ((w >= ITEM_KNIFE && w <= ITEM_LASER)
                || (w >= ITEM_GRENADELAUNCH && w <= ITEM_TANKSHELLS)
                || w == ITEM_FLAREPISTOL
                || w == ITEM_ROCKETROUND
                || w == ITEM_GRENADEROUND) {
                return 0;
            }
            return 1;
        }
        default:
            return 1;
    }
}

static void lnSpawnSpecialNpcs(void)
{
    if (!gevrIsQuestVrApk() || !g_gevrStereo || gevrLnPackGet() == NULL) return;
    if (s_specialsSpawned || g_StageNum == LEVELID_TITLE) return;
    const s32 n = get_numguards();
    if (n <= 0) return;

    const int wantRival = VrRivalNpc && (VrLaserswordNinjaMode != 0 || VrRivalInNormalMode != 0)
        ? (VrRivalNpc >= 2 ? 3 : 1) : 0;
    const int wantNinja = VrNinjaNpc && (VrLaserswordNinjaMode != 0 || VrNinjaInNormalMode != 0)
        ? (VrNinjaNpc >= 2 ? 3 : 1) : 0;
    if (!wantRival && !wantNinja) {
        s_specialsSpawned = 1;
        return;
    }

    int candidates[128], nc = 0;
    for (s32 i = 0; i < n && nc < 128; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (lnIsEnemyGuard(c) && (c->weapons_held[0] || c->weapons_held[1])) {
            candidates[nc++] = i;
        }
    }
    if (nc == 0) return;

    /* Deterministic pseudo-random shuffle using game seed */
    for (int i = nc - 1; i > 0; i--) {
        int j = (int)(randomGetNext() % (u32)(i + 1));
        int tmp = candidates[i]; candidates[i] = candidates[j]; candidates[j] = tmp;
    }

    int idx = 0, slot = 0;
    for (int k = 0; k < wantRival && idx < nc && slot < LN_MAX_SPECIAL; k++, idx++) {
        ChrRecord *c = &g_ChrSlots[candidates[idx]];
        LnSpecialNpc *sp = &s_specials[slot++];
        memset(sp, 0, sizeof(*sp));
        sp->used = 1;
        sp->chr = c;
        sp->chrnum = c->chrnum;
        sp->kind = LN_NPC_RIVAL;
        sp->color = GEVR_LN_COLOR_RED;
        sp->doubleBladed = ((c->chrnum + g_StageNum) % 3) == 0 ? 1 : 0;
        sp->scale = gevrLnRivalScaleFactor(VrRivalScale, (unsigned int)(c->chrnum * 7919 + g_StageNum));
        c->maxdamage *= 5.0f;
        if (c->maxdamage < 40.0f) c->maxdamage = 40.0f;
        c->speedrating = 100;
        c->accuracyrating = 90;
    }
    for (int k = 0; k < wantNinja && idx < nc && slot < LN_MAX_SPECIAL; k++, idx++) {
        ChrRecord *c = &g_ChrSlots[candidates[idx]];
        LnSpecialNpc *sp = &s_specials[slot++];
        memset(sp, 0, sizeof(*sp));
        sp->used = 1;
        sp->chr = c;
        sp->chrnum = c->chrnum;
        sp->kind = LN_NPC_NINJA;
        sp->color = 0;
        sp->scale = 1.0f;
        c->maxdamage *= 2.5f;
        if (c->maxdamage < 25.0f) c->maxdamage = 25.0f;
        c->speedrating = 95;
    }
    s_specialsSpawned = 1;
}

static void lnTickSpecialNpcs(const float playerPos[3], float dt)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    lnGetPlayerTileOrigin(pOrigin, &pStan);

    for (int i = 0; i < LN_MAX_SPECIAL; i++) {
        LnSpecialNpc *sp = &s_specials[i];
        if (!sp->used) continue;
        if (!lnChrAliveValid(sp->chr, sp->chrnum)) {
            sp->used = 0;
            continue;
        }
        ChrRecord *c = sp->chr;
        if (!sp->scaleApplied && sp->scale != 1.0f && c->model) {
            modelSetScale(c->model, c->model->scale * sp->scale);
            sp->scaleApplied = 1;
        }
        gevrLnIgnitionUpdate(&sp->ignite, 1, 0, dt);
        if (sp->meleeCd > 0.0f) sp->meleeCd -= dt;
        if (sp->powerCd > 0.0f) sp->powerCd -= dt;
        if (sp->swingTimer > 0.0f) sp->swingTimer -= dt;

        /* Enemy thrown boomerang update */
        if (sp->boomerang.active) {
            const float handPos[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f * sp->scale, c->prop->pos.z };
            float prev[3];
            memcpy(prev, sp->boomerang.pos, sizeof(prev));
            int hitWall = 0;
            if (!sp->boomerang.returning && pdphysIsReady()) {
                float nxt[3] = {
                    prev[0] + sp->boomerang.vel[0] * dt,
                    prev[1] + sp->boomerang.vel[1] * dt,
                    prev[2] + sp->boomerang.vel[2] * dt
                };
                hitWall = pdphysRaycastAnyRoom(prev, nxt, NULL, NULL, NULL);
            }
            const float pdx = sp->boomerang.pos[0] - playerPos[0];
            const float pdy = sp->boomerang.pos[1] - playerPos[1];
            const float pdz = sp->boomerang.pos[2] - playerPos[2];
            if (pdx * pdx + pdy * pdy + pdz * pdz <= 65.0f * 65.0f) {
                /* Bullet/projectile travel direction: attacker -> player */
                const float vx = playerPos[0] - sp->boomerang.pos[0];
                const float vz = playerPos[2] - sp->boomerang.pos[2];
                if (!gevrLnInterceptPlayerDamage(1.2f, vx, vz)) {
                    record_damage_kills(1.2f, vx, vz, -1, 1);
                }
                sp->boomerang.returning = 1;
            }
            gevrLnThrownStep(&sp->boomerang, sp->handValid ? sp->handPosW : handPos, hitWall, dt);
        }

        if (sp->kind == LN_NPC_ALLY_LASERSWORD) continue;
        if (s_chokeActive && s_chokeChr == c) continue; /* suspended & helpless */

        const float npcEye[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f * sp->scale, c->prop->pos.z };
        const float dx = playerPos[0] - c->prop->pos.x;
        const float dy = playerPos[1] - (c->prop->pos.y + 100.0f * sp->scale);
        const float dz = playerPos[2] - c->prop->pos.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 2200.0f * 2200.0f) continue;

        if (c->alertness < 50 && d2 > 900.0f * 900.0f) continue;

        const float d = sqrtf(fmaxf(1.0f, d2));
        const int hasLos = lnHasLos(npcEye, c->prop->stan, c->prop->pos.f, playerPos);
        if (!hasLos) continue;

        if (d > 110.0f && g_CurrentPlayer && g_CurrentPlayer->prop && g_CurrentPlayer->prop->stan) {
            if (c->actiontype != ACT_GOPOS) {
                plot_course_for_actor(c, &g_CurrentPlayer->prop->pos, g_CurrentPlayer->prop->stan, SPEED_RUN);
            }
            /* Ground/stair-validated chase step toward player */
            if (d > 140.0f && d < 1200.0f) {
                const float spd = (sp->kind == LN_NPC_RIVAL ? 220.0f : 185.0f) * dt;
                coord3d probe;
                probe.x = c->prop->pos.x + (dx / d) * spd;
                probe.y = c->prop->pos.y + 40.0f;
                probe.z = c->prop->pos.z + (dz / d) * spd;
                StandTile *walkTile = c->prop->stan;
                f32 groundY = c->prop->pos.y;
                s32 incentre = 0;
                if (walkTile && stanTestLineUnobstructed(&walkTile, c->prop->pos.x, c->prop->pos.z, probe.x, probe.z,
                                                         CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER,
                                                         c->prop->pos.y + 50.0f, c->prop->pos.y + 50.0f,
                                                         c->prop->pos.y + 50.0f, c->prop->pos.y + 50.0f) != 0) {
                    StandTile *groundTile = stanFindGroundAtCyl(&probe, c->chrwidth, c->prop->rooms, walkTile, &groundY, &incentre);
                    if (groundTile && fabsf(groundY - c->prop->pos.y) <= 45.0f) {
                        const float nxt[3] = { probe.x, groundY, probe.z };
                        if (!pdphysIsReady() || !pdphysRaycastAnyRoom(npcEye, nxt, NULL, NULL, NULL)) {
                            lnMoveChrProp(c, nxt);
                        }
                    }
                }
            }
        }

        /* Melee lasersword / katana swing */
        const float meleeRange = 145.0f * (sp->scale > 1.0f ? sp->scale : 1.0f);
        if (d <= meleeRange && sp->meleeCd <= 0.0f) {
            sp->swingTimer = 0.28f;
            if (sp->kind == LN_NPC_RIVAL && VrLaserswordNinjaMode == GEVR_LN_LASERSWORD
                && VrLnPerfectParry && s_blocking
                && lnModeActive() && !lnUsingGunsOverride() && s_bladeLockTimer <= 0.0f) {
                s_bladeLockTimer = 0.85f;
                s_bladeLockPulse = 0.0f;
                s_bladeLockChr = c;
                s_bladeLockChrnum = c->chrnum;
                sp->meleeCd = 1.15f;
                const float clashPt[3] = {
                    (playerPos[0] + c->prop->pos.x) * 0.5f,
                    c->prop->pos.y + 115.0f * sp->scale,
                    (playerPos[2] + c->prop->pos.z) * 0.5f
                };
                lnAddSpark(clashPt, 0xFFF078FFu, 30.0f, 0.25f);
                lnSfx(GEVR_LN_SFX_CLASH_LASERSWORD, 1.3f);
                lnHaptic(VrLaserswordHand ? 0 : 1, 1.0f, 0.10f);
            } else {
                sp->meleeCd = 0.85f;
                const float vx = playerPos[0] - c->prop->pos.x;
                const float vz = playerPos[2] - c->prop->pos.z;
                if (!gevrLnInterceptPlayerDamage(1.5f, vx, vz)) {
                    record_damage_kills(1.5f, vx, vz, -1, 1);
                }
            }
        }

        /* Special attack: Rival Force Lightning / Evil Ninja Boomerang */
        if (sp->powerCd <= 0.0f && d > 180.0f && d < 950.0f) {
            if (sp->kind == LN_NPC_RIVAL) {
                sp->powerCd = 4.5f;
                const float vx = playerPos[0] - c->prop->pos.x;
                const float vz = playerPos[2] - c->prop->pos.z;
                s_rivalLightningShow = 0.45f;
                if (sp->handValid) {
                    memcpy(s_rivalLightningFrom, sp->handPosW, sizeof(s_rivalLightningFrom));
                } else {
                    s_rivalLightningFrom[0] = c->prop->pos.x;
                    s_rivalLightningFrom[1] = c->prop->pos.y + 115.0f * sp->scale;
                    s_rivalLightningFrom[2] = c->prop->pos.z;
                }
                memcpy(s_rivalLightningTo, playerPos, sizeof(s_rivalLightningTo));
                if (!gevrLnInterceptPlayerDamage(0.9f, vx, vz)) {
                    record_damage_kills(0.9f, vx, vz, -1, 1);
                }
            } else if (sp->kind == LN_NPC_NINJA && !sp->boomerang.active) {
                sp->powerCd = 4.2f;
                const float fallback[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
                const float *origin = sp->handValid ? sp->handPosW : fallback;
                const float aim[3] = { dx / d, dy / d, dz / d };
                gevrLnThrownLaunch(&sp->boomerang, 1, origin, aim);
            }
        }
    }
    (void)pOrigin;
    (void)pStan;
}

s32 gevrLnAllyThink(ChrRecord *ally)
{
    if (!gevrIsQuestVrApk() || !g_gevrStereo || gevrLnPackGet() == NULL) return 0;
    if (!VrAllyLasersword || !lnChrAliveValid(ally, -1)) return 0;

    LnSpecialNpc *sp = lnFindSpecial(ally);
    if (!sp) {
        for (int i = 0; i < LN_MAX_SPECIAL; i++) {
            if (!s_specials[i].used) {
                sp = &s_specials[i];
                memset(sp, 0, sizeof(*sp));
                sp->used = 1;
                sp->chr = ally;
                sp->chrnum = ally->chrnum;
                sp->kind = LN_NPC_ALLY_LASERSWORD;
                sp->color = (VrLaserswordColor == GEVR_LN_COLOR_BLUE) ? GEVR_LN_COLOR_GREEN : GEVR_LN_COLOR_BLUE;
                sp->scale = 1.0f;
                break;
            }
        }
    }
    if (sp && sp->meleeCd <= 0.0f) {
        const s32 n = get_numguards();
        ChrRecord *best = NULL;
        float bestD2 = 175.0f * 175.0f;
        const float allyPt[3] = { ally->prop->pos.x, ally->prop->pos.y + 100.0f, ally->prop->pos.z };
        for (s32 i = 0; i < n; i++) {
            ChrRecord *e = &g_ChrSlots[i];
            if (!lnIsEnemyGuard(e)) continue;
            const float tgt[3] = { e->prop->pos.x, e->prop->pos.y + 100.0f, e->prop->pos.z };
            const float dx = tgt[0] - allyPt[0];
            const float dy = e->prop->pos.y - ally->prop->pos.y;
            const float dz = tgt[2] - allyPt[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestD2 && lnHasLos(allyPt, ally->prop->stan, ally->prop->pos.f, tgt)) {
                bestD2 = d2;
                best = e;
            }
        }
        if (best) {
            const float dir[3] = {
                best->prop->pos.x - ally->prop->pos.x,
                0.0f,
                best->prop->pos.z - ally->prop->pos.z
            };
            float nd[3];
            lnNorm3(dir, nd);
            lnHitGuardWithBlade(best, nd, 10.0f, 1, 1, NULL);
            sp->swingTimer = 0.28f;
            sp->meleeCd = 0.7f;
        }
    }
    return 0; /* let gevrAllyThink run normal follow/target pathing too */
}

static ChrRecord *lnFindForceTarget(const float origin[3], const float dir[3], float maxRangeCm, float minDot)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    lnGetPlayerTileOrigin(pOrigin, &pStan);

    ChrRecord *best = NULL;
    float bestScore = 1e30f;
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!lnIsEnemyGuard(c)) continue;
        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
        const float dx = tgt[0] - origin[0];
        const float dy = tgt[1] - origin[1];
        const float dz = tgt[2] - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < 100.0f || d2 > maxRangeCm * maxRangeCm) continue;
        const float d = sqrtf(d2);
        const float dot = (dx * dir[0] + dy * dir[1] + dz * dir[2]) / d;
        if (dot < minDot) continue;
        if (!lnHasLos(origin, pStan, pOrigin, tgt)) continue;
        const float score = d * (1.4f - dot);
        if (score < bestScore) { bestScore = score; best = c; }
    }
    return best;
}

static void lnTriggerForcePush(const float origin[3], const float dir[3])
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    lnGetPlayerTileOrigin(pOrigin, &pStan);

    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!lnIsEnemyGuard(c)) continue;
        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 90.0f, c->prop->pos.z };
        const float dx = tgt[0] - origin[0];
        const float dy = tgt[1] - origin[1];
        const float dz = tgt[2] - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 650.0f * 650.0f || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float to[3] = { dx / d, dy / d, dz / d };
        if (lnDot3(dir, to) < 0.55f) continue;
        if (!lnHasLos(origin, pStan, pOrigin, tgt)) continue;
        lnHitGuardWithBlade(c, to, 1.2f, 0, 0, NULL);
        const float spark[3] = { c->prop->pos.x, c->prop->pos.y + 105.0f, c->prop->pos.z };
        lnAddSpark(spark, 0xA0D8FFE0u, 24.0f, 0.25f);
    }
    for (int slot = 0; slot < 24; slot++) {
        float pos[3];
        if (!gevrRagdollGetBodyPos(slot, pos)) continue;
        const float dx = pos[0] - origin[0], dy = pos[1] - origin[1], dz = pos[2] - origin[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 750.0f * 750.0f || d2 < 1.0f) continue;
        const float d = sqrtf(d2);
        const float to[3] = { dx / d, dy / d, dz / d };
        if (lnDot3(dir, to) < 0.50f) continue;
        if (!lnHasLos(origin, pStan, pOrigin, pos)) continue;
        const float vel[3] = { dir[0] * 1350.0f, dir[1] * 1350.0f + 260.0f, dir[2] * 1350.0f };
        gevrRagdollThrowBody(slot, vel);
    }
    if (VrLaserswordForcePowers && VrLaserswordCatchProjectiles && g_CurrentPlayer && g_CurrentPlayer->prop) {
        for (int i = 0; i < PROJECTILES_ARR_MAX; i++) {
            if (!lnProjValidAirborne(i)) continue;
            Projectile *pr = &g_Projectiles[i];
            const float dx = pr->obj->prop->pos.x - origin[0];
            const float dy = pr->obj->prop->pos.y - origin[1];
            const float dz = pr->obj->prop->pos.z - origin[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (i != s_caughtProjIdx) {
                if (d2 > 750.0f * 750.0f || d2 < 1.0f) continue;
                const float d = sqrtf(d2);
                const float to[3] = { dx / d, dy / d, dz / d };
                if (lnDot3(dir, to) < 0.55f) continue;
                if (!lnHasLos(origin, pStan, pOrigin, pr->obj->prop->pos.f)) continue;
            }
            float spd = sqrtf(pr->speed.x * pr->speed.x + pr->speed.y * pr->speed.y + pr->speed.z * pr->speed.z);
            if (spd < 18.0f) spd = 22.0f;
            pr->speed.x = dir[0] * spd;
            pr->speed.y = dir[1] * spd;
            pr->speed.z = dir[2] * spd;
            pr->ownerprop = g_CurrentPlayer->prop;
            lnAddSpark(pr->obj->prop->pos.f, 0x88E0FFFFu, 26.0f, 0.25f);
        }
        s_caughtProjIdx = -1;
    }
    lnSfx(GEVR_LN_SFX_FORCE_PUSH, 1.15f);
    lnHaptic(VrLaserswordHand ? 1 : 0, 0.7f, 0.08f);
}

/* -------------------------------------------------------------------------- */
/* Per-Tick Simulation                                                        */
/* -------------------------------------------------------------------------- */

void gevrLnGameTick(s32 ticks60)
{
    if (g_StageNum != s_lastStage) {
        gevrLnGameReset();
    }
    if (ticks60 <= 0) return;
    float dt = (float)ticks60 / 60.0f;
    if (dt > 0.1f) dt = 0.1f;
    s_time += dt;
    lnTrailStepAge(dt);
    if (s_wallSparkCd > 0.0f) s_wallSparkCd -= dt;
    if (s_shadowDashCd > 0.0f) s_shadowDashCd -= dt;
    if (s_vrWhooshCd[0] > 0.0f) s_vrWhooshCd[0] -= dt;
    if (s_vrWhooshCd[1] > 0.0f) s_vrWhooshCd[1] -= dt;
    for (int h = 0; h < 2; h++) {
        for (int i = 0; i < LN_VR_HIT_SLOTS; i++) {
            if (s_vrHitCd[h][i] > 0.0f) s_vrHitCd[h][i] -= dt;
        }
    }
    if (s_shadowDashTimer > 0.0f) {
        s_shadowDashTimer -= dt;
        if (s_shadowDashTimer <= 0.0f) {
            s_shadowDashTimer = 0.0f;
            lnRestoreShadowSpeed();
        }
    }

    /* Pending cuts after ragdoll capture */
    for (int i = 0; i < LN_MAX_CUTS; i++) {
        if (!s_cuts[i].used) continue;
        if (gevrRagdollForceCutNear(s_cuts[i].pos, 95.0f, s_cuts[i].vel, s_cuts[i].cutsLeft) > 0) {
            s_cuts[i].used = 0;
        } else {
            s_cuts[i].ttl -= dt;
            if (s_cuts[i].ttl <= 0.0f) s_cuts[i].used = 0;
        }
    }

    for (int i = 0; i < LN_MAX_SPARKS; i++) {
        if (s_sparks[i].used) {
            s_sparks[i].age += dt;
            if (s_sparks[i].age >= s_sparks[i].maxAge) s_sparks[i].used = 0;
        }
    }
    for (int i = 0; i < LN_MAX_TRACERS; i++) {
        if (s_tracers[i].used) {
            s_tracers[i].age += dt;
            if (s_tracers[i].age >= s_tracers[i].maxAge) s_tracers[i].used = 0;
        }
    }
    for (int i = 0; i < LN_MAX_SMOKE; i++) {
        if (s_smoke[i].used) {
            s_smoke[i].pos[0] += s_smoke[i].vel[0] * dt;
            s_smoke[i].pos[1] += s_smoke[i].vel[1] * dt;
            s_smoke[i].pos[2] += s_smoke[i].vel[2] * dt;
            const float damp = fmaxf(0.0f, 1.0f - 1.5f * dt);
            s_smoke[i].vel[0] *= damp; s_smoke[i].vel[1] *= damp; s_smoke[i].vel[2] *= damp;
            s_smoke[i].age += dt;
            if (s_smoke[i].age >= s_smoke[i].maxAge) s_smoke[i].used = 0;
        }
    }
    if (s_rivalLightningShow > 0.0f) s_rivalLightningShow -= dt;

    if (!lnInGameplay() || !gevrIsQuestVrApk() || !g_gevrStereo || gevrLnPackGet() == NULL) {
        s_blocking = 0;
        s_blockHoldSec = 0.0f;
        s_ninjaShieldHeld = 0;
        s_lightningActive = 0;
        s_bladeLockTimer = 0.0f;
        s_caughtProjIdx = -1;
        s_vrBladeValid = s_vrOffBladeValid = s_vrForceValid = 0;
        s_prevTipValid[0] = s_prevTipValid[1] = 0;
        lnRestoreShadowSpeed();
        gevrLnAudioSetState(0, 0, 0.0f, 0.0f, 0, 0, 0, 0);
        return;
    }

    /* Spawn & update special NPCs even in normal mode if enabled */
    lnSpawnSpecialNpcs();
    float eye[3], right[3], up[3], fwd[3];
    lnGetPlayerCameraWorld(eye, right, up, fwd);
    lnTickSpecialNpcs(eye, dt);
    if (VrAllyLasersword) {
        const s32 n = get_numguards();
        for (s32 i = 0; i < n; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (lnChrAliveValid(c, -1) && gevrAllyIs(c)) {
                gevrLnAllyThink(c);
            }
        }
    }

    if (!gevrLnLasersActive()) {
        s_laserGranted = 0;
    } else if (!s_laserGranted) {
        bondinvAddInvItem(ITEM_LASER);
        currentPlayerEquipWeaponWrapper(GUNRIGHT, ITEM_LASER);
        currentPlayerEquipWeaponWrapper(GUNLEFT, ITEM_UNARMED);
        s_laserGranted = 1;
    }

    if (!lnModeActive() || lnUsingGunsOverride()) {
        s_blocking = 0;
        s_blockHoldSec = 0.0f;
        s_ninjaShieldHeld = 0;
        s_lightningActive = 0;
        s_bladeLockTimer = 0.0f;
        s_caughtProjIdx = -1;
        s_vrBladeValid = s_vrOffBladeValid = s_vrForceValid = 0;
        s_prevTipValid[0] = s_prevTipValid[1] = 0;
        lnRestoreShadowSpeed();
        gevrLnAudioSetState(0, 0, 0.0f, 0.0f, 0, 0, 0, 0);
        return;
    }

    /* Determine RT / LT actions & world-space tracked VR poses */
    const int bCtrl = VrLaserswordHand ? 0 : 1;
    const int fCtrl = 1 - bCtrl;
    const int sword = lnEffectiveNinjaSword();
    const int dbl = (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLaserswordStyle == GEVR_LN_STYLE_DOUBLE);
    const int dualOffhand = gevrLnIsDualSword(VrLaserswordNinjaMode, sword)
        || (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLaserswordStyle == GEVR_LN_STYLE_DUAL);
    const float reachCm = (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) ? 115.0f : gevrLnWeaponReachCm(GEVR_LN_NINJA, sword);

    int bladeTap = 0, bladeBlock = 0, bladeThrow = 0;
    int offHold = 0, offRelease = 0, offDouble = 0;
    float bVelCm[3] = { 0.0f, 0.0f, 0.0f };
    float fVelCm[3] = { 0.0f, 0.0f, 0.0f };
    float vrSwing01 = 0.0f;

    float bladeHandW[3], forceHandW[3], forceDirW[3], bladeAimW[3];
    memcpy(bladeHandW, eye, sizeof(bladeHandW));
    memcpy(forceHandW, eye, sizeof(forceHandW));
    memcpy(forceDirW, fwd, sizeof(forceDirW));
    memcpy(bladeAimW, fwd, sizeof(bladeAimW));
    for (int k = 0; k < 3; k++) {
        bladeHandW[k] += right[k] * (VrLaserswordHand ? -18.0f : 18.0f) - up[k] * 12.0f + fwd[k] * 32.0f;
        forceHandW[k] += right[k] * (VrLaserswordHand ? 18.0f : -18.0f) - up[k] * 12.0f + fwd[k] * 32.0f;
    }

    s_vrBladeValid = 0;
    s_vrOffBladeValid = 0;
    s_vrForceValid = 0;

    {
        const int bDown = get_analog_value(bCtrl, "trigger") > 0.5f;
        const int fDown = get_analog_value(fCtrl, "trigger") > 0.5f;
        if (s_vrPowerCd > 0.0f) s_vrPowerCd -= dt;
        if (s_vrThrowGuard > 0.0f) s_vrThrowGuard -= dt;

        bladeBlock = bDown;
        gevrLnGetHandVelocityWorld(bCtrl, bVelCm);
        gevrLnGetHandVelocityWorld(fCtrl, fVelCm);

        float br[3], bu[3], bf[3], fr[3], fu[3], ff[3];
        if (gevrLnGetHandPoseWorld(bCtrl, s_vrBladeHiltW, br, bu, bf)) {
            const float bd[3] = {
                bf[0] * 0.35f + bu[0] * 0.94f,
                bf[1] * 0.35f + bu[1] * 0.94f,
                bf[2] * 0.35f + bu[2] * 0.94f
            };
            lnNorm3(bd, s_vrBladeDirW);
            lnNorm3(bf, s_vrBladeAimW);
            const float activeLen = (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) ? (91.0f * s_fpIgnite.extend + 24.0f) : reachCm;
            for (int k = 0; k < 3; k++) {
                s_vrBladeTipW[k] = s_vrBladeHiltW[k] + s_vrBladeDirW[k] * activeLen;
                s_vrBladeBackW[k] = s_vrBladeHiltW[k] - s_vrBladeDirW[k] * (activeLen * 0.92f);
            }
            s_vrBladeValid = 1;
            memcpy(bladeHandW, s_vrBladeHiltW, sizeof(bladeHandW));
            memcpy(bladeAimW, s_vrBladeAimW, sizeof(bladeAimW));
        }

        if (gevrLnGetHandPoseWorld(fCtrl, s_vrForcePosW, fr, fu, ff)) {
            lnNorm3(ff, s_vrForceAimW);
            s_vrForceValid = 1;
            memcpy(forceHandW, s_vrForcePosW, sizeof(forceHandW));
            memcpy(forceDirW, s_vrForceAimW, sizeof(forceDirW));

            if (dualOffhand) {
                const float od[3] = {
                    ff[0] * 0.35f + fu[0] * 0.94f,
                    ff[1] * 0.35f + fu[1] * 0.94f,
                    ff[2] * 0.35f + fu[2] * 0.94f
                };
                memcpy(s_vrOffHiltW, s_vrForcePosW, sizeof(s_vrOffHiltW));
                lnNorm3(od, s_vrOffDirW);
                const float oLen = (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) ? (81.0f * s_fpIgnite.extend + 24.0f) : 70.0f;
                for (int k = 0; k < 3; k++) {
                    s_vrOffTipW[k] = s_vrOffHiltW[k] + s_vrOffDirW[k] * oLen;
                }
                s_vrOffBladeValid = 1;
            }
        }

        if (!bDown && s_vrTrigWas[bCtrl] && gevrLnVrWristThrow(bVelCm, bladeAimW, 1)) {
            bladeThrow = 1;
        }
        const float bSpeed = sqrtf(lnDot3(bVelCm, bVelCm));
        const float fSpeed = sqrtf(lnDot3(fVelCm, fVelCm));
        vrSwing01 = fminf(1.0f, fmaxf(bSpeed, fSpeed) / 220.0f);

        /* Real tracked VR blade collision */
        const int allowPrimaryBladeHit = s_vrBladeValid && !bDown && !s_thrown.active
            && !(VrLaserswordNinjaMode == GEVR_LN_NINJA && gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_SHADOW)
            && !(VrLaserswordNinjaMode == GEVR_LN_NINJA && gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_BOOMERANG);
        if (allowPrimaryBladeHit && bSpeed >= 105.0f) {
            float vDir[3];
            lnNorm3(bVelCm, vDir);
            if (s_vrWhooshCd[0] <= 0.0f && bSpeed >= 145.0f) {
                lnSfx(VrLaserswordNinjaMode == GEVR_LN_LASERSWORD ? GEVR_LN_SFX_SLASH_LASERSWORD : GEVR_LN_SFX_SLASH_STEEL, fminf(1.3f, bSpeed / 180.0f));
                s_vrWhooshCd[0] = 0.24f;
            }
            lnVrSweepBladeAgainstGuards(0, s_vrBladeHiltW, s_vrBladeTipW, s_vrBladeBackW, dbl,
                                        s_prevTipW[0], s_prevTipValid[0], vDir, 14.0f, bCtrl);
        } else if (VrLaserswordNinjaMode == GEVR_LN_NINJA
                   && (gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_BOOMERANG || gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_SHADOW)
                   && !bDown && bSpeed >= 150.0f && s_vrWhooshCd[0] <= 0.0f) {
            bladeTap = 1;
            s_vrWhooshCd[0] = 0.30f;
        }
        if (s_vrBladeValid) {
            memcpy(s_prevTipW[0], s_vrBladeTipW, sizeof(s_prevTipW[0]));
            s_prevTipValid[0] = 1;
        } else {
            s_prevTipValid[0] = 0;
        }

        if (dualOffhand && s_vrOffBladeValid && !fDown && fSpeed >= 105.0f) {
            float vDir[3];
            lnNorm3(fVelCm, vDir);
            if (s_vrWhooshCd[1] <= 0.0f && fSpeed >= 145.0f) {
                lnSfx(VrLaserswordNinjaMode == GEVR_LN_LASERSWORD ? GEVR_LN_SFX_SLASH_LASERSWORD : GEVR_LN_SFX_SLASH_STEEL, fminf(1.2f, fSpeed / 180.0f));
                s_vrWhooshCd[1] = 0.24f;
            }
            lnVrSweepBladeAgainstGuards(1, s_vrOffHiltW, s_vrOffTipW, NULL, 0,
                                        s_prevTipW[1], s_prevTipValid[1], vDir, 12.0f, fCtrl);
        }
        if (s_vrOffBladeValid) {
            memcpy(s_prevTipW[1], s_vrOffTipW, sizeof(s_prevTipW[1]));
            s_prevTipValid[1] = 1;
        } else {
            s_prevTipValid[1] = 0;
        }

        offHold = fDown;
        if (!fDown && s_vrTrigWas[fCtrl]) {
            offRelease = 1;
            if (gevrLnVrWristThrow(fVelCm, forceDirW, 1)) offDouble = 1;
        }
        if (fDown && s_vrPowerCd <= 0.0f && gevrLnVrWristPull(fVelCm, forceDirW, 1)) {
            s_vrPowerCd = 0.25f;
        }
        s_vrTrigWas[bCtrl] = bDown;
        s_vrTrigWas[fCtrl] = fDown;
    }

    s_blocking = bladeBlock && !s_thrown.active;
    s_ninjaShieldHeld = lnHasNinjaShield() && offHold;
    if (s_blocking || s_ninjaShieldHeld) s_blockHoldSec += dt;
    else s_blockHoldSec = 0.0f;

    /* Ignition animation + snap-hiss SFX */
    if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) {
        const float prevExt = s_fpIgnite.extend;
        gevrLnIgnitionUpdate(&s_fpIgnite, !s_thrown.active, 1, dt);
        if (prevExt <= 0.01f && s_fpIgnite.extend > 0.01f) {
            lnSfx(GEVR_LN_SFX_IGNITE, 1.0f);
        }
    }

    /* Enemy Blade Lock clash resolution */
    if (s_bladeLockTimer > 0.0f) {
        if (!lnChrAliveValid(s_bladeLockChr, s_bladeLockChrnum)) {
            s_bladeLockTimer = 0.0f;
            s_bladeLockChr = NULL;
        } else {
            s_bladeLockTimer -= dt;
            s_bladeLockPulse -= dt;
            if (s_bladeLockPulse <= 0.0f) {
                s_bladeLockPulse = 0.14f;
                const float clashPt[3] = {
                    (eye[0] + s_bladeLockChr->prop->pos.x) * 0.5f,
                    s_bladeLockChr->prop->pos.y + 115.0f,
                    (eye[2] + s_bladeLockChr->prop->pos.z) * 0.5f
                };
                lnAddSpark(clashPt, 0xFFF480FFu, 26.0f, 0.18f);
                lnHaptic(bCtrl, 0.65f, 0.04f);
            }
            const float pushIn = lnDot3(bVelCm, bladeAimW);
            if (bladeTap || pushIn > 150.0f) {
                float dir[3] = { s_bladeLockChr->prop->pos.x - eye[0], 0.0f, s_bladeLockChr->prop->pos.z - eye[2] };
                lnNorm3(dir, dir);
                lnHitGuardWithBlade(s_bladeLockChr, dir, 18.0f, 0, 1, NULL);
                LnSpecialNpc *sp = lnFindSpecial(s_bladeLockChr);
                if (sp) sp->meleeCd = 1.6f;
                lnSfx(GEVR_LN_SFX_CLASH_LASERSWORD, 1.5f);
                lnHaptic(bCtrl, 1.0f, 0.12f);
                s_bladeLockTimer = 0.0f;
                s_bladeLockChr = NULL;
            }
        }
    }

    /* Shadow-Step mobility activation in Ninja mode */
    if (VrLaserswordNinjaMode == GEVR_LN_NINJA && VrNinjaShadowStep) {
        const int shadowSlot = (gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_SHADOW);
        if ((shadowSlot && (bladeTap || offDouble))
            || (offDouble && !s_chokeActive && s_grabBodySlot < 0)) {
            lnActivateShadowStep(eye, fwd);
        }
    }

    /* Sample world-space motion ribbons once per tick (fixes stereo per-eye trail corruption) */
    if (VrLnBladeTrails && !s_thrown.active) {
        if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && s_fpIgnite.extend > 0.25f) {
            if (s_vrBladeValid) {
                const GevrLnPack *pk = gevrLnPackGet();
                const float emitOff = (pk != NULL)
                    ? pk->meshes[dbl ? GEVR_LN_MESH_HILT_DOUBLE : GEVR_LN_MESH_HILT_SINGLE].emitterOffsetCm
                    : (dbl ? 16.0f : 13.5f);
                const float emitW[3] = {
                    s_vrBladeHiltW[0] + s_vrBladeDirW[0] * emitOff,
                    s_vrBladeHiltW[1] + s_vrBladeDirW[1] * emitOff,
                    s_vrBladeHiltW[2] + s_vrBladeDirW[2] * emitOff
                };
                lnTrailPushWorld(0, emitW, s_vrBladeTipW);
                if (dbl) {
                    const float emitB[3] = {
                        s_vrBladeHiltW[0] - s_vrBladeDirW[0] * emitOff,
                        s_vrBladeHiltW[1] - s_vrBladeDirW[1] * emitOff,
                        s_vrBladeHiltW[2] - s_vrBladeDirW[2] * emitOff
                    };
                    lnTrailPushWorld(1, emitB, s_vrBladeBackW);
                }
            }
            if (VrLaserswordStyle == GEVR_LN_STYLE_DUAL && s_vrOffBladeValid) {
                lnTrailPushWorld(2, s_vrOffHiltW, s_vrOffTipW);
            }
        } else if (VrLaserswordNinjaMode == GEVR_LN_NINJA && gevrLnSelectedNinjaSlot != GEVR_LN_NINJA_SLOT_BOOMERANG) {
            if (s_vrBladeValid) {
                lnTrailPushWorld(0, s_vrBladeHiltW, s_vrBladeTipW);
            }
            if (gevrLnIsDualSword(GEVR_LN_NINJA, sword) && s_vrOffBladeValid) {
                lnTrailPushWorld(2, s_vrOffHiltW, s_vrOffTipW);
            }
        }
    }

    /* Wall / floor plasma scorch sparks along actual tracked blade segment */
    if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLnBladeTrails && !s_thrown.active
        && s_fpIgnite.extend > 0.45f && s_vrBladeValid && pdphysIsReady()) {
        float hitPos[3];
        int hit = pdphysRaycastAnyRoom(s_vrBladeHiltW, s_vrBladeTipW, hitPos, NULL, NULL);
        if (!hit && dbl) {
            hit = pdphysRaycastAnyRoom(s_vrBladeHiltW, s_vrBladeBackW, hitPos, NULL, NULL);
        }
        if (hit && s_wallSparkCd <= 0.0f) {
            lnAddSpark(hitPos, 0xFF9838FFu, 15.0f, 0.14f);
            lnHaptic(bCtrl, 0.25f, 0.02f);
            s_wallSparkCd = 0.055f;
        }
    }

    /* Throw Lasersword / Ninja Boomerang */
    const int wantThrow = bladeThrow || (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && gevrLnSelectedPower == GEVR_LN_FORCE_THROW && offDouble)
        || (VrLaserswordNinjaMode == GEVR_LN_NINJA && gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_BOOMERANG && bladeTap);
    if (wantThrow && !s_thrown.active && !s_blocking) {
        const int isBoom = (VrLaserswordNinjaMode == GEVR_LN_NINJA);
        gevrLnThrownLaunch(&s_thrown, isBoom, bladeHandW, bladeAimW);
        s_thrownBounces = 0;
        s_throwHitCount = 0;
        lnTrailClear();
        lnSfx(isBoom ? GEVR_LN_SFX_SLASH_STEEL : GEVR_LN_SFX_SLASH_LASERSWORD, 1.2f);
        lnHaptic(bCtrl, 0.7f, 0.08f);
    }

    /* Update flying thrown blade / boomerang (+ corridor wall ricochets) */
    if (s_thrown.active) {
        for (int i = 0; i < s_throwHitCount; i++) {
            if (s_throwHitCd[i] > 0.0f) s_throwHitCd[i] -= dt;
        }
        float prev[3];
        memcpy(prev, s_thrown.pos, sizeof(prev));
        int hitWall = 0;
        if (!s_thrown.returning && pdphysIsReady()) {
            float nxt[3] = {
                prev[0] + s_thrown.vel[0] * dt,
                prev[1] + s_thrown.vel[1] * dt,
                prev[2] + s_thrown.vel[2] * dt
            };
            float hitPos[3], hitN[3];
            if (pdphysRaycastAnyRoom(prev, nxt, hitPos, hitN, NULL)) {
                if (s_thrown.isBoomerang && VrNinjaRicochet && s_thrownBounces < 2
                    && fabsf(lnDot3(s_thrown.dir, hitN)) < 0.72f) {
                    float refl[3];
                    gevrLnReflectVec(s_thrown.dir, hitN, refl);
                    lnNorm3(refl, s_thrown.dir);
                    s_thrown.vel[0] = s_thrown.dir[0] * s_thrown.speed;
                    s_thrown.vel[1] = s_thrown.dir[1] * s_thrown.speed;
                    s_thrown.vel[2] = s_thrown.dir[2] * s_thrown.speed;
                    s_thrown.pos[0] = hitPos[0] + hitN[0] * 8.0f;
                    s_thrown.pos[1] = hitPos[1] + hitN[1] * 8.0f;
                    s_thrown.pos[2] = hitPos[2] + hitN[2] * 8.0f;
                    s_thrownBounces++;
                    lnAddSpark(hitPos, 0xFFE870FFu, 20.0f, 0.20f);
                    lnSfx(GEVR_LN_SFX_CLASH_METAL, 0.95f);
                } else {
                    hitWall = 1;
                    lnAddSpark(hitPos, s_thrown.isBoomerang ? 0xFFE870FFu : 0xFF9838FFu, 22.0f, 0.20f);
                    lnSfx(s_thrown.isBoomerang ? GEVR_LN_SFX_CLASH_METAL : GEVR_LN_SFX_CLASH_LASERSWORD, 0.9f);
                }
            }
        }
        gevrLnThrownStep(&s_thrown, bladeHandW, hitWall, dt);

        const s32 n = get_numguards();
        for (s32 i = 0; i < n; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (!lnIsEnemyGuard(c)) continue;
            const float dx = c->prop->pos.x - s_thrown.pos[0];
            const float dy = (c->prop->pos.y + 100.0f) - s_thrown.pos[1];
            const float dz = c->prop->pos.z - s_thrown.pos[2];
            if (dx * dx + dy * dy + dz * dz > 85.0f * 85.0f) continue;

            int cooldownIdx = -1;
            for (int h = 0; h < s_throwHitCount; h++) {
                if (s_throwHitChr[h] == c) { cooldownIdx = h; break; }
            }
            if (cooldownIdx >= 0 && s_throwHitCd[cooldownIdx] > 0.0f) continue;
            if (cooldownIdx < 0 && s_throwHitCount < 12) {
                cooldownIdx = s_throwHitCount++;
                s_throwHitChr[cooldownIdx] = c;
            }
            if (cooldownIdx >= 0) s_throwHitCd[cooldownIdx] = 0.25f;

            float dir[3];
            lnNorm3(s_thrown.vel, dir);
            lnHitGuardWithBlade(c, dir, 18.0f, 2, 1, s_thrown.pos);
        }
    }

    /* Force Lightning vs Telekinesis (Force Grab/Choke, Pull, Push, Limb Ball) */
    const int allowForce = (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLaserswordForcePowers != 0)
        || (VrLaserswordNinjaMode == GEVR_LN_NINJA && !lnHasNinjaShield());
    const int useLightning = (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && gevrLnSelectedPower == GEVR_LN_FORCE_LIGHTNING);
    const float *forceOrigin = s_vrForceValid ? forceHandW : eye;
    const float *forceAim = s_vrForceValid ? forceDirW : fwd;

    s_lightningActive = 0;
    s_lightningCount = 0;

    if (!allowForce) {
        s_chokeActive = 0;
        s_chokeChr = NULL;
        s_grabBodySlot = -1;
        s_caughtProjIdx = -1;
    } else if (useLightning && offHold) {
        float pOrigin[3];
        StandTile *pStan = NULL;
        lnGetPlayerTileOrigin(pOrigin, &pStan);

        s_lightningActive = 1;
        s_lightningTickAccum += dt;
        const int doDmg = (s_lightningTickAccum >= 0.12f);
        if (doDmg) s_lightningTickAccum = 0.0f;

        const s32 n = get_numguards();
        for (s32 i = 0; i < n && s_lightningCount < LN_MAX_LIGHTNING; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (!lnIsEnemyGuard(c)) continue;
            const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
            const float dx = tgt[0] - forceOrigin[0];
            const float dy = tgt[1] - forceOrigin[1];
            const float dz = tgt[2] - forceOrigin[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 900.0f * 900.0f || d2 < 1.0f) continue;
            const float d = sqrtf(d2);
            const float dir[3] = { dx / d, dy / d, dz / d };
            if (lnDot3(forceAim, dir) < 0.65f) continue;
            if (!lnHasLos(forceOrigin, pStan, pOrigin, tgt)) continue;
            s_lightningChrs[s_lightningCount] = c;
            s_lightningChrnums[s_lightningCount] = c->chrnum;
            s_lightningCount++;
            if (doDmg) {
                lnHitGuardWithBlade(c, dir, 0.55f, 0, 0, NULL);
            }
        }
    } else if (allowForce) {
        float pOrigin[3];
        StandTile *pStan = NULL;
        lnGetPlayerTileOrigin(pOrigin, &pStan);

        float chokeTarget[3];
        gevrLnChokeTargetPos(forceOrigin, forceAim, chokeTarget);
        float ballCenter[3] = {
            forceOrigin[0] + forceAim[0] * 105.0f,
            forceOrigin[1] + forceAim[1] * 105.0f - 8.0f,
            forceOrigin[2] + forceAim[2] * 105.0f
        };

        if (offHold) {
            if (s_chokeActive) {
                if (!lnChrAliveValid(s_chokeChr, s_chokeChrnum)) {
                    s_chokeActive = 0;
                    s_chokeChr = NULL;
                } else {
                    s_chokeHoldSec += dt;
                    float cur[3] = { s_chokeChr->prop->pos.x, s_chokeChr->prop->pos.y, s_chokeChr->prop->pos.z };
                    float footTarget[3] = { chokeTarget[0], chokeTarget[1] - 110.0f, chokeTarget[2] };
                    const float blend = fminf(1.0f, dt * 10.0f);
                    float next[3] = {
                        cur[0] + (footTarget[0] - cur[0]) * blend,
                        cur[1] + (footTarget[1] - cur[1]) * blend,
                        cur[2] + (footTarget[2] - cur[2]) * blend
                    };
                    lnMoveChrProp(s_chokeChr, next);
                    s_chokeChr->actiontype = ACT_STAND;
                    chrSetFiring(s_chokeChr, GUNRIGHT, FALSE);
                    chrSetFiring(s_chokeChr, GUNLEFT, FALSE);
                    s_chokeChr->damage += dt * 1.4f;

                    /* VR Force Slam downward gesture while choking */
                    if (VrLaserswordForcePowers && fVelCm[1] < -200.0f && s_chokeHoldSec > 0.15f) {
                        const float slamDir[3] = { 0.0f, -1.0f, 0.0f };
                        const float slamVel[3] = { 0.0f, -900.0f, 0.0f };
                        memcpy(s_thrownBodyStart, s_chokeChr->prop->pos.f, sizeof(s_thrownBodyStart));
                        lnHitGuardWithBlade(s_chokeChr, slamDir, 45.0f, 0, 0, NULL);
                        int bslot = gevrRagdollFindNearestBody(s_thrownBodyStart, slamDir, 260.0f, -1.0f, NULL);
                        if (bslot >= 0) gevrRagdollThrowBody(bslot, slamVel);
                        lnAddSpark(s_thrownBodyStart, 0x88C0FFFFu, 32.0f, 0.25f);
                        lnSfx(GEVR_LN_SFX_FORCE_PUSH, 1.35f);
                        lnHaptic(fCtrl, 1.0f, 0.12f);
                        s_chokeActive = 0;
                        s_chokeChr = NULL;
                    }
                }
            } else if (s_grabBodySlot >= 0) {
                float p[3];
                if (gevrRagdollGetBodyPos(s_grabBodySlot, p)) {
                    gevrRagdollPullBody(s_grabBodySlot, chokeTarget, 1600.0f, 7.5f);
                } else {
                    s_grabBodySlot = -1;
                }
            } else if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLaserswordCatchProjectiles && s_caughtProjIdx >= 0) {
                if (lnProjValidAirborne(s_caughtProjIdx)) {
                    Projectile *pr = &g_Projectiles[s_caughtProjIdx];
                    pr->speed.x = (forceOrigin[0] + forceAim[0] * 45.0f - pr->obj->prop->pos.x) * 0.14f;
                    pr->speed.y = (forceOrigin[1] + forceAim[1] * 45.0f - pr->obj->prop->pos.y) * 0.14f;
                    pr->speed.z = (forceOrigin[2] + forceAim[2] * 45.0f - pr->obj->prop->pos.z) * 0.14f;
                } else {
                    s_caughtProjIdx = -1;
                }
            } else {
                int caughtProj = 0;
                if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD && VrLaserswordCatchProjectiles) {
                    float bestD2 = 1100.0f * 1100.0f;
                    int bestIdx = -1;
                    for (int pi = 0; pi < PROJECTILES_ARR_MAX; pi++) {
                        if (!lnProjValidAirborne(pi)) continue;
                        Projectile *pr = &g_Projectiles[pi];
                        const float dx = pr->obj->prop->pos.x - forceOrigin[0];
                        const float dy = pr->obj->prop->pos.y - forceOrigin[1];
                        const float dz = pr->obj->prop->pos.z - forceOrigin[2];
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 >= bestD2 || d2 < 4.0f) continue;
                        const float d = sqrtf(d2);
                        if ((dx * forceAim[0] + dy * forceAim[1] + dz * forceAim[2]) / d < 0.72f) continue;
                        if (!lnHasLos(forceOrigin, pStan, pOrigin, pr->obj->prop->pos.f)) continue;
                        bestD2 = d2;
                        bestIdx = pi;
                    }
                    if (bestIdx >= 0) {
                        s_caughtProjIdx = bestIdx;
                        caughtProj = 1;
                        lnHaptic(fCtrl, 0.5f, 0.05f);
                    }
                }
                ChrRecord *closeNpc = caughtProj ? NULL : lnFindForceTarget(forceOrigin, forceAim, GEVR_LN_GRAB_RANGE_CM, 0.72f);
                if (caughtProj) {
                    /* held by off-hand */
                } else if (closeNpc) {
                    s_chokeChr = closeNpc;
                    s_chokeChrnum = closeNpc->chrnum;
                    s_chokeActive = 1;
                    s_chokeHoldSec = 0.0f;
                    lnHaptic(fCtrl, 0.5f, 0.06f);
                } else {
                    float bodyPos[3];
                    int bslot = gevrRagdollFindNearestBody(forceOrigin, forceAim, 900.0f, 0.75f, bodyPos);
                    if (bslot >= 0 && lnHasLos(forceOrigin, pStan, pOrigin, bodyPos)) {
                        s_grabBodySlot = bslot;
                        gevrRagdollPullBody(bslot, chokeTarget, 1600.0f, 7.5f);
                    } else if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) {
                        int pulled = gevrRagdollPullLooseLimbs(forceOrigin, ballCenter, GEVR_LN_PULL_RANGE_CM, s_time, GEVR_LN_MAX_LIMBS);
                        if (pulled > 0) {
                            s_limbBallSuspended = 1;
                            memcpy(s_limbBallPos, ballCenter, sizeof(s_limbBallPos));
                        }
                        ChrRecord *farNpc = lnFindForceTarget(forceOrigin, forceAim, GEVR_LN_PULL_RANGE_CM, 0.84f);
                        if (farNpc) {
                            float p[3] = { farNpc->prop->pos.x, farNpc->prop->pos.y, farNpc->prop->pos.z };
                            const float toHand[3] = { forceOrigin[0] - p[0], forceOrigin[1] - 100.0f - p[1], forceOrigin[2] - p[2] };
                            float nd[3];
                            const float d = lnNorm3(toHand, nd);
                            if (d > 140.0f) {
                                const float step = fminf(d - 110.0f, 680.0f * dt);
                                float nxt[3] = { p[0] + nd[0] * step, p[1] + nd[1] * step, p[2] + nd[2] * step };
                                lnMoveChrProp(farNpc, nxt);
                            }
                        }
                    }
                }
            }
        }

        if (offRelease) {
            if (s_chokeActive && lnChrAliveValid(s_chokeChr, s_chokeChrnum)) {
                float launchDir[3] = { forceAim[0], forceAim[1], forceAim[2] };
                if (sqrtf(lnDot3(fVelCm, fVelCm)) > 240.0f) {
                    lnNorm3(fVelCm, launchDir);
                }
                const float throwVel[3] = { launchDir[0] * 1600.0f, launchDir[1] * 1600.0f + 160.0f, launchDir[2] * 1600.0f };
                memcpy(s_thrownBodyStart, s_chokeChr->prop->pos.f, sizeof(s_thrownBodyStart));
                lnHitGuardWithBlade(s_chokeChr, launchDir, 40.0f, 0, 0, NULL);
                int bslot = gevrRagdollFindNearestBody(s_thrownBodyStart, launchDir, 260.0f, -1.0f, NULL);
                if (bslot >= 0) {
                    gevrRagdollThrowBody(bslot, throwVel);
                    s_thrownBodySlot = bslot;
                    s_thrownBodyAge = 0.0f;
                    s_thrownBodyHitCount = 0;
                    gevrLnThrownBodyInit(&s_thrownBody, 80.0f, 1600.0f);
                }
                lnSfx(GEVR_LN_SFX_FORCE_PUSH, 1.0f);
                s_chokeActive = 0;
                s_chokeChr = NULL;
                lnHaptic(fCtrl, 0.9f, 0.10f);
            } else if (s_grabBodySlot >= 0) {
                float launchDir[3] = { forceAim[0], forceAim[1], forceAim[2] };
                if (sqrtf(lnDot3(fVelCm, fVelCm)) > 240.0f) {
                    lnNorm3(fVelCm, launchDir);
                }
                const float throwVel[3] = { launchDir[0] * 1600.0f, launchDir[1] * 1600.0f + 180.0f, launchDir[2] * 1600.0f };
                gevrRagdollGetBodyPos(s_grabBodySlot, s_thrownBodyStart);
                gevrRagdollThrowBody(s_grabBodySlot, throwVel);
                s_thrownBodySlot = s_grabBodySlot;
                s_thrownBodyAge = 0.0f;
                s_thrownBodyHitCount = 0;
                gevrLnThrownBodyInit(&s_thrownBody, 80.0f, 1600.0f);
                lnSfx(GEVR_LN_SFX_FORCE_PUSH, 0.95f);
                s_grabBodySlot = -1;
                lnHaptic(fCtrl, 0.9f, 0.10f);
            } else if (s_caughtProjIdx >= 0) {
                if (lnProjValidAirborne(s_caughtProjIdx) && g_CurrentPlayer && g_CurrentPlayer->prop) {
                    Projectile *pr = &g_Projectiles[s_caughtProjIdx];
                    pr->speed.x = forceAim[0] * 28.0f;
                    pr->speed.y = forceAim[1] * 28.0f;
                    pr->speed.z = forceAim[2] * 28.0f;
                    pr->ownerprop = g_CurrentPlayer->prop;
                    lnAddSpark(pr->obj->prop->pos.f, 0x88E0FFFFu, 22.0f, 0.22f);
                    lnSfx(GEVR_LN_SFX_FORCE_PUSH, 1.05f);
                }
                s_caughtProjIdx = -1;
            }
            s_chokeActive = 0;
            s_chokeChr = NULL;
            s_grabBodySlot = -1;
        }

        if (s_limbBallSuspended && VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) {
            int kept = gevrRagdollPullLooseLimbs(forceOrigin, ballCenter, 360.0f, s_time, GEVR_LN_MAX_LIMBS);
            if (kept <= 0 && !offHold) {
                s_limbBallSuspended = 0;
            } else {
                memcpy(s_limbBallPos, ballCenter, sizeof(s_limbBallPos));
            }
        }

        if (offDouble && VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) {
            if (s_limbBallSuspended) {
                gevrRagdollLaunchLooseLimbs(s_limbBallPos, forceAim, 1800.0f, 190.0f);
                const s32 n = get_numguards();
                for (s32 i = 0; i < n; i++) {
                    ChrRecord *c = &g_ChrSlots[i];
                    if (!lnIsEnemyGuard(c)) continue;
                    const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 100.0f, c->prop->pos.z };
                    const float dx = tgt[0] - forceOrigin[0];
                    const float dy = tgt[1] - forceOrigin[1];
                    const float dz = tgt[2] - forceOrigin[2];
                    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                    if (d < 1.0f || d > 950.0f) continue;
                    const float dir[3] = { dx / d, dy / d, dz / d };
                    if (lnDot3(forceAim, dir) < 0.65f) continue;
                    if (!lnHasLos(forceOrigin, pStan, pOrigin, tgt)) continue;
                    const float dmg = gevrLnLimbBlastDamage(d);
                    if (dmg >= 80.0f) {
                        lnHitGuardWithBlade(c, dir, 24.0f, 1, 0, NULL);
                    } else if (dmg > 0.0f) {
                        lnHitGuardWithBlade(c, dir, dmg * 0.05f, 0, 0, NULL);
                    }
                }
                s_limbBallSuspended = 0;
                lnHaptic(fCtrl, 1.0f, 0.12f);
            } else {
                lnTriggerForcePush(forceOrigin, forceAim);
            }
        }
    }

    /* Thrown body momentum & dismemberment impacts */
    if (s_thrownBodySlot >= 0) {
        s_thrownBodyAge += dt;
        float bpos[3];
        if (s_thrownBodyAge > 1.2f || !gevrRagdollGetBodyPos(s_thrownBodySlot, bpos)) {
            s_thrownBodySlot = -1;
        } else {
            const float tdx = bpos[0] - s_thrownBodyStart[0];
            const float tdy = bpos[1] - s_thrownBodyStart[1];
            const float tdz = bpos[2] - s_thrownBodyStart[2];
            const float travel = sqrtf(tdx * tdx + tdy * tdy + tdz * tdz);
            const s32 n = get_numguards();
            for (s32 i = 0; i < n; i++) {
                ChrRecord *c = &g_ChrSlots[i];
                if (!lnIsEnemyGuard(c)) continue;
                int seen = 0;
                for (int h = 0; h < s_thrownBodyHitCount; h++) {
                    if (s_thrownBodyHit[h] == c) { seen = 1; break; }
                }
                if (seen) continue;
                const float dx = c->prop->pos.x - bpos[0];
                const float dy = (c->prop->pos.y + 90.0f) - bpos[1];
                const float dz = c->prop->pos.z - bpos[2];
                if (dx * dx + dy * dy + dz * dz > 110.0f * 110.0f) continue;

                if (s_thrownBodyHitCount < 8) s_thrownBodyHit[s_thrownBodyHitCount++] = c;
                float knock = 0.0f;
                const int impact = gevrLnThrownBodyImpact(&s_thrownBody, travel, &knock);
                if (impact == GEVR_LN_IMPACT_DISMEMBER) {
                    lnHitGuardWithBlade(c, forceAim, 24.0f, 3, 0, NULL);
                } else if (impact == GEVR_LN_IMPACT_RAGDOLL_KILL) {
                    lnHitGuardWithBlade(c, forceAim, 12.0f, 0, 0, NULL);
                } else if (impact == GEVR_LN_IMPACT_KNOCKDOWN) {
                    lnHitGuardWithBlade(c, forceAim, 0.8f, 0, 0, NULL);
                }
                if (knock <= 120.0f) {
                    s_thrownBodySlot = -1;
                    break;
                }
            }
        }
    }

    /* Update pack clip audio mixer state */
    s_swingAudio01 = vrSwing01;
    if (VrLnAudio) {
        gevrLnAudioSetState(1, VrLaserswordNinjaMode == GEVR_LN_LASERSWORD, s_fpIgnite.extend, s_swingAudio01,
                            (s_chokeActive || s_grabBodySlot >= 0 || s_limbBallSuspended || s_caughtProjIdx >= 0) ? 1 : 0,
                            s_lightningActive, s_thrown.active, s_thrown.isBoomerang);
    } else {
        gevrLnAudioSetState(0, 0, 0.0f, 0.0f, 0, 0, 0, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* Bullet & Melee Interception                                                */
/* -------------------------------------------------------------------------- */

s32 gevrLnInterceptPlayerDamage(f32 damageAmount, f32 vecX, f32 vecZ)
{
    (void)damageAmount;
    if (!lnModeActive() || lnUsingGunsOverride() || !lnInGameplay()) return 0;

    float eye[3], right[3], up[3], fwd[3];
    lnGetPlayerCameraWorld(eye, right, up, fwd);

    /* Ninja bullet-slice mid-swing during fast VR tracked blade swing */
    const int vrSwinging = s_vrBladeValid && s_vrWhooshCd[0] > 0.05f;
    if (VrLaserswordNinjaMode == GEVR_LN_NINJA && VrNinjaBulletSlice && vrSwinging) {
        const float cutPt[3] = {
            (s_vrBladeHiltW[0] + s_vrBladeTipW[0]) * 0.5f,
            (s_vrBladeHiltW[1] + s_vrBladeTipW[1]) * 0.5f,
            (s_vrBladeHiltW[2] + s_vrBladeTipW[2]) * 0.5f
        };
        lnAddSpark(cutPt, 0xF0F8FFFFu, 26.0f, 0.18f);
        lnSfx(GEVR_LN_SFX_CLASH_METAL, 1.15f);
        lnHaptic(VrLaserswordHand ? 0 : 1, 0.7f, 0.06f);
        return 1;
    }

    if (!s_vrBladeValid && !s_vrForceValid) return 0;

    /* GoldenEye record_damage_kills passes bullet travel vector (shooter -> player);
     * negate to obtain direction from player toward the shooter. */
    const float travel[3] = { vecX, 0.0f, vecZ };
    float toShooter[3];
    if (!gevrLnBulletTravelToShooter(travel, toShooter)) {
        toShooter[0] = fwd[0];
        toShooter[1] = fwd[1];
        toShooter[2] = fwd[2];
    }

    float reflectDir[3] = { 0.0f, 0.0f, 0.0f };
    const float *bDir = s_vrBladeValid ? s_vrBladeDirW : fwd;
    const float *handAim = s_ninjaShieldHeld
        ? (s_vrForceValid ? s_vrForceAimW : fwd)
        : (s_vrBladeValid ? s_vrBladeAimW : fwd);
    const int res = gevrLnCheckVrBladeBlock(VrLaserswordNinjaMode, s_blocking || s_ninjaShieldHeld,
                                            s_ninjaShieldHeld, fwd, bDir, handAim, toShooter, reflectDir);
    if (res == GEVR_LN_BLOCK_NONE) return 0;

    const int perfect = (VrLnPerfectParry != 0 && s_blockHoldSec <= 0.22f && res == GEVR_LN_BLOCK_DEFLECT);
    float bladePt[3];
    if (s_ninjaShieldHeld && s_vrForceValid) {
        memcpy(bladePt, s_vrForcePosW, sizeof(bladePt));
    } else if (s_vrBladeValid) {
        bladePt[0] = s_vrBladeHiltW[0] + s_vrBladeDirW[0] * 55.0f;
        bladePt[1] = s_vrBladeHiltW[1] + s_vrBladeDirW[1] * 55.0f;
        bladePt[2] = s_vrBladeHiltW[2] + s_vrBladeDirW[2] * 55.0f;
    } else {
        memcpy(bladePt, s_vrForcePosW, sizeof(bladePt));
    }

    unsigned int core = 0xFFFFFFFFu, glow = 0xFFD860FFu;
    if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) {
        gevrLnLaserswordPalette(VrLaserswordColor, &core, &glow);
    }
    lnAddSpark(bladePt, perfect ? 0xFFF480FFu : (res == GEVR_LN_BLOCK_DEFLECT && VrLaserswordNinjaMode == GEVR_LN_LASERSWORD ? glow : 0xFFE860FFu),
               perfect ? 38.0f : (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD ? 28.0f : 20.0f), 0.22f);
    lnSfx(res == GEVR_LN_BLOCK_DEFLECT ? GEVR_LN_SFX_DEFLECT : GEVR_LN_SFX_CLASH_METAL, perfect ? 1.45f : 1.0f);
    lnHaptic(VrLaserswordHand ? 0 : 1, perfect ? 1.0f : 0.85f, perfect ? 0.10f : 0.07f);

    if (res == GEVR_LN_BLOCK_DEFLECT) {
        ChrRecord *shooter = lnFindForceTarget(bladePt, reflectDir, 2200.0f, 0.70f);
        if (!shooter) {
            shooter = lnFindForceTarget(eye, fwd, 2200.0f, 0.70f);
        }
        if (shooter) {
            const float tgt[3] = {
                shooter->prop->pos.x,
                shooter->prop->pos.y + 115.0f,
                shooter->prop->pos.z
            };
            const float dir[3] = { tgt[0] - bladePt[0], tgt[1] - bladePt[1], tgt[2] - bladePt[2] };
            float nd[3];
            lnNorm3(dir, nd);
            const unsigned int boltCol = gevrLnLasersActive()
                ? (perfect ? 0xFFF480FFu : 0x58E8FFFFu)
                : (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD ? glow : 0xFFD840FFu);
            lnAddTracer(bladePt, tgt, boltCol, 0.18f);
            lnHitGuardWithBlade(shooter, nd, perfect ? 18.0f : 3.5f, 0, 1, NULL);
        } else {
            const float farPt[3] = {
                bladePt[0] + reflectDir[0] * 900.0f,
                bladePt[1] + reflectDir[1] * 900.0f,
                bladePt[2] + reflectDir[2] * 900.0f
            };
            const unsigned int boltCol = gevrLnLasersActive()
                ? (perfect ? 0xFFF480FFu : 0x58E8FFFFu)
                : glow;
            lnAddTracer(bladePt, farPt, boltCol, 0.16f);
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Fast3D Shaded Geometry Helpers                                             */
/* -------------------------------------------------------------------------- */

static void lnSetVtx(Vtx *v, f32 x, f32 y, f32 z, u32 rgba)
{
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = (rgba >> 24) & 0xff;
    v->v.cn[1] = (rgba >> 16) & 0xff;
    v->v.cn[2] = (rgba >> 8) & 0xff;
    v->v.cn[3] = rgba & 0xff;
}

static Gfx *lnBeginGeom(Gfx *gdl, const float originView[3], float scale, int translucent)
{
    Mtxf mf;
    Mtx *mv = dynAllocateMatrix();
    if (!mv) return NULL;
    matrix_4x4_set_identity(&mf);
    mf.m[0][0] = mf.m[1][1] = mf.m[2][2] = 0.1f * scale; /* vertices in mm */
    mf.m[3][0] = originView[0];
    mf.m[3][1] = originView[1];
    mf.m[3][2] = originView[2];
    guMtxF2L(mf.m, mv);
    gSPMatrix(gdl++, osVirtualToPhysical((void *)currentPlayerGetProjectionMatrix()), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gdl++, osVirtualToPhysical(mv), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    if (translucent) {
        gDPSetRenderMode(gdl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    } else {
        gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    }
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    return gdl;
}

static void lnBuildBasis(const float tipDir[3], const float refRight[3], float bx[3], float by[3], float bz[3])
{
    lnNorm3(tipDir, by);
    float rx[3] = { 1.0f, 0.0f, 0.0f };
    if (refRight && lnDot3(refRight, refRight) > 0.01f) lnNorm3(refRight, rx);
    if (fabsf(lnDot3(rx, by)) > 0.94f) {
        rx[0] = fabsf(by[0]) < 0.9f ? 1.0f : 0.0f;
        rx[1] = 0.0f;
        rx[2] = fabsf(by[0]) < 0.9f ? 0.0f : 1.0f;
    }
    lnCross3(rx, by, bz);
    lnNorm3(bz, bz);
    lnCross3(by, bz, bx);
    lnNorm3(bx, bx);
}

static Gfx *lnEmitBladePass(Gfx *gdl, const float aMm[3], const float dir[3], float lenMm,
                            float r0Mm, float r1Mm, u32 rgbaBase, u32 rgbaTip)
{
    Vtx *v = dynAllocateVertices(12);
    if (!v) return gdl;
    float u[3], f[3], w[3];
    lnBuildBasis(dir, NULL, u, f, w);
    const float bMm[3] = { aMm[0] + f[0] * lenMm, aMm[1] + f[1] * lenMm, aMm[2] + f[2] * lenMm };
    for (int i = 0; i < 6; i++) {
        const float theta = (float)i * (6.2831853f / 6.0f);
        const float ca = cosf(theta), sa = sinf(theta);
        const float nx = u[0] * ca + w[0] * sa;
        const float ny = u[1] * ca + w[1] * sa;
        const float nz = u[2] * ca + w[2] * sa;
        lnSetVtx(&v[i], aMm[0] + nx * r0Mm, aMm[1] + ny * r0Mm, aMm[2] + nz * r0Mm, rgbaBase);
        lnSetVtx(&v[6 + i], bMm[0] + nx * r1Mm, bMm[1] + ny * r1Mm, bMm[2] + nz * r1Mm, rgbaTip);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 12, 0);
    for (int i = 0; i < 6; i++) {
        const int j = (i + 1) % 6;
        gSP2Triangles(gdl++, i, j, 6 + j, 0, i, 6 + j, 6 + i, 0);
    }
    return gdl;
}

static Gfx *lnDrawPackMesh(Gfx *gdl, const GevrLnMesh *mesh, const float bx[3], const float by[3], const float bz[3],
                           float katanaLenCm, int goldTint)
{
    if (!mesh || !mesh->verts || mesh->triCount <= 0) return gdl;
    const float bladeScale = (katanaLenCm > 0.0f) ? fmaxf(0.25f, (katanaLenCm - 24.0f) / (105.0f - 24.0f)) : 1.0f;
    for (int t0 = 0; t0 < mesh->triCount; t0 += 10) {
        const int batch = (mesh->triCount - t0 > 10) ? 10 : (mesh->triCount - t0);
        Vtx *v = dynAllocateVertices(batch * 3);
        if (!v) return gdl;
        for (int k = 0; k < batch * 3; k++) {
            const GevrLnVtx *s = &mesh->verts[t0 * 3 + k];
            const float py = (katanaLenCm > 0.0f && s->p[1] > 24.0f)
                ? (24.0f + (s->p[1] - 24.0f) * bladeScale) : s->p[1];
            unsigned int r = s->rgba[0], g = s->rgba[1], b = s->rgba[2], a = s->rgba[3];
            if (goldTint && r > 70u && b > 70u) {
                r = (r * 290u) / 255u;
                if (r > 255u) r = 255u;
                g = (g * 225u) / 255u;
                b = b / 4u;
            }
            const u32 col = (r << 24) | (g << 16) | (b << 8) | a;
            lnSetVtx(&v[k],
                     (s->p[0] * bx[0] + py * by[0] + s->p[2] * bz[0]) * 10.0f,
                     (s->p[0] * bx[1] + py * by[1] + s->p[2] * bz[1]) * 10.0f,
                     (s->p[0] * bx[2] + py * by[2] + s->p[2] * bz[2]) * 10.0f, col);
        }
        gSPVertex(gdl++, osVirtualToPhysical(v), batch * 3, 0);
        for (int t = 0; t + 1 < batch; t += 2) {
            const int a = t * 3, b = a + 3;
            gSP2Triangles(gdl++, a, a + 1, a + 2, 0, b, b + 1, b + 2, 0);
        }
        if (batch & 1) {
            const int a = (batch - 1) * 3;
            gSP1Triangle(gdl++, a, a + 1, a + 2, 0);
        }
    }
    return gdl;
}

static Gfx *lnDrawLaserswordExView(Gfx *gdl, const float hiltView[3], const float dirView[3], const float refRight[3],
                                   float lenCm, float extend01, int colorIdx, float scale, int doubleEnded)
{
    const GevrLnPack *pk = gevrLnPackGet();
    if (!pk) return gdl;
    const GevrLnMesh *hm = &pk->meshes[doubleEnded ? GEVR_LN_MESH_HILT_DOUBLE : GEVR_LN_MESH_HILT_SINGLE];
    unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
    gevrLnLaserswordPalette(colorIdx, &core, &glow);

    float bx[3], by[3], bz[3];
    lnBuildBasis(dirView, refRight, bx, by, bz);

    Gfx *beg = lnBeginGeom(gdl, hiltView, scale, 0);
    if (!beg) return gdl;
    gdl = lnDrawPackMesh(beg, hm, bx, by, bz, 0.0f, 0);

    if (extend01 > 0.02f) {
        const float emMm = hm->emitterOffsetCm * 10.0f;
        const float rMm = (hm->bladeRadiusCm > 0.1f ? hm->bladeRadiusCm : 2.5f) * 10.0f;
        const float emitter[3] = { by[0] * emMm, by[1] * emMm, by[2] * emMm };
        const float backEmitter[3] = { -by[0] * emMm, -by[1] * emMm, -by[2] * emMm };
        const float negBy[3] = { -by[0], -by[1], -by[2] };
        const float bladeMm = fmaxf(0.0f, lenCm * 10.0f - emMm) * extend01;

        gdl = lnEmitBladePass(gdl, emitter, by, bladeMm, rMm * 0.45f, rMm * 0.25f, core, core);
        if (doubleEnded) {
            gdl = lnEmitBladePass(gdl, backEmitter, negBy, bladeMm * 0.92f, rMm * 0.45f, rMm * 0.25f, core, core);
        }
        beg = lnBeginGeom(gdl, hiltView, scale, 1);
        if (beg) {
            const float pulse = 1.0f + 0.08f * sinf(s_time * 38.0f);
            gdl = lnEmitBladePass(beg, emitter, by, bladeMm + 25.0f,
                                  rMm * pulse, rMm * 0.56f * pulse, glow, glow & 0xFFFFFF90u);
            if (doubleEnded) {
                gdl = lnEmitBladePass(gdl, backEmitter, negBy, bladeMm * 0.92f + 25.0f,
                                      rMm * pulse, rMm * 0.56f * pulse, glow, glow & 0xFFFFFF90u);
            }
        }
    }
    return gdl;
}

static Gfx *lnDrawLaserswordView(Gfx *gdl, const float hiltView[3], const float dirView[3], const float refRight[3],
                                 float lenCm, float extend01, int colorIdx, float scale)
{
    return lnDrawLaserswordExView(gdl, hiltView, dirView, refRight, lenCm, extend01, colorIdx, scale, 0);
}

static Gfx *lnDrawKatanaView(Gfx *gdl, const float hiltView[3], const float dirView[3], const float refRight[3],
                             float lenCm, int isGold, float scale)
{
    const GevrLnPack *pk = gevrLnPackGet();
    if (!pk) return gdl;
    const GevrLnMesh *km = &pk->meshes[isGold ? GEVR_LN_MESH_KATANA_GOLD : GEVR_LN_MESH_KATANA_SILVER];
    float bx[3], by[3], bz[3];
    lnBuildBasis(dirView, refRight, bx, by, bz);
    Gfx *beg = lnBeginGeom(gdl, hiltView, scale, 0);
    if (!beg) return gdl;
    return lnDrawPackMesh(beg, km, bx, by, bz, lenCm, 0);
}

static Gfx *lnDrawShieldView(Gfx *gdl, const float centerView[3], const float rightView[3],
                             const float upView[3], float scale)
{
    const GevrLnPack *pk = gevrLnPackGet();
    if (!pk) return gdl;
    float bx[3], by[3], bz[3];
    lnNorm3(rightView, bx);
    lnNorm3(upView, by);
    lnCross3(bx, by, bz);
    lnNorm3(bz, bz);
    Gfx *beg = lnBeginGeom(gdl, centerView, scale, 0);
    if (!beg) return gdl;
    return lnDrawPackMesh(beg, &pk->meshes[GEVR_LN_MESH_SHIELD], bx, by, bz, 0.0f, 0);
}

static Gfx *lnDrawBoomerangView(Gfx *gdl, const float centerView[3], float spinAngle,
                                int variant, float scale)
{
    const GevrLnPack *pk = gevrLnPackGet();
    if (!pk) return gdl;
    const int isGold = (variant == GEVR_LN_BOOM_GOLD_SHURIKEN || variant == GEVR_LN_BOOM_GOLD_WING);
    const int isWing = (variant == GEVR_LN_BOOM_WING || variant == GEVR_LN_BOOM_GOLD_WING);
    const float ca = cosf(spinAngle), sa = sinf(spinAngle);
    const float bx[3] = { ca, 0.0f, sa };
    const float by[3] = { 0.0f, 1.0f, 0.0f };
    const float bz[3] = { -sa, 0.0f, ca };
    Gfx *beg = lnBeginGeom(gdl, centerView, scale, 0);
    if (!beg) return gdl;
    return lnDrawPackMesh(beg, &pk->meshes[isWing ? GEVR_LN_MESH_WING : GEVR_LN_MESH_SHURIKEN], bx, by, bz, 0.0f, isGold);
}

static Gfx *lnDrawSparkView(Gfx *gdl, const float centerView[3], float radiusCm, unsigned int rgba, float scale)
{
    Gfx *beg = lnBeginGeom(gdl, centerView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(11);
    if (!v) return gdl;
    lnSetVtx(&v[0], 0, 0, 0, 0xFFFFFFFFu);
    for (int i = 0; i < 10; i++) {
        const float a = (float)i * (6.2831853f / 10.0f) + s_time * 8.0f;
        const float r = (i & 1) ? radiusCm * 10.0f : radiusCm * 4.2f;
        lnSetVtx(&v[1 + i], cosf(a) * r, sinf(a) * r, 0.0f, rgba);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 11, 0);
    for (int i = 0; i < 10; i++) {
        gSP1Triangle(gdl++, 0, 1 + i, 1 + ((i + 1) % 10), 0);
    }
    return gdl;
}

static Gfx *lnDrawLightningView(Gfx *gdl, const float aView[3], const float bView[3],
                                unsigned int outerRgba, float scale, int seed)
{
    Gfx *beg = lnBeginGeom(gdl, aView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    const int segs = 7;
    Vtx *v = dynAllocateVertices((segs + 1) * 2);
    if (!v) return gdl;
    const float d[3] = {
        (bView[0] - aView[0]) * 10.0f / scale,
        (bView[1] - aView[1]) * 10.0f / scale,
        (bView[2] - aView[2]) * 10.0f / scale
    };
    for (int i = 0; i <= segs; i++) {
        const float u = (float)i / (float)segs;
        const float jitter = (i == 0 || i == segs) ? 0.0f : 55.0f * sinf(s_time * 48.0f + (float)(i * 19 + seed));
        const float jv = (i == 0 || i == segs) ? 0.0f : 35.0f * cosf(s_time * 53.0f + (float)(i * 13 + seed));
        const float px = d[0] * u + jitter, py = d[1] * u + jv, pz = d[2] * u;
        lnSetVtx(&v[i * 2], px + 9.0f, py + 9.0f, pz, outerRgba);
        lnSetVtx(&v[i * 2 + 1], px - 9.0f, py - 9.0f, pz, 0xFFFFFFFFu);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), (segs + 1) * 2, 0);
    for (int i = 0; i < segs; i++) {
        int o = i * 2;
        gSP2Triangles(gdl++, o, o + 1, o + 2, 0, o + 1, o + 3, o + 2, 0);
    }
    return gdl;
}

static void lnWorldToView(const Mtxf *w2s, const float w[3], float s, float out[3])
{
    coord3d c;
    c.x = w[0]; c.y = w[1]; c.z = w[2];
    mtx4TransformVecInPlace((Mtxf *)w2s, &c);
    out[0] = c.x * s; out[1] = c.y * s; out[2] = c.z * s;
}

/* Read-only per-eye transform of world-space motion ribbons (never mutates s_trails[]) */
static Gfx *lnDrawTrailWorld(Gfx *gdl, const Mtxf *w2s, const LnBladeTrail *tr, unsigned int rgba, float scale)
{
    if (!tr || tr->count < 2 || !w2s) return gdl;
    float origV[3];
    lnWorldToView(w2s, tr->hilt[0], scale, origV);
    Gfx *beg = lnBeginGeom(gdl, origV, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(tr->count * 2);
    if (!v) return gdl;
    const float invS = 10.0f / scale;
    const unsigned int rgb = rgba & 0xFFFFFF00u;
    const unsigned int maxA = rgba & 0xFFu;
    for (int i = 0; i < tr->count; i++) {
        float hv[3], tv[3];
        lnWorldToView(w2s, tr->hilt[i], scale, hv);
        lnWorldToView(w2s, tr->tip[i], scale, tv);
        const float fade = 1.0f - (float)i / (float)tr->count;
        const unsigned int aTip = (unsigned int)((float)maxA * fade * 0.72f);
        const unsigned int aBase = (unsigned int)((float)maxA * fade * 0.16f);
        lnSetVtx(&v[i * 2 + 0],
                 (hv[0] - origV[0]) * invS,
                 (hv[1] - origV[1]) * invS,
                 (hv[2] - origV[2]) * invS, rgb | aBase);
        lnSetVtx(&v[i * 2 + 1],
                 (tv[0] - origV[0]) * invS,
                 (tv[1] - origV[1]) * invS,
                 (tv[2] - origV[2]) * invS, rgb | aTip);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), tr->count * 2, 0);
    for (int i = 0; i < tr->count - 1; i++) {
        const int o = i * 2;
        gSP2Triangles(gdl++, o, o + 1, o + 3, 0, o, o + 3, o + 2, 0);
    }
    return gdl;
}

static Gfx *lnDrawSmokeView(Gfx *gdl, const float centerView[3], float radiusCm, float alpha01, float scale)
{
    if (alpha01 <= 0.02f) return gdl;
    Gfx *beg = lnBeginGeom(gdl, centerView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(9);
    if (!v) return gdl;
    const unsigned int a0 = (unsigned int)(alpha01 * 175.0f);
    const unsigned int a1 = (unsigned int)(alpha01 * 38.0f);
    lnSetVtx(&v[0], 0.0f, 0.0f, 0.0f, 0x38445400u | a0);
    const float r = radiusCm * 10.0f;
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * (6.2831853f / 8.0f) + s_time * 1.4f;
        lnSetVtx(&v[1 + i], cosf(a) * r, sinf(a) * r, 0.0f, 0x60708400u | a1);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 9, 0);
    for (int i = 0; i < 8; i++) {
        gSP1Triangle(gdl++, 0, 1 + i, 1 + ((i + 1) & 7), 0);
    }
    return gdl;
}

/* -------------------------------------------------------------------------- */
/* World Render (Thrown Weapons, Rival/Ninja/Ally Blades, VFX)                 */
/* -------------------------------------------------------------------------- */

Gfx *gevrLnDrawWorld(Gfx *gdl)
{
    if (!gevrIsQuestVrApk() || !g_gevrStereo || !gevrLnPackGet() || !g_CurrentPlayer || !lnInGameplay()) return gdl;
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    if (!w2s) return gdl;
    const float s = lnViewScale();
    Gfx *const startGdl = gdl;

    /* Player's thrown Lasersword or Boomerang */
    if (s_thrown.active) {
        float pv[3];
        lnWorldToView(w2s, s_thrown.pos, s, pv);
        if (pv[2] < -10.0f) {
            if (!s_thrown.isBoomerang) {
                const float spin[3] = { cosf(s_thrown.spinAngle), 0.0f, sinf(s_thrown.spinAngle) };
                gdl = lnDrawLaserswordExView(gdl, pv, spin, NULL, 105.0f, 1.0f, VrLaserswordColor, s,
                                             VrLaserswordStyle == GEVR_LN_STYLE_DOUBLE);
            } else {
                gdl = lnDrawBoomerangView(gdl, pv, s_thrown.spinAngle, VrNinjaBoomerang, s);
            }
        }
    }

    /* Special NPCs (Rivals / Evil Ninjas / Lasersword Ally) & their thrown boomerangs */
    for (int i = 0; i < LN_MAX_SPECIAL; i++) {
        LnSpecialNpc *sp = &s_specials[i];
        if (!sp->used) continue;
        if (sp->boomerang.active) {
            float pv[3];
            lnWorldToView(w2s, sp->boomerang.pos, s, pv);
            if (pv[2] < -10.0f) {
                gdl = lnDrawBoomerangView(gdl, pv, sp->boomerang.spinAngle, GEVR_LN_BOOM_SHURIKEN, s);
            }
        }
        if (!lnChrAliveValid(sp->chr, sp->chrnum)) continue;
        ChrRecord *c = sp->chr;

        float hiltW[3], dirW[3];
        if (sp->handValid && (g_GlobalTimer - sp->handTick) <= 8) {
            memcpy(hiltW, sp->handPosW, sizeof(hiltW));
            memcpy(dirW, sp->handDirW, sizeof(dirW));
        } else {
            const float yaw = c->model ? getsubroty(c->model) : 0.0f;
            const float cy = cosf(yaw), sy = sinf(yaw);
            hiltW[0] = c->prop->pos.x + cy * 18.0f * sp->scale + sy * 16.0f * sp->scale;
            hiltW[1] = c->prop->pos.y + 106.0f * sp->scale;
            hiltW[2] = c->prop->pos.z - sy * 18.0f * sp->scale + cy * 16.0f * sp->scale;
            dirW[0] = sy * 0.60f;
            dirW[1] = 0.72f;
            dirW[2] = cy * 0.60f;
            lnNorm3(dirW, dirW);
        }
        if (sp->swingTimer > 0.0f) {
            const float swingDrop = sinf((sp->swingTimer / 0.28f) * 3.14159f) * 0.65f;
            dirW[1] -= swingDrop;
            lnNorm3(dirW, dirW);
        }

        const float tipWorld[3] = {
            hiltW[0] + dirW[0] * 100.0f * sp->scale,
            hiltW[1] + dirW[1] * 100.0f * sp->scale,
            hiltW[2] + dirW[2] * 100.0f * sp->scale
        };
        float hv[3], tv[3];
        lnWorldToView(w2s, hiltW, s, hv);
        if (hv[2] >= -10.0f) continue;
        lnWorldToView(w2s, tipWorld, s, tv);
        float d[3] = { tv[0] - hv[0], tv[1] - hv[1], tv[2] - hv[2] };
        lnNorm3(d, d);
        if (sp->kind == LN_NPC_RIVAL || sp->kind == LN_NPC_ALLY_LASERSWORD) {
            gdl = lnDrawLaserswordExView(gdl, hv, d, NULL, 110.0f * sp->scale, sp->ignite.extend, sp->color, s,
                                         sp->kind == LN_NPC_RIVAL && sp->doubleBladed);
        } else {
            gdl = lnDrawKatanaView(gdl, hv, d, NULL, 105.0f, 0, s);
        }
    }

    /* Ninja Shadow-Step smoke clouds */
    for (int i = 0; i < LN_MAX_SMOKE; i++) {
        if (!s_smoke[i].used) continue;
        float pv[3];
        lnWorldToView(w2s, s_smoke[i].pos, s, pv);
        if (pv[2] < -10.0f) {
            const float u = s_smoke[i].age / s_smoke[i].maxAge;
            gdl = lnDrawSmokeView(gdl, pv, s_smoke[i].radius * (0.6f + 0.9f * u), 1.0f - u, s);
        }
    }

    /* Clash / ricochet starbursts */
    for (int i = 0; i < LN_MAX_SPARKS; i++) {
        if (!s_sparks[i].used) continue;
        float pv[3];
        lnWorldToView(w2s, s_sparks[i].pos, s, pv);
        if (pv[2] < -10.0f) {
            const float k = 1.0f - s_sparks[i].age / s_sparks[i].maxAge;
            gdl = lnDrawSparkView(gdl, pv, s_sparks[i].radius * (0.4f + 0.8f * k), s_sparks[i].rgba, s);
        }
    }

    /* Deflected laser bolts */
    for (int i = 0; i < LN_MAX_TRACERS; i++) {
        if (!s_tracers[i].used) continue;
        float av[3], bv[3];
        lnWorldToView(w2s, s_tracers[i].from, s, av);
        lnWorldToView(w2s, s_tracers[i].to, s, bv);
        gdl = lnDrawLightningView(gdl, av, bv, s_tracers[i].rgba, s, i * 7);
    }

    /* Rival NPC Force Lightning */
    if (s_rivalLightningShow > 0.0f) {
        float av[3], bv[3];
        lnWorldToView(w2s, s_rivalLightningFrom, s, av);
        lnWorldToView(w2s, s_rivalLightningTo, s, bv);
        gdl = lnDrawLightningView(gdl, av, bv, 0xFF3838FFu, s, 42);
    }

    if (gdl != startGdl) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    return gdl;
}

/* -------------------------------------------------------------------------- */
/* First-Person Viewmodel Render (Lasersword, Swords, Shield, Force FX)       */
/* -------------------------------------------------------------------------- */

Gfx *gevrLnDrawFirstPerson(Gfx *gdl)
{
    if (!lnModeActive() || lnUsingGunsOverride() || !lnInGameplay()) return gdl;
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    const float s = lnViewScale();
    const int bCtrl = VrLaserswordHand ? 0 : 1;
    const int fCtrl = 1 - bCtrl;
    Gfx *const startGdl = gdl;

    float offHilt[3] = { 0.0f, 0.0f, 0.0f };
    float offDir[3] = { 0.0f, 1.0f, 0.0f };
    float offRight[3] = { 1.0f, 0.0f, 0.0f };
    float offUp[3] = { 0.0f, 1.0f, 0.0f };
    float ob[3] = { 0.0f, 0.0f, 1.0f };
    const int haveVrOff = gevrLnGetGripAxes(fCtrl, offHilt, offRight, offUp, ob);
    if (haveVrOff) {
        offDir[0] = -ob[0] * 0.35f + offUp[0] * 0.94f;
        offDir[1] = -ob[1] * 0.35f + offUp[1] * 0.94f;
        offDir[2] = -ob[2] * 0.35f + offUp[2] * 0.94f;
        lnNorm3(offDir, offDir);
    }

    /* Weapon hand pose (Quest VR tracked controllers only) */
    if (!s_thrown.active) {
        float hilt[3], tipDir[3], r[3], u[3], b[3];
        const int haveVr = gevrLnGetGripAxes(bCtrl, hilt, r, u, b);
        if (haveVr) {
            tipDir[0] = -b[0] * 0.35f + u[0] * 0.94f;
            tipDir[1] = -b[1] * 0.35f + u[1] * 0.94f;
            tipDir[2] = -b[2] * 0.35f + u[2] * 0.94f;
            lnNorm3(tipDir, tipDir);
        }

        if (VrLaserswordNinjaMode == GEVR_LN_LASERSWORD) {
            const int dbl = (VrLaserswordStyle == GEVR_LN_STYLE_DOUBLE);
            unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
            gevrLnLaserswordPalette(VrLaserswordColor, &core, &glow);
            if (haveVr) {
                gdl = lnDrawLaserswordExView(gdl, hilt, tipDir, r, 115.0f, s_fpIgnite.extend, VrLaserswordColor, s, dbl);
            }
            if (VrLaserswordStyle == GEVR_LN_STYLE_DUAL && haveVrOff) {
                gdl = lnDrawLaserswordView(gdl, offHilt, offDir, offRight, 105.0f, s_fpIgnite.extend, VrLaserswordColor, s);
            }
            if (VrLnBladeTrails && w2s) {
                gdl = lnDrawTrailWorld(gdl, w2s, &s_trails[0], glow, s);
                if (dbl) gdl = lnDrawTrailWorld(gdl, w2s, &s_trails[1], glow, s);
                if (VrLaserswordStyle == GEVR_LN_STYLE_DUAL) gdl = lnDrawTrailWorld(gdl, w2s, &s_trails[2], glow, s);
            }
        } else if (VrLaserswordNinjaMode == GEVR_LN_NINJA) {
            const int sw = lnEffectiveNinjaSword();
            const unsigned int nTrailRgba = gevrLnIsGoldSword(sw) ? 0xFFE058D8u : 0xD8ECFFD8u;
            if (haveVr) {
                if (gevrLnSelectedNinjaSlot == GEVR_LN_NINJA_SLOT_BOOMERANG) {
                    gdl = lnDrawBoomerangView(gdl, hilt, s_time * 3.0f, VrNinjaBoomerang, s);
                } else {
                    const float reachCm = gevrLnWeaponReachCm(GEVR_LN_NINJA, sw);
                    gdl = lnDrawKatanaView(gdl, hilt, tipDir, r, reachCm, gevrLnIsGoldSword(sw), s);
                }
            }
            /* Off-hand dual short sword or Ninja Shield */
            if (haveVrOff) {
                if (gevrLnIsDualSword(GEVR_LN_NINJA, sw)) {
                    gdl = lnDrawKatanaView(gdl, offHilt, offDir, offRight, 70.0f, gevrLnIsGoldSword(sw), s);
                } else if (lnHasNinjaShield()) {
                    gdl = lnDrawShieldView(gdl, offHilt, offRight, offUp, s);
                }
            }
            if (VrLnBladeTrails && w2s) {
                gdl = lnDrawTrailWorld(gdl, w2s, &s_trails[0], nTrailRgba, s);
                if (gevrLnIsDualSword(GEVR_LN_NINJA, sw)) gdl = lnDrawTrailWorld(gdl, w2s, &s_trails[2], nTrailRgba, s);
            }
        }
    }

    /* Off-hand Force Lightning arcs */
    if (haveVrOff && s_lightningActive && w2s) {
        for (int i = 0; i < s_lightningCount; i++) {
            if (!lnChrAliveValid(s_lightningChrs[i], s_lightningChrnums[i])) continue;
            const float tgtW[3] = {
                s_lightningChrs[i]->prop->pos.x,
                s_lightningChrs[i]->prop->pos.y + 115.0f,
                s_lightningChrs[i]->prop->pos.z
            };
            float tv[3];
            lnWorldToView(w2s, tgtW, s, tv);
            gdl = lnDrawLightningView(gdl, offHilt, tv, 0x50B8FFFFu, s, i * 11);
        }
    }

    /* Force Choke tether line */
    if (haveVrOff && s_chokeActive && w2s && lnChrAliveValid(s_chokeChr, s_chokeChrnum)) {
        const float tgtW[3] = {
            s_chokeChr->prop->pos.x,
            s_chokeChr->prop->pos.y + 125.0f,
            s_chokeChr->prop->pos.z
        };
        float tv[3];
        lnWorldToView(w2s, tgtW, s, tv);
        gdl = lnDrawLightningView(gdl, offHilt, tv, 0xFF5050C0u, s, 99);
    }

    if (gdl != startGdl) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    return gdl;
}
