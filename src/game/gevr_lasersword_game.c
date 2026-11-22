#include "gevr_lasersword_game.h"
#include "gevr_lasersword.h"
#include "gevr_ragdoll.h"
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

extern int VrLasersword, VrLaserswordColor, VrLaserswordNpcLasers, VrLaserswordNinjaMode;
extern s32 g_gevrStereo, g_StageNum, g_GlobalTimer;
extern f32 D_800364CC;
extern s32 g_PlayerIsInTank;

extern bool netIsActive(void);
extern int getPlayerCount(void);
extern s32 lvlGetControlsLockedFlag(void);
extern s32 gevrSpectating(void);
extern s32 gevrCoopLocalDowned(void);
extern s32 gevrAllyIs(ChrRecord *chr);
extern s32 gevrLsGetGripAxes(s32 ctrl, f32 pos[3], f32 right[3], f32 up[3], f32 back[3]);
extern s32 gevrLsGetHandPoseWorld(s32 ctrl, f32 posW[3], f32 rightW[3], f32 upW[3], f32 fwdW[3]);
extern s32 gevrLsGetHandVelocityWorld(s32 ctrl, f32 velCmS[3]);
extern int bondinvAddInvItem(ITEM_IDS item);
extern s32 getCurrentPlayerWeaponId(GUNHAND hand);
extern float get_analog_value(int hand_index, const char *input_name);
extern int vr_haptics_ready(void);
extern s32 trigger_haptic_vibration_c(int hand_index, float amplitude, float duration);
extern Gfx *combiner_bayer_lod_perspective(Gfx *gdl);

/* -------------------------------------------------------------------------- */
/* State                                                                      */
/* -------------------------------------------------------------------------- */

#define LS_MAX_CUTS      8
#define LS_MAX_SPARKS    12
#define LS_MAX_TRACERS   8
#define LS_TRAIL_SAMPLES 7
#define LS_VR_HIT_SLOTS  16
#define LS_PARRY_WINDOW  0.22f

typedef struct {
    int count;
    float hilt[LS_TRAIL_SAMPLES][3];
    float tip[LS_TRAIL_SAMPLES][3];
    float age[LS_TRAIL_SAMPLES];
} LsBladeTrail;

typedef struct {
    int used;
    float pos[3];
    float vel[3];
    int cutsLeft;
    float ttl;
} LsPendingCut;

typedef struct {
    int used;
    float pos[3];
    unsigned int rgba;
    float radius;
    float age;
    float maxAge;
} LsSpark;

typedef struct {
    int used;
    float from[3];
    float to[3];
    unsigned int rgba;
    float age;
    float maxAge;
} LsTracer;

static GevrLsTrigger s_rtTrig, s_ltTrig;
static GevrLsSlash s_slash;
static GevrLsIgnition s_ignite[2];
static GevrLsThrown s_thrown;
static int s_blocking;
static float s_blockHoldSec;
static int s_granted;
static s32 s_lastStage = -1;
static float s_time;
static float s_wallSparkCd[2];
static float s_vrWhooshCd[2];
static float s_vrThrowGuard[2];
static int s_vrTrigWas[2];

static ChrRecord *s_vrHitChr[2][LS_VR_HIT_SLOTS];
static float s_vrHitCd[2][LS_VR_HIT_SLOTS];
static ChrRecord *s_throwHitChr[12];
static float s_throwHitCd[12];
static int s_throwHitCount;

static LsBladeTrail s_trails[2];
static int s_vrPoseValid[2];
static float s_vrHiltW[2][3];
static float s_vrTipW[2][3];
static float s_vrPrevTipW[2][3];
static float s_vrDirW[2][3];
static float s_vrAimW[2][3];
static int s_vrBlocking[2];

static LsPendingCut s_cuts[LS_MAX_CUTS];
static LsSpark s_sparks[LS_MAX_SPARKS];
static LsTracer s_tracers[LS_MAX_TRACERS];

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static float lsViewScale(void)
{
    return (D_800364CC > 1e-6f) ? D_800364CC : 1.0f;
}

static float lsNorm3(const float v[3], float out[3])
{
    const float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l2 < 1e-10f) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f;
        return 0.0f;
    }
    const float inv = 1.0f / sqrtf(l2);
    out[0] = v[0] * inv; out[1] = v[1] * inv; out[2] = v[2] * inv;
    return 1.0f / inv;
}

static void lsCross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static float lsDot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void lsHaptic(int ctrl, float amp, float dur)
{
    if (g_gevrStereo && ctrl >= 0 && ctrl <= 1 && vr_haptics_ready()) {
        trigger_haptic_vibration_c(ctrl, amp, dur);
    }
}

static int lsModeEnabled(void)
{
    return VrLasersword != 0 && VrLaserswordNinjaMode == 0;
}

static int lsInGameplay(void)
{
    if (!g_CurrentPlayer || g_CurrentPlayer->bonddead || g_PlayerIsInTank == 1) return 0;
    if (g_StageNum == LEVELID_TITLE || g_CurrentPlayer->pause_state != 0) return 0;
    if (g_CurrentPlayer->watch_animation_state != 0 || lvlGetControlsLockedFlag()) return 0;
    if (gevrSpectating() || gevrCoopLocalDowned()) return 0;
    if (netIsActive() && getPlayerCount() > 1) return 0;
    return 1;
}

static int lsHandHoldsKnife(s32 hand)
{
    if (!lsModeEnabled() || !lsInGameplay() || hand < 0 || hand > 1) return 0;
    return getCurrentPlayerWeaponId(hand) == ITEM_KNIFE;
}

static int lsChrEnemy(ChrRecord *c)
{
    if (!c || !c->prop || !c->model || !c->prop->stan) return 0;
    if (c->chrnum < 0 || c->prop->type != PROP_TYPE_CHR) return 0;
    if (chrIsDead(c) || (c->hidden & CHRHIDDEN_REMOVE) || (c->chrflags & CHRFLAG_HIDDEN)) return 0;
    if (gevrAllyIs(c) || (c->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN)) return 0;
    if (c->actiontype == ACT_SURRENDER) return 0;
    return 1;
}

static int lsHasLos(const float from[3], StandTile *fromStan, const float tileOrigin[3], const float to[3])
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

static void lsAddSpark(const float pos[3], unsigned int rgba, float radius, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < LS_MAX_SPARKS; i++) {
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

static void lsAddTracer(const float from[3], const float to[3], unsigned int rgba, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < LS_MAX_TRACERS; i++) {
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

static void lsQueueCut(const float pos[3], const float vel[3], int maxCuts)
{
    if (gevrRagdollForceCutNear(pos, 95.0f, vel, maxCuts) > 0) {
        return;
    }
    for (int i = 0; i < LS_MAX_CUTS; i++) {
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

static void lsTrailClearHand(s32 hand)
{
    if (hand >= 0 && hand < 2) {
        memset(&s_trails[hand], 0, sizeof(s_trails[hand]));
    }
}

static void lsTrailStepAge(float dt)
{
    for (int h = 0; h < 2; h++) {
        LsBladeTrail *tr = &s_trails[h];
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

static void lsTrailPushWorld(s32 hand, const float hiltW[3], const float tipW[3])
{
    if (hand < 0 || hand > 1) return;
    LsBladeTrail *tr = &s_trails[hand];
    if (tr->count > 0) {
        const float dx = tipW[0] - tr->tip[0][0];
        const float dy = tipW[1] - tr->tip[0][1];
        const float dz = tipW[2] - tr->tip[0][2];
        if (dx * dx + dy * dy + dz * dz < 4.0f * 4.0f) return;
    }
    const int n = tr->count < LS_TRAIL_SAMPLES ? tr->count : LS_TRAIL_SAMPLES - 1;
    for (int i = n; i > 0; i--) {
        memcpy(tr->hilt[i], tr->hilt[i - 1], sizeof(tr->hilt[0]));
        memcpy(tr->tip[i], tr->tip[i - 1], sizeof(tr->tip[0]));
        tr->age[i] = tr->age[i - 1];
    }
    memcpy(tr->hilt[0], hiltW, sizeof(tr->hilt[0]));
    memcpy(tr->tip[0], tipW, sizeof(tr->tip[0]));
    tr->age[0] = 0.0f;
    if (tr->count < LS_TRAIL_SAMPLES) tr->count++;
}

static void lsGetPlayerCameraWorld(float eye[3], float right[3], float up[3], float fwd[3])
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
    lsNorm3(right, right);
    lsNorm3(up, up);
    lsNorm3(fwd, fwd);
}

static void lsGetPlayerTileOrigin(float outOrigin[3], StandTile **outStan)
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

static void lsHitGuard(ChrRecord *c, const float hitPt[3], const float dir[3], float dmgAmount, int forceCutCount)
{
    if (!lsChrEnemy(c)) return;
    coord3d d;
    d.x = dir[0]; d.y = dir[1]; d.z = dir[2];
    const f32 targetDmg = c->damage + dmgAmount;

    if (targetDmg < c->maxdamage) {
        const f32 savedMax = c->maxdamage;
        c->maxdamage = targetDmg + 1000.0f;
        handles_shot_actors(c, HIT_CHEST, &d, ITEM_KNIFE, FALSE);
        c->maxdamage = savedMax;
        if (!chrIsDead(c)) {
            c->damage = targetDmg;
        }
    } else {
        c->damage = c->maxdamage;
        handles_shot_actors(c, HIT_CHEST, &d, ITEM_KNIFE, TRUE);
    }

    unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
    gevrLsPalette(VrLaserswordColor, &core, &glow);
    const float fallbackPos[3] = { c->prop->pos.x, c->prop->pos.y + 130.0f, c->prop->pos.z };
    const float *cutPt = hitPt ? hitPt : fallbackPos;
    lsAddSpark(cutPt, glow, 22.0f, 0.22f);
    if (forceCutCount > 0 && chrIsDead(c)) {
        const float cutVel[3] = { dir[0] * 420.0f, dir[1] * 260.0f + 160.0f, dir[2] * 420.0f };
        lsQueueCut(cutPt, cutVel, forceCutCount);
    }
}

static int lsVrCanHitChr(int hand, ChrRecord *c)
{
    for (int i = 0; i < LS_VR_HIT_SLOTS; i++) {
        if (s_vrHitChr[hand][i] == c && s_vrHitCd[hand][i] > 0.0f) return 0;
    }
    return 1;
}

static void lsVrMarkHitChr(int hand, ChrRecord *c, float cd)
{
    int freeIdx = 0;
    float minCd = 1e9f;
    for (int i = 0; i < LS_VR_HIT_SLOTS; i++) {
        if (s_vrHitChr[hand][i] == c) {
            s_vrHitCd[hand][i] = cd;
            return;
        }
        if (s_vrHitCd[hand][i] <= 0.0f) {
            freeIdx = i;
            break;
        }
        if (s_vrHitCd[hand][i] < minCd) {
            minCd = s_vrHitCd[hand][i];
            freeIdx = i;
        }
    }
    s_vrHitChr[hand][freeIdx] = c;
    s_vrHitCd[hand][freeIdx] = cd;
}

static ChrRecord *lsFindShooter(const float eye[3], const float dir[3], float maxDistCm, float minDot)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    lsGetPlayerTileOrigin(pOrigin, &pStan);

    ChrRecord *best = NULL;
    float bestScore = 1e30f;
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!lsChrEnemy(c)) continue;
        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
        const float dx = tgt[0] - eye[0];
        const float dy = tgt[1] - eye[1];
        const float dz = tgt[2] - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < 100.0f || d2 > maxDistCm * maxDistCm) continue;
        const float d = sqrtf(d2);
        const float dot = (dx * dir[0] + dy * dir[1] + dz * dir[2]) / d;
        if (dot >= minDot && lsHasLos(eye, pStan, pOrigin, tgt)) {
            const float score = d * (1.4f - dot);
            if (score < bestScore) {
                bestScore = score;
                best = c;
            }
        }
    }
    return best;
}

/* -------------------------------------------------------------------------- */
/* Lifecycle & Queries                                                        */
/* -------------------------------------------------------------------------- */

void gevrLsGameReset(void)
{
    gevrLsTriggerReset(&s_rtTrig);
    gevrLsTriggerReset(&s_ltTrig);
    memset(&s_slash, 0, sizeof(s_slash));
    memset(s_ignite, 0, sizeof(s_ignite));
    memset(&s_thrown, 0, sizeof(s_thrown));
    s_blocking = 0;
    s_blockHoldSec = 0.0f;
    s_granted = 0;
    s_wallSparkCd[0] = s_wallSparkCd[1] = 0.0f;
    s_vrWhooshCd[0] = s_vrWhooshCd[1] = 0.0f;
    s_vrThrowGuard[0] = s_vrThrowGuard[1] = 0.0f;
    s_vrTrigWas[0] = s_vrTrigWas[1] = 0;
    memset(s_vrHitChr, 0, sizeof(s_vrHitChr));
    memset(s_vrHitCd, 0, sizeof(s_vrHitCd));
    memset(s_throwHitChr, 0, sizeof(s_throwHitChr));
    memset(s_throwHitCd, 0, sizeof(s_throwHitCd));
    s_throwHitCount = 0;
    memset(s_trails, 0, sizeof(s_trails));
    memset(s_vrPoseValid, 0, sizeof(s_vrPoseValid));
    memset(s_vrBlocking, 0, sizeof(s_vrBlocking));
    memset(s_cuts, 0, sizeof(s_cuts));
    memset(s_sparks, 0, sizeof(s_sparks));
    memset(s_tracers, 0, sizeof(s_tracers));
    gevrLsAudioReset();
    s_lastStage = g_StageNum;
}

s32 gevrLsHandActive(s32 hand)
{
    return lsHandHoldsKnife(hand);
}

s32 gevrLsNpcLasersActive(void)
{
    return (lsModeEnabled() && VrLaserswordNpcLasers != 0 && lsInGameplay()) ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Gameplay Tick                                                              */
/* -------------------------------------------------------------------------- */

void gevrLsGameTick(s32 ticks60)
{
    if (g_StageNum != s_lastStage) {
        gevrLsGameReset();
    }
    const float dt = ticks60 > 0 ? fminf((float)ticks60 / 60.0f, 0.05f) : (1.0f / 60.0f);
    s_time += dt;

    /* Pending cuts after ragdoll capture */
    for (int i = 0; i < LS_MAX_CUTS; i++) {
        if (!s_cuts[i].used) continue;
        if (gevrRagdollForceCutNear(s_cuts[i].pos, 95.0f, s_cuts[i].vel, s_cuts[i].cutsLeft) > 0) {
            s_cuts[i].used = 0;
        } else {
            s_cuts[i].ttl -= dt;
            if (s_cuts[i].ttl <= 0.0f) s_cuts[i].used = 0;
        }
    }

    for (int i = 0; i < LS_MAX_SPARKS; i++) {
        if (s_sparks[i].used) {
            s_sparks[i].age += dt;
            if (s_sparks[i].age >= s_sparks[i].maxAge) s_sparks[i].used = 0;
        }
    }
    for (int i = 0; i < LS_MAX_TRACERS; i++) {
        if (s_tracers[i].used) {
            s_tracers[i].age += dt;
            if (s_tracers[i].age >= s_tracers[i].maxAge) s_tracers[i].used = 0;
        }
    }
    lsTrailStepAge(dt);

    if (!lsModeEnabled() || !lsInGameplay()) {
        gevrLsAudioSetState(0, 0.0f, 0.0f, 0);
        return;
    }

    if (!s_granted) {
        bondinvAddInvItem(ITEM_KNIFE);
        s_granted = 1;
    }

    const int holdR = lsHandHoldsKnife(GUNRIGHT);
    const int holdL = lsHandHoldsKnife(GUNLEFT);
    if (!holdR && !holdL && !s_thrown.active) {
        for (int h = 0; h < 2; h++) {
            gevrLsIgnitionUpdate(&s_ignite[h], 0, dt);
            lsTrailClearHand(h);
            s_vrPoseValid[h] = 0;
            s_vrBlocking[h] = 0;
        }
        s_blocking = 0;
        s_blockHoldSec = 0.0f;
        gevrLsAudioSetState(0, 0.0f, 0.0f, 0);
        return;
    }

    for (int h = 0; h < 2; h++) {
        if (s_wallSparkCd[h] > 0.0f) s_wallSparkCd[h] -= dt;
        if (s_vrWhooshCd[h] > 0.0f) s_vrWhooshCd[h] -= dt;
        if (s_vrThrowGuard[h] > 0.0f) s_vrThrowGuard[h] -= dt;
        for (int k = 0; k < LS_VR_HIT_SLOTS; k++) {
            if (s_vrHitCd[h][k] > 0.0f) s_vrHitCd[h][k] -= dt;
        }
        const int drawn = lsHandHoldsKnife(h) && !(s_thrown.active && s_thrown.hand == h);
        const float prevExt = s_ignite[h].extend;
        const float ext = gevrLsIgnitionUpdate(&s_ignite[h], drawn, dt);
        if (drawn && prevExt <= 0.02f && ext > 0.02f) {
            gevrLsAudioTrigger(GEVR_LS_SFX_IGNITE, 1.0f);
        }
        if (!drawn) {
            lsTrailClearHand(h);
            s_vrPoseValid[h] = 0;
            s_vrBlocking[h] = 0;
        }
    }

    float eye[3], right[3], up[3], fwd[3];
    lsGetPlayerCameraWorld(eye, right, up, fwd);
    float pOrigin[3];
    StandTile *pStan = NULL;
    lsGetPlayerTileOrigin(pOrigin, &pStan);

    float maxSwing01 = 0.0f;
    float maxExtend = 0.0f;

    if (g_gevrStereo) {
        s_blocking = 0;
        for (int h = 0; h < 2; h++) {
            const int ctrl = (h == GUNRIGHT) ? 1 : 0;
            const int drawn = lsHandHoldsKnife(h) && !(s_thrown.active && s_thrown.hand == h);
            if (!drawn) continue;
            if (s_ignite[h].extend > maxExtend) maxExtend = s_ignite[h].extend;

            const int trigNow = get_analog_value(ctrl, "trigger") > 0.5f;
            const int trigPressed = trigNow && !s_vrTrigWas[ctrl];
            const int trigReleased = !trigNow && s_vrTrigWas[ctrl];
            s_vrTrigWas[ctrl] = trigNow;
            if (trigPressed) s_vrThrowGuard[ctrl] = 0.35f;
            s_vrBlocking[h] = trigNow;
            if (trigNow) s_blocking = 1;

            float hr[3], hu[3], hf[3], hPos[3], velCm[3] = { 0.0f, 0.0f, 0.0f };
            gevrLsGetHandVelocityWorld(ctrl, velCm);
            if (!gevrLsGetHandPoseWorld(ctrl, hPos, hr, hu, hf)) continue;

            float prevTip[3];
            const int hasPrev = s_vrPoseValid[h];
            if (hasPrev) memcpy(prevTip, s_vrTipW[h], sizeof(prevTip));

            float dirW[3] = {
                hf[0] * 0.35f + hu[0] * 0.94f,
                hf[1] * 0.35f + hu[1] * 0.94f,
                hf[2] * 0.35f + hu[2] * 0.94f
            };
            lsNorm3(dirW, dirW);
            const float activeLen = 91.0f * s_ignite[h].extend + 24.0f;
            const float tipW[3] = {
                hPos[0] + dirW[0] * activeLen,
                hPos[1] + dirW[1] * activeLen,
                hPos[2] + dirW[2] * activeLen
            };
            memcpy(s_vrHiltW[h], hPos, sizeof(hPos));
            memcpy(s_vrTipW[h], tipW, sizeof(tipW));
            memcpy(s_vrDirW[h], dirW, sizeof(dirW));
            memcpy(s_vrAimW[h], hf, sizeof(hf));
            s_vrPoseValid[h] = 1;
            lsTrailPushWorld(h, hPos, tipW);

            /* VR throw gesture */
            if (!s_thrown.active && trigReleased && s_vrThrowGuard[ctrl] <= 0.0f
                && gevrLsVrWristThrow(velCm, hf, 1)) {
                gevrLsThrownLaunch(&s_thrown, hPos, hf);
                s_thrown.hand = h;
                s_throwHitCount = 0;
                lsTrailClearHand(h);
                gevrLsAudioTrigger(GEVR_LS_SFX_SLASH, 1.2f);
                lsHaptic(ctrl, 0.7f, 0.08f);
                continue;
            }

            const float spd = sqrtf(velCm[0] * velCm[0] + velCm[1] * velCm[1] + velCm[2] * velCm[2]);
            const float sw01 = fminf(1.0f, spd / 240.0f);
            if (sw01 > maxSwing01) maxSwing01 = sw01;

            if (spd >= 115.0f && s_ignite[h].extend > 0.35f) {
                float velDir[3];
                lsNorm3(velCm, velDir);
                if (s_vrWhooshCd[h] <= 0.0f) {
                    gevrLsAudioTrigger(GEVR_LS_SFX_SLASH, 0.75f + 0.45f * sw01);
                    s_vrWhooshCd[h] = 0.22f;
                }
                const s32 n = get_numguards();
                for (s32 i = 0; i < n; i++) {
                    ChrRecord *c = &g_ChrSlots[i];
                    if (!lsChrEnemy(c) || !lsVrCanHitChr(h, c)) continue;
                    const float r = (c->chrwidth > 12.0f ? c->chrwidth : 20.0f) + 18.0f;
                    float hitPt[3];
                    int hit = gevrLsSegmentHitCylinder(hPos, tipW, c->prop->pos.f, r, 185.0f, hitPt);
                    if (!hit && hasPrev) {
                        hit = gevrLsSegmentHitCylinder(prevTip, tipW, c->prop->pos.f, r, 185.0f, hitPt);
                    }
                    if (!hit) continue;
                    const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
                    if (!lsHasLos(hPos, pStan, pOrigin, tgt)) continue;

                    lsVrMarkHitChr(h, c, 0.24f);
                    lsHitGuard(c, hitPt, velDir, 6.0f + 8.0f * sw01, 2);
                    lsHaptic(ctrl, 0.85f, 0.08f);
                }
                /* Also slice fallen ragdolls at the blade's midpoint/tip (head/limb chops) */
                const float midPt[3] = {
                    (hPos[0] + tipW[0]) * 0.5f,
                    (hPos[1] + tipW[1]) * 0.5f,
                    (hPos[2] + tipW[2]) * 0.5f
                };
                if (gevrRagdollForceCutNear(midPt, 52.0f, velCm, 1) > 0
                    || gevrRagdollForceCutNear(tipW, 42.0f, velCm, 1) > 0) {
                    lsHaptic(ctrl, 0.65f, 0.05f);
                }
            }

            if (pdphysIsReady() && s_ignite[h].extend > 0.5f && s_wallSparkCd[h] <= 0.0f) {
                float wallPos[3];
                if (pdphysRaycastAnyRoom(hPos, tipW, wallPos, NULL, NULL)) {
                    lsAddSpark(wallPos, 0xFF9838FFu, 15.0f, 0.14f);
                    lsHaptic(ctrl, 0.25f, 0.02f);
                    s_wallSparkCd[h] = 0.055f;
                }
            }
        }
    } else {
        /* Flat / Pad controls (primarily GUNRIGHT when holding ITEM_KNIFE) */
        const int h = lsHandHoldsKnife(GUNRIGHT) ? GUNRIGHT : GUNLEFT;
        const int inHand = !(s_thrown.active && s_thrown.hand == h);
        maxExtend = s_ignite[h].extend;

        int rtDown = 0, ltDown = 0, isMouse = 0;
        gevrLsInputReadFlat(&rtDown, &ltDown, &isMouse);
        (void)isMouse;
        const int rtEvt = gevrLsTriggerUpdate(&s_rtTrig, rtDown && inHand, dt);
        const int ltEvt = gevrLsTriggerUpdate(&s_ltTrig, ltDown && inHand, dt);
        s_blocking = (ltDown && inHand) ? 1 : 0;

        if (rtEvt == GEVR_LS_TRIG_TAP && inHand) {
            if (gevrLsSlashStart(&s_slash, s_blocking)) {
                gevrLsAudioTrigger(GEVR_LS_SFX_SLASH, 1.0f);
                const float reach2 = (GEVR_LS_REACH_CM + 55.0f) * (GEVR_LS_REACH_CM + 55.0f);
                const s32 n = get_numguards();
                for (s32 i = 0; i < n; i++) {
                    ChrRecord *c = &g_ChrSlots[i];
                    if (!lsChrEnemy(c)) continue;
                    const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 125.0f, c->prop->pos.z };
                    const float dx = tgt[0] - eye[0], dy = tgt[1] - eye[1], dz = tgt[2] - eye[2];
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 > reach2 || d2 < 1.0f) continue;
                    const float d = sqrtf(d2);
                    const float to[3] = { dx / d, dy / d, dz / d };
                    if (lsDot3(fwd, to) < 0.35f) continue;
                    if (!lsHasLos(eye, pStan, pOrigin, tgt)) continue;
                    /* Aimed strike height (clamped inside body: high look aims at head/neck for decapitation!) */
                    float aimY = eye[1] + fwd[1] * fminf(d, GEVR_LS_REACH_CM) + 8.0f;
                    const float yLo = c->prop->pos.y + 45.0f;
                    const float yHi = c->prop->pos.y + 162.0f;
                    if (aimY < yLo) aimY = yLo;
                    if (aimY > yHi) aimY = yHi;
                    const float hitPt[3] = { c->prop->pos.x, aimY, c->prop->pos.z };
                    lsHitGuard(c, hitPt, to, 14.0f, 2);
                }
            }
        }

        const float sp = gevrLsSlashStep(&s_slash, dt);
        if (sp >= 0.0f) maxSwing01 = sinf(sp * 3.14159f);

        if (inHand) {
            const float sideSign = (h == GUNRIGHT) ? 1.0f : -1.0f;
            const float len = 91.0f * s_ignite[h].extend + 24.0f;
            float hw[3], tw[3], wDir[3];
            if (s_slash.active) {
                const float swing = (s_slash.dir == 0 ? 1.0f - 2.0f * sp : -1.0f + 2.0f * sp) * sideSign;
                for (int k = 0; k < 3; k++) {
                    hw[k] = eye[k] + right[k] * (14.0f * swing) - up[k] * 10.0f + fwd[k] * 34.0f;
                    wDir[k] = right[k] * (-0.55f * swing) + up[k] * (0.35f - 0.5f * sp) + fwd[k] * 0.75f;
                }
            } else {
                for (int k = 0; k < 3; k++) {
                    hw[k] = eye[k] + right[k] * (17.0f * sideSign) - up[k] * 14.0f + fwd[k] * 32.0f;
                    wDir[k] = right[k] * (-0.22f * sideSign) + up[k] * 0.72f + fwd[k] * 0.66f;
                }
            }
            lsNorm3(wDir, wDir);
            for (int k = 0; k < 3; k++) tw[k] = hw[k] + wDir[k] * len;
            lsTrailPushWorld(h, hw, tw);
        }

        if (!s_thrown.active && (ltEvt == GEVR_LS_TRIG_DOUBLE_TAP || rtEvt == GEVR_LS_TRIG_HOLD_RELEASE) && inHand) {
            const float throwOrigin[3] = {
                eye[0] + fwd[0] * 36.0f,
                eye[1] - up[1] * 8.0f,
                eye[2] + fwd[2] * 36.0f
            };
            gevrLsThrownLaunch(&s_thrown, throwOrigin, fwd);
            s_thrown.hand = h;
            s_throwHitCount = 0;
            lsTrailClearHand(h);
            gevrLsAudioTrigger(GEVR_LS_SFX_SLASH, 1.2f);
        }
    }

    if (s_blocking) {
        s_blockHoldSec += dt;
    } else {
        s_blockHoldSec = 0.0f;
    }

    /* Spinning Thrown Lasersword update */
    if (s_thrown.active) {
        for (int i = 0; i < s_throwHitCount; i++) {
            if (s_throwHitCd[i] > 0.0f) s_throwHitCd[i] -= dt;
        }
        float catchPos[3];
        if (s_vrPoseValid[s_thrown.hand]) {
            memcpy(catchPos, s_vrHiltW[s_thrown.hand], sizeof(catchPos));
        } else {
            catchPos[0] = eye[0] + right[0] * 16.0f - up[0] * 10.0f + fwd[0] * 28.0f;
            catchPos[1] = eye[1] + right[1] * 16.0f - up[1] * 10.0f + fwd[1] * 28.0f;
            catchPos[2] = eye[2] + right[2] * 16.0f - up[2] * 10.0f + fwd[2] * 28.0f;
        }
        int hitWall = 0;
        if (!s_thrown.returning && pdphysIsReady()) {
            const float nxt[3] = {
                s_thrown.pos[0] + s_thrown.vel[0] * dt,
                s_thrown.pos[1] + s_thrown.vel[1] * dt,
                s_thrown.pos[2] + s_thrown.vel[2] * dt
            };
            float hitPos[3];
            if (pdphysRaycastAnyRoom(s_thrown.pos, nxt, hitPos, NULL, NULL)) {
                hitWall = 1;
                lsAddSpark(hitPos, 0xFF9838FFu, 22.0f, 0.20f);
                gevrLsAudioTrigger(GEVR_LS_SFX_CLASH, 0.9f);
            }
        }
        gevrLsThrownStep(&s_thrown, catchPos, hitWall, dt);

        const s32 n = get_numguards();
        for (s32 i = 0; i < n; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (!lsChrEnemy(c)) continue;
            const float dx = c->prop->pos.x - s_thrown.pos[0];
            const float dz = c->prop->pos.z - s_thrown.pos[2];
            const float dy = s_thrown.pos[1] - c->prop->pos.y;
            if (dx * dx + dz * dz > 85.0f * 85.0f || dy < -12.0f || dy > 195.0f) continue;

            int cdIdx = -1;
            for (int k = 0; k < s_throwHitCount; k++) {
                if (s_throwHitChr[k] == c) { cdIdx = k; break; }
            }
            if (cdIdx >= 0 && s_throwHitCd[cdIdx] > 0.0f) continue;
            if (cdIdx < 0 && s_throwHitCount < 12) {
                cdIdx = s_throwHitCount++;
                s_throwHitChr[cdIdx] = c;
            }
            if (cdIdx >= 0) s_throwHitCd[cdIdx] = 0.28f;

            float dDir[3];
            lsNorm3(s_thrown.vel, dDir);
            const float cutPt[3] = { c->prop->pos.x, s_thrown.pos[1], c->prop->pos.z };
            lsHitGuard(c, cutPt, dDir, 24.0f, 2);
        }
    }

    gevrLsAudioSetState(1, fmaxf(maxExtend, s_thrown.active ? 1.0f : 0.0f), maxSwing01, s_thrown.active);
}

/* -------------------------------------------------------------------------- */
/* Damage Interception (Bullet Deflection)                                    */
/* -------------------------------------------------------------------------- */

s32 gevrLsInterceptPlayerDamage(f32 damageAmount, f32 vecX, f32 vecZ)
{
    (void)damageAmount;
    if (!lsModeEnabled() || !lsInGameplay()) return 0;

    int blockHand = -1;
    for (int h = 0; h < 2; h++) {
        if (!lsHandHoldsKnife(h) || (s_thrown.active && s_thrown.hand == h)) continue;
        if (g_gevrStereo) {
            if (s_vrBlocking[h]) { blockHand = h; break; }
        } else if (s_blocking) {
            blockHand = h;
            break;
        }
    }
    if (blockHand < 0) return 0;

    float eye[3], right[3], up[3], fwd[3];
    lsGetPlayerCameraWorld(eye, right, up, fwd);

    const float travel[3] = { vecX, 0.0f, vecZ };
    float toShooter[3];
    if (!gevrLsBulletTravelToShooter(travel, toShooter)) {
        memcpy(toShooter, fwd, sizeof(toShooter));
    }

    float reflectDir[3] = { 0.0f, 0.0f, 0.0f };
    int res;
    if (g_gevrStereo && s_vrPoseValid[blockHand]) {
        res = gevrLsCheckVrBladeBlock(1, fwd, s_vrDirW[blockHand], s_vrAimW[blockHand], toShooter, reflectDir);
    } else {
        res = gevrLsCheckBlock(1, fwd, toShooter, reflectDir);
    }
    if (res != GEVR_LS_BLOCK_DEFLECT) return 0;

    const int perfect = (s_blockHoldSec <= LS_PARRY_WINDOW);
    float bladePt[3];
    if (g_gevrStereo && s_vrPoseValid[blockHand]) {
        for (int k = 0; k < 3; k++) bladePt[k] = s_vrHiltW[blockHand][k] + s_vrDirW[blockHand][k] * 55.0f;
    } else {
        const float sideSign = (blockHand == GUNRIGHT) ? 1.0f : -1.0f;
        for (int k = 0; k < 3; k++) bladePt[k] = eye[k] + fwd[k] * 48.0f + right[k] * (6.0f * sideSign) - up[k] * 6.0f;
    }

    unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
    gevrLsPalette(VrLaserswordColor, &core, &glow);
    lsAddSpark(bladePt, perfect ? 0xFFF480FFu : glow, perfect ? 38.0f : 28.0f, 0.22f);
    gevrLsAudioTrigger(GEVR_LS_SFX_DEFLECT, perfect ? 1.45f : 1.0f);
    lsHaptic(blockHand == GUNRIGHT ? 1 : 0, perfect ? 1.0f : 0.85f, perfect ? 0.10f : 0.07f);

    ChrRecord *shooter = lsFindShooter(eye, reflectDir, 2200.0f, 0.78f);
    if (!shooter) {
        shooter = lsFindShooter(eye, fwd, 2200.0f, 0.70f);
    }
    if (shooter) {
        const float tgt[3] = { shooter->prop->pos.x, shooter->prop->pos.y + 115.0f, shooter->prop->pos.z };
        const float d[3] = { tgt[0] - bladePt[0], tgt[1] - bladePt[1], tgt[2] - bladePt[2] };
        float nd[3];
        lsNorm3(d, nd);
        lsAddTracer(bladePt, tgt, VrLaserswordNpcLasers ? (perfect ? 0xFFF480FFu : 0x58E8FFFFu) : glow, 0.18f);
        lsHitGuard(shooter, tgt, nd, perfect ? 18.0f : 3.5f, 0);
    } else {
        const float farPt[3] = {
            bladePt[0] + reflectDir[0] * 900.0f,
            bladePt[1] + reflectDir[1] * 900.0f,
            bladePt[2] + reflectDir[2] * 900.0f
        };
        lsAddTracer(bladePt, farPt, VrLaserswordNpcLasers ? (perfect ? 0xFFF480FFu : 0x58E8FFFFu) : glow, 0.16f);
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Fast3D Rendering                                                           */
/* -------------------------------------------------------------------------- */

static void lsSetVtx(Vtx *v, f32 x, f32 y, f32 z, u32 rgba)
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

static u32 lsShadeRgba(u32 rgba, float mul)
{
    if (mul < 0.40f) mul = 0.40f;
    if (mul > 1.0f) mul = 1.0f;
    const unsigned int r = (unsigned int)((float)((rgba >> 24) & 0xff) * mul);
    const unsigned int g = (unsigned int)((float)((rgba >> 16) & 0xff) * mul);
    const unsigned int b = (unsigned int)((float)((rgba >> 8) & 0xff) * mul);
    return (r << 24) | (g << 16) | (b << 8) | (rgba & 0xffu);
}

static Gfx *lsBeginGeom(Gfx *gdl, const float originView[3], float scale, int translucent)
{
    Mtxf mf;
    Mtx *mv = dynAllocateMatrix();
    if (!mv) return NULL;
    matrix_4x4_set_identity(&mf);
    mf.m[0][0] = mf.m[1][1] = mf.m[2][2] = 0.1f * scale;
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

static void lsOrthoFrame(const float dir[3], float uOut[3], float vOut[3])
{
    const float ref[3] = { fabsf(dir[1]) < 0.9f ? 0.0f : 1.0f, fabsf(dir[1]) < 0.9f ? 1.0f : 0.0f, 0.0f };
    lsCross3(dir, ref, uOut);
    lsNorm3(uOut, uOut);
    lsCross3(dir, uOut, vOut);
    lsNorm3(vOut, vOut);
}

static Gfx *lsEmitPrism(Gfx *gdl, const float aMm[3], const float dir[3], float lenMm,
                        float r0Mm, float r1Mm, u32 rgbaBase, u32 rgbaTip, int emissive)
{
    Vtx *v = dynAllocateVertices(12);
    if (!v) return gdl;
    float u[3], w[3];
    lsOrthoFrame(dir, u, w);
    const float bMm[3] = {
        aMm[0] + dir[0] * lenMm,
        aMm[1] + dir[1] * lenMm,
        aMm[2] + dir[2] * lenMm
    };
    for (int i = 0; i < 6; i++) {
        const float theta = (float)i * (6.2831853f / 6.0f);
        const float ca = cosf(theta), sa = sinf(theta);
        const float nx = u[0] * ca + w[0] * sa;
        const float ny = u[1] * ca + w[1] * sa;
        const float nz = u[2] * ca + w[2] * sa;
        const float ldot = emissive ? 1.0f : (0.72f + 0.28f * (ny * 0.75f + nx * 0.45f - nz * 0.25f));
        const u32 c0 = emissive ? rgbaBase : lsShadeRgba(rgbaBase, ldot);
        const u32 c1 = emissive ? rgbaTip : lsShadeRgba(rgbaTip, ldot);
        lsSetVtx(&v[i], aMm[0] + nx * r0Mm, aMm[1] + ny * r0Mm, aMm[2] + nz * r0Mm, c0);
        lsSetVtx(&v[6 + i], bMm[0] + nx * r1Mm, bMm[1] + ny * r1Mm, bMm[2] + nz * r1Mm, c1);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 12, 0);
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        gSP2Triangles(gdl++, i, j, 6 + j, 0, i, 6 + j, 6 + i, 0);
    }
    return gdl;
}

static Gfx *lsDrawLaserswordView(Gfx *gdl, const float hiltView[3], const float dirView[3],
                                 float lenCm, float extend01, int colorIdx, float scale)
{
    unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
    gevrLsPalette(colorIdx, &core, &glow);
    const float hiltStart[3] = { 0.0f, 0.0f, 0.0f };
    const float emitter[3] = { dirView[0] * 240.0f, dirView[1] * 240.0f, dirView[2] * 240.0f };

    Gfx *beg = lsBeginGeom(gdl, hiltView, scale, 0);
    if (!beg) return gdl;
    gdl = beg;
    gdl = lsEmitPrism(gdl, hiltStart, dirView, 240.0f, 17.0f, 15.0f, 0x282A30FFu, 0x808894FFu, 0);
    if (extend01 > 0.02f) {
        const float bladeMm = (lenCm * 10.0f - 240.0f) * extend01;
        gdl = lsEmitPrism(gdl, emitter, dirView, bladeMm, 11.0f, 6.0f, core, core, 1);
        beg = lsBeginGeom(gdl, hiltView, scale, 1);
        if (beg) {
            gdl = beg;
            const float pulse = 1.0f + 0.08f * sinf(s_time * 38.0f);
            gdl = lsEmitPrism(gdl, emitter, dirView, bladeMm + 25.0f, 25.0f * pulse, 14.0f * pulse, glow, glow & 0xFFFFFF90u, 1);
        }
    }
    return gdl;
}

static void lsWorldToView(const Mtxf *w2s, const float w[3], float s, float out[3])
{
    coord3d c;
    c.x = w[0]; c.y = w[1]; c.z = w[2];
    mtx4TransformVecInPlace((Mtxf *)w2s, &c);
    out[0] = c.x * s; out[1] = c.y * s; out[2] = c.z * s;
}

static Gfx *lsDrawTrailWorld(Gfx *gdl, const Mtxf *w2s, const LsBladeTrail *tr, unsigned int rgba, float scale)
{
    if (!tr || tr->count < 2 || !w2s) return gdl;
    float origV[3];
    lsWorldToView(w2s, tr->hilt[0], scale, origV);
    Gfx *beg = lsBeginGeom(gdl, origV, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(tr->count * 2);
    if (!v) return gdl;
    const float invS = 10.0f / scale;
    const unsigned int rgb = rgba & 0xFFFFFF00u;
    const unsigned int maxA = rgba & 0xFFu;
    for (int i = 0; i < tr->count; i++) {
        float hv[3], tv[3];
        lsWorldToView(w2s, tr->hilt[i], scale, hv);
        lsWorldToView(w2s, tr->tip[i], scale, tv);
        const float fade = 1.0f - (float)i / (float)tr->count;
        const unsigned int aTip = (unsigned int)((float)maxA * fade * 0.72f);
        const unsigned int aBase = (unsigned int)((float)maxA * fade * 0.16f);
        lsSetVtx(&v[i * 2 + 0], (hv[0] - origV[0]) * invS, (hv[1] - origV[1]) * invS, (hv[2] - origV[2]) * invS, rgb | aBase);
        lsSetVtx(&v[i * 2 + 1], (tv[0] - origV[0]) * invS, (tv[1] - origV[1]) * invS, (tv[2] - origV[2]) * invS, rgb | aTip);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), tr->count * 2, 0);
    for (int i = 0; i < tr->count - 1; i++) {
        const int o = i * 2;
        gSP2Triangles(gdl++, o, o + 1, o + 3, 0, o, o + 3, o + 2, 0);
    }
    return gdl;
}

static Gfx *lsDrawSparkView(Gfx *gdl, const float centerView[3], float radiusCm, unsigned int rgba, float scale)
{
    Gfx *beg = lsBeginGeom(gdl, centerView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(11);
    if (!v) return gdl;
    lsSetVtx(&v[0], 0, 0, 0, 0xFFFFFFFFu);
    for (int i = 0; i < 10; i++) {
        const float a = (float)i * (6.2831853f / 10.0f) + s_time * 8.0f;
        const float r = (i & 1) ? radiusCm * 10.0f : radiusCm * 4.2f;
        lsSetVtx(&v[1 + i], cosf(a) * r, sinf(a) * r, 0.0f, rgba);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 11, 0);
    for (int i = 0; i < 10; i++) {
        gSP1Triangle(gdl++, 0, 1 + i, 1 + ((i + 1) % 10), 0);
    }
    return gdl;
}

static Gfx *lsDrawBoltView(Gfx *gdl, const float aView[3], const float bView[3], unsigned int outerRgba, float scale)
{
    Gfx *beg = lsBeginGeom(gdl, aView, scale, 1);
    if (!beg) return gdl;
    gdl = beg;
    Vtx *v = dynAllocateVertices(4);
    if (!v) return gdl;
    const float d[3] = {
        (bView[0] - aView[0]) * 10.0f / scale,
        (bView[1] - aView[1]) * 10.0f / scale,
        (bView[2] - aView[2]) * 10.0f / scale
    };
    lsSetVtx(&v[0], 8.0f, 8.0f, 0.0f, outerRgba);
    lsSetVtx(&v[1], -8.0f, -8.0f, 0.0f, 0xFFFFFFFFu);
    lsSetVtx(&v[2], d[0] + 8.0f, d[1] + 8.0f, d[2], outerRgba);
    lsSetVtx(&v[3], d[0] - 8.0f, d[1] - 8.0f, d[2], 0xFFFFFFFFu);
    gSPVertex(gdl++, osVirtualToPhysical(v), 4, 0);
    gSP2Triangles(gdl++, 0, 1, 2, 0, 1, 3, 2, 0);
    return gdl;
}

Gfx *gevrLsDrawHand(Gfx *gdl, s32 hand)
{
    if (!lsHandHoldsKnife(hand)) return gdl;
    if (s_thrown.active && s_thrown.hand == hand) return gdl;

    const Mtxf *w2s = camGetWorldToScreenMtxf();
    const float s = lsViewScale();
    const float sideSign = (hand == GUNRIGHT) ? 1.0f : -1.0f;
    const int ctrl = (hand == GUNRIGHT) ? 1 : 0;
    Gfx *const startGdl = gdl;

    float hilt[3], tipDir[3];
    int haveVr = 0;
    if (g_gevrStereo) {
        float r[3], u[3], b[3];
        if (gevrLsGetGripAxes(ctrl, hilt, r, u, b)) {
            tipDir[0] = -b[0] * 0.35f + u[0] * 0.94f;
            tipDir[1] = -b[1] * 0.35f + u[1] * 0.94f;
            tipDir[2] = -b[2] * 0.35f + u[2] * 0.94f;
            lsNorm3(tipDir, tipDir);
            haveVr = 1;
        }
    }
    if (!haveVr) {
        if (s_blocking) {
            hilt[0] = -14.0f * sideSign * s; hilt[1] = -6.0f * s; hilt[2] = -34.0f * s;
            tipDir[0] = 0.65f * sideSign; tipDir[1] = 0.72f; tipDir[2] = -0.22f;
        } else if (s_slash.active) {
            const float u = s_slash.duration > 0.0f ? s_slash.t / s_slash.duration : 0.0f;
            const float swing = (s_slash.dir == 0 ? 1.0f - 2.0f * u : -1.0f + 2.0f * u) * sideSign;
            hilt[0] = (14.0f * swing) * s; hilt[1] = (-10.0f - 4.0f * sinf(u * 3.14159f)) * s; hilt[2] = -36.0f * s;
            tipDir[0] = -0.55f * swing; tipDir[1] = 0.35f - 0.5f * u; tipDir[2] = -0.75f;
        } else {
            hilt[0] = 17.0f * sideSign * s; hilt[1] = -14.0f * s; hilt[2] = -32.0f * s;
            tipDir[0] = -0.22f * sideSign; tipDir[1] = 0.72f; tipDir[2] = -0.66f;
        }
        lsNorm3(tipDir, tipDir);
    }

    unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
    gevrLsPalette(VrLaserswordColor, &core, &glow);
    gdl = lsDrawLaserswordView(gdl, hilt, tipDir, GEVR_LS_REACH_CM, s_ignite[hand].extend, VrLaserswordColor, s);
    if (w2s) {
        gdl = lsDrawTrailWorld(gdl, w2s, &s_trails[hand], glow, s);
    }

    if (gdl != startGdl) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    return gdl;
}

Gfx *gevrLsDrawWorld(Gfx *gdl)
{
    if (!g_CurrentPlayer || !lsInGameplay()) return gdl;
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    if (!w2s) return gdl;
    const float s = lsViewScale();
    Gfx *const startGdl = gdl;

    if (s_thrown.active) {
        float pv[3];
        lsWorldToView(w2s, s_thrown.pos, s, pv);
        if (pv[2] < -10.0f) {
            const float spin[3] = { cosf(s_thrown.spinAngle), 0.0f, sinf(s_thrown.spinAngle) };
            gdl = lsDrawLaserswordView(gdl, pv, spin, 105.0f, 1.0f, VrLaserswordColor, s);
        }
    }

    for (int i = 0; i < LS_MAX_SPARKS; i++) {
        if (!s_sparks[i].used) continue;
        float pv[3];
        lsWorldToView(w2s, s_sparks[i].pos, s, pv);
        if (pv[2] < -10.0f) {
            const float k = 1.0f - s_sparks[i].age / s_sparks[i].maxAge;
            gdl = lsDrawSparkView(gdl, pv, s_sparks[i].radius * (0.4f + 0.8f * k), s_sparks[i].rgba, s);
        }
    }

    for (int i = 0; i < LS_MAX_TRACERS; i++) {
        if (!s_tracers[i].used) continue;
        float av[3], bv[3];
        lsWorldToView(w2s, s_tracers[i].from, s, av);
        lsWorldToView(w2s, s_tracers[i].to, s, bv);
        gdl = lsDrawBoltView(gdl, av, bv, s_tracers[i].rgba, s);
    }

    if (gdl != startGdl) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    return gdl;
}
