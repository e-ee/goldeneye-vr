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

#define LS_MAX_CUTS      8
#define LS_MAX_SPARKS    12
#define LS_MAX_TRACERS   8
#define LS_TRAIL_SAMPLES 7
#define LS_VR_HIT_SLOTS  16
#define LS_PARRY_WINDOW  0.22f

static struct {
    GevrLsTrigger rtTrig, ltTrig;
    GevrLsSlash slash;
    GevrLsIgnition ignite[2];
    GevrLsThrown thrown;
    int blocking;
    float blockHoldSec;
    int granted;
    s32 lastStage;
    float time;
    float wallSparkCd[2];
    float vrWhooshCd[2];
    float vrThrowGuard[2];
    int vrTrigWas[2];
    ChrRecord *vrHitChr[2][LS_VR_HIT_SLOTS];
    float vrHitCd[2][LS_VR_HIT_SLOTS];
    ChrRecord *throwHitChr[12];
    float throwHitCd[12];
    int throwHitCount;
    struct {
        int count;
        float hilt[LS_TRAIL_SAMPLES][3], tip[LS_TRAIL_SAMPLES][3], age[LS_TRAIL_SAMPLES];
    } trails[2];
    int vrPoseValid[2], vrBlocking[2];
    float vrHiltW[2][3], vrTipW[2][3], vrDirW[2][3], vrAimW[2][3], vrRightW[2][3];
    struct { int used, cutsLeft; float pos[3], vel[3], ttl; } cuts[LS_MAX_CUTS];
    struct { int used; float pos[3], radius, age, maxAge; unsigned int rgba; } sparks[LS_MAX_SPARKS];
    struct { int used; float from[3], to[3], age, maxAge; unsigned int rgba; } tracers[LS_MAX_TRACERS];
} s_ls = { .lastStage = -1 };

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static float lsViewScale(void) { return (D_800364CC > 1e-6f) ? D_800364CC : 1.0f; }

static float lsNorm3(const float v[3], float out[3])
{
    const float l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l2 < 1e-10f) { out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f; return 0.0f; }
    const float len = sqrtf(l2), inv = 1.0f / len;
    out[0] = v[0] * inv; out[1] = v[1] * inv; out[2] = v[2] * inv;
    return len;
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

static int lsInGameplay(void)
{
    if (!g_CurrentPlayer || g_CurrentPlayer->bonddead || g_PlayerIsInTank == 1) return 0;
    if (g_StageNum == LEVELID_TITLE || g_CurrentPlayer->pause_state != 0) return 0;
    if (g_CurrentPlayer->watch_animation_state != 0 || lvlGetControlsLockedFlag()) return 0;
    if (gevrSpectating() || gevrCoopLocalDowned()) return 0;
    return (netIsActive() && getPlayerCount() > 1) ? 0 : 1;
}

static int lsActive(void)
{
    return VrLasersword != 0 && VrLaserswordNinjaMode == 0 && lsInGameplay() && gevrLsPackGet() != NULL;
}

static int lsHandHoldsKnife(s32 hand)
{
    return (lsActive() && hand >= 0 && hand <= 1 && getCurrentPlayerWeaponId(hand) == ITEM_KNIFE) ? 1 : 0;
}

static int lsChrEnemy(ChrRecord *c)
{
    if (!c || !c->prop || !c->model || !c->prop->stan) return 0;
    if (c->chrnum < 0 || c->prop->type != PROP_TYPE_CHR) return 0;
    if (chrIsDead(c) || (c->hidden & CHRHIDDEN_REMOVE) || (c->chrflags & CHRFLAG_HIDDEN)) return 0;
    if (gevrAllyIs(c) || (c->chrflags & CHRFLAG_COUNT_DEATH_AS_CIVILIAN)) return 0;
    return c->actiontype != ACT_SURRENDER;
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
    return (pdphysIsReady() && pdphysRaycastAnyRoom(from, to, NULL, NULL, NULL)) ? 0 : 1;
}

static void lsAddSpark(const float pos[3], unsigned int rgba, float radius, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < LS_MAX_SPARKS; i++) {
        if (!s_ls.sparks[i].used) { slot = i; break; }
        if (s_ls.sparks[i].age > oldest) { oldest = s_ls.sparks[i].age; slot = i; }
    }
    s_ls.sparks[slot].used = 1;
    memcpy(s_ls.sparks[slot].pos, pos, 12);
    s_ls.sparks[slot].rgba = rgba;
    s_ls.sparks[slot].radius = radius;
    s_ls.sparks[slot].age = 0.0f;
    s_ls.sparks[slot].maxAge = maxAge;
}

static void lsAddTracer(const float from[3], const float to[3], unsigned int rgba, float maxAge)
{
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < LS_MAX_TRACERS; i++) {
        if (!s_ls.tracers[i].used) { slot = i; break; }
        if (s_ls.tracers[i].age > oldest) { oldest = s_ls.tracers[i].age; slot = i; }
    }
    s_ls.tracers[slot].used = 1;
    memcpy(s_ls.tracers[slot].from, from, 12);
    memcpy(s_ls.tracers[slot].to, to, 12);
    s_ls.tracers[slot].rgba = rgba;
    s_ls.tracers[slot].age = 0.0f;
    s_ls.tracers[slot].maxAge = maxAge;
}

static void lsQueueCut(const float pos[3], const float vel[3], int maxCuts)
{
    if (gevrRagdollForceCutNear(pos, 95.0f, vel, maxCuts) > 0) return;
    for (int i = 0; i < LS_MAX_CUTS; i++) {
        if (!s_ls.cuts[i].used) {
            s_ls.cuts[i].used = 1;
            memcpy(s_ls.cuts[i].pos, pos, 12);
            memcpy(s_ls.cuts[i].vel, vel, 12);
            s_ls.cuts[i].cutsLeft = maxCuts;
            s_ls.cuts[i].ttl = 0.25f;
            return;
        }
    }
}

static void lsTrailClearHand(s32 hand)
{
    if (hand >= 0 && hand < 2) memset(&s_ls.trails[hand], 0, sizeof(s_ls.trails[hand]));
}

static void lsTrailStepAge(float dt)
{
    for (int h = 0; h < 2; h++) {
        int keep = 0;
        for (int i = 0; i < s_ls.trails[h].count; i++) {
            s_ls.trails[h].age[i] += dt;
            if (s_ls.trails[h].age[i] <= 0.14f) keep = i + 1;
            else break;
        }
        s_ls.trails[h].count = keep;
    }
}

static void lsTrailPushWorld(s32 hand, const float hiltW[3], const float tipW[3])
{
    if (hand < 0 || hand > 1) return;
    if (s_ls.trails[hand].count > 0) {
        const float dx = tipW[0] - s_ls.trails[hand].tip[0][0];
        const float dy = tipW[1] - s_ls.trails[hand].tip[0][1];
        const float dz = tipW[2] - s_ls.trails[hand].tip[0][2];
        if (dx * dx + dy * dy + dz * dz < 16.0f) return;
    }
    const int n = s_ls.trails[hand].count < LS_TRAIL_SAMPLES ? s_ls.trails[hand].count : LS_TRAIL_SAMPLES - 1;
    for (int i = n; i > 0; i--) {
        memcpy(s_ls.trails[hand].hilt[i], s_ls.trails[hand].hilt[i - 1], 12);
        memcpy(s_ls.trails[hand].tip[i], s_ls.trails[hand].tip[i - 1], 12);
        s_ls.trails[hand].age[i] = s_ls.trails[hand].age[i - 1];
    }
    memcpy(s_ls.trails[hand].hilt[0], hiltW, 12);
    memcpy(s_ls.trails[hand].tip[0], tipW, 12);
    s_ls.trails[hand].age[0] = 0.0f;
    if (s_ls.trails[hand].count < LS_TRAIL_SAMPLES) s_ls.trails[hand].count++;
}

static void lsGetCameraWorld(float eye[3], float right[3], float up[3], float fwd[3])
{
    Mtxf *v2w = currentPlayerGetViewToWorldMtxf();
    if (!v2w) {
        memset(eye, 0, 12);
        right[0] = 1; right[1] = 0; right[2] = 0;
        up[0] = 0; up[1] = 1; up[2] = 0;
        fwd[0] = 0; fwd[1] = 0; fwd[2] = -1;
        return;
    }
    for (int i = 0; i < 3; i++) {
        eye[i] = v2w->m[3][i];
        right[i] = v2w->m[0][i];
        up[i] = v2w->m[1][i];
        fwd[i] = -v2w->m[2][i];
    }
    lsNorm3(right, right);
    lsNorm3(up, up);
    lsNorm3(fwd, fwd);
}

static void lsGetTileOrigin(float outOrigin[3], StandTile **outStan)
{
    PropRecord *me = getCurrentPlayerProp();
    if (me && me->stan) {
        memcpy(outOrigin, me->pos.f, 12);
        *outStan = me->stan;
    } else {
        memset(outOrigin, 0, 12);
        *outStan = NULL;
    }
}

static void lsHitGuard(ChrRecord *c, const float hitPt[3], const float dir[3], float dmgAmount, int forceCutCount)
{
    if (!lsChrEnemy(c)) return;
    coord3d d = { { dir[0], dir[1], dir[2] } };
    const f32 targetDmg = c->damage + dmgAmount;

    if (targetDmg < c->maxdamage) {
        const f32 savedMax = c->maxdamage;
        c->maxdamage = targetDmg + 1000.0f;
        handles_shot_actors(c, HIT_CHEST, &d, ITEM_KNIFE, FALSE);
        c->maxdamage = savedMax;
        if (!chrIsDead(c)) c->damage = targetDmg;
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
        if (s_ls.vrHitChr[hand][i] == c && s_ls.vrHitCd[hand][i] > 0.0f) return 0;
    }
    return 1;
}

static void lsVrMarkHitChr(int hand, ChrRecord *c, float cd)
{
    int freeIdx = 0;
    float minCd = 1e9f;
    for (int i = 0; i < LS_VR_HIT_SLOTS; i++) {
        if (s_ls.vrHitChr[hand][i] == c) { s_ls.vrHitCd[hand][i] = cd; return; }
        if (s_ls.vrHitCd[hand][i] <= 0.0f) { freeIdx = i; break; }
        if (s_ls.vrHitCd[hand][i] < minCd) { minCd = s_ls.vrHitCd[hand][i]; freeIdx = i; }
    }
    s_ls.vrHitChr[hand][freeIdx] = c;
    s_ls.vrHitCd[hand][freeIdx] = cd;
}

static ChrRecord *lsFindShooter(const float eye[3], const float dir[3], float maxDistCm, float minDot)
{
    float pOrigin[3];
    StandTile *pStan = NULL;
    lsGetTileOrigin(pOrigin, &pStan);

    ChrRecord *best = NULL;
    float bestScore = 1e30f;
    const s32 n = get_numguards();
    for (s32 i = 0; i < n; i++) {
        ChrRecord *c = &g_ChrSlots[i];
        if (!lsChrEnemy(c)) continue;
        const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
        const float dx = tgt[0] - eye[0], dy = tgt[1] - eye[1], dz = tgt[2] - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < 100.0f || d2 > maxDistCm * maxDistCm) continue;
        const float d = sqrtf(d2);
        const float dot = (dx * dir[0] + dy * dir[1] + dz * dir[2]) / d;
        if (dot >= minDot && lsHasLos(eye, pStan, pOrigin, tgt)) {
            const float score = d * (1.4f - dot);
            if (score < bestScore) { bestScore = score; best = c; }
        }
    }
    return best;
}

/* -------------------------------------------------------------------------- */
/* Lifecycle & Queries                                                        */
/* -------------------------------------------------------------------------- */

void gevrLsGameReset(void)
{
    memset(&s_ls, 0, sizeof(s_ls));
    s_ls.lastStage = g_StageNum;
    gevrLsAudioReset();
}

s32 gevrLsHandActive(s32 hand) { return lsHandHoldsKnife(hand); }

s32 gevrLsNpcLasersActive(void)
{
    return (lsActive() && VrLaserswordNpcLasers != 0) ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
/* Gameplay Tick                                                              */
/* -------------------------------------------------------------------------- */

static void lsLaunchThrow(int hand, const float origin[3], const float aim[3], int ctrl)
{
    gevrLsThrownLaunch(&s_ls.thrown, origin, aim);
    s_ls.thrown.hand = hand;
    s_ls.throwHitCount = 0;
    lsTrailClearHand(hand);
    gevrLsAudioTrigger(GEVR_LS_SFX_SLASH, 1.2f);
    if (ctrl >= 0) lsHaptic(ctrl, 0.7f, 0.08f);
}

void gevrLsGameTick(s32 ticks60)
{
    if (g_StageNum != s_ls.lastStage) gevrLsGameReset();
    const float dt = ticks60 > 0 ? fminf((float)ticks60 / 60.0f, 0.05f) : (1.0f / 60.0f);
    s_ls.time += dt;

    for (int i = 0; i < LS_MAX_CUTS; i++) {
        if (!s_ls.cuts[i].used) continue;
        if (gevrRagdollForceCutNear(s_ls.cuts[i].pos, 95.0f, s_ls.cuts[i].vel, s_ls.cuts[i].cutsLeft) > 0) {
            s_ls.cuts[i].used = 0;
        } else if ((s_ls.cuts[i].ttl -= dt) <= 0.0f) {
            s_ls.cuts[i].used = 0;
        }
    }
    for (int i = 0; i < LS_MAX_SPARKS; i++) {
        if (s_ls.sparks[i].used && (s_ls.sparks[i].age += dt) >= s_ls.sparks[i].maxAge) s_ls.sparks[i].used = 0;
    }
    for (int i = 0; i < LS_MAX_TRACERS; i++) {
        if (s_ls.tracers[i].used && (s_ls.tracers[i].age += dt) >= s_ls.tracers[i].maxAge) s_ls.tracers[i].used = 0;
    }
    lsTrailStepAge(dt);

    if (!lsActive()) {
        gevrLsAudioSetState(0, 0.0f, 0.0f, 0);
        return;
    }
    if (!s_ls.granted) {
        bondinvAddInvItem(ITEM_KNIFE);
        s_ls.granted = 1;
    }

    const int holdR = lsHandHoldsKnife(GUNRIGHT);
    const int holdL = lsHandHoldsKnife(GUNLEFT);
    if (!holdR && !holdL && !s_ls.thrown.active) {
        for (int h = 0; h < 2; h++) {
            gevrLsIgnitionUpdate(&s_ls.ignite[h], 0, dt);
            lsTrailClearHand(h);
            s_ls.vrPoseValid[h] = s_ls.vrBlocking[h] = 0;
        }
        s_ls.blocking = 0;
        s_ls.blockHoldSec = 0.0f;
        gevrLsAudioSetState(0, 0.0f, 0.0f, 0);
        return;
    }

    for (int h = 0; h < 2; h++) {
        if (s_ls.wallSparkCd[h] > 0.0f) s_ls.wallSparkCd[h] -= dt;
        if (s_ls.vrWhooshCd[h] > 0.0f) s_ls.vrWhooshCd[h] -= dt;
        if (s_ls.vrThrowGuard[h] > 0.0f) s_ls.vrThrowGuard[h] -= dt;
        for (int k = 0; k < LS_VR_HIT_SLOTS; k++) {
            if (s_ls.vrHitCd[h][k] > 0.0f) s_ls.vrHitCd[h][k] -= dt;
        }
        const int drawn = lsHandHoldsKnife(h) && !(s_ls.thrown.active && s_ls.thrown.hand == h);
        const float prevExt = s_ls.ignite[h].extend;
        const float ext = gevrLsIgnitionUpdate(&s_ls.ignite[h], drawn, dt);
        if (drawn && prevExt <= 0.02f && ext > 0.02f) gevrLsAudioTrigger(GEVR_LS_SFX_IGNITE, 1.0f);
        if (!drawn) {
            lsTrailClearHand(h);
            s_ls.vrPoseValid[h] = s_ls.vrBlocking[h] = 0;
        }
    }

    float eye[3], right[3], up[3], fwd[3], pOrigin[3];
    StandTile *pStan = NULL;
    lsGetCameraWorld(eye, right, up, fwd);
    lsGetTileOrigin(pOrigin, &pStan);

    float maxSwing01 = 0.0f, maxExtend = 0.0f;

    if (g_gevrStereo) {
        s_ls.blocking = 0;
        for (int h = 0; h < 2; h++) {
            const int ctrl = (h == GUNRIGHT) ? 1 : 0;
            if (!lsHandHoldsKnife(h) || (s_ls.thrown.active && s_ls.thrown.hand == h)) continue;
            if (s_ls.ignite[h].extend > maxExtend) maxExtend = s_ls.ignite[h].extend;

            const int trigNow = get_analog_value(ctrl, "trigger") > 0.5f;
            const int trigPressed = trigNow && !s_ls.vrTrigWas[ctrl];
            const int trigReleased = !trigNow && s_ls.vrTrigWas[ctrl];
            s_ls.vrTrigWas[ctrl] = trigNow;
            if (trigPressed) s_ls.vrThrowGuard[ctrl] = 0.35f;
            s_ls.vrBlocking[h] = trigNow;
            if (trigNow) s_ls.blocking = 1;

            float hr[3], hu[3], hf[3], hPos[3], velCm[3] = { 0.0f, 0.0f, 0.0f };
            gevrLsGetHandVelocityWorld(ctrl, velCm);
            if (!gevrLsGetHandPoseWorld(ctrl, hPos, hr, hu, hf)) continue;

            float prevTip[3];
            const int hasPrev = s_ls.vrPoseValid[h];
            if (hasPrev) memcpy(prevTip, s_ls.vrTipW[h], 12);

            float dirW[3] = {
                hf[0] * 0.35f + hu[0] * 0.94f,
                hf[1] * 0.35f + hu[1] * 0.94f,
                hf[2] * 0.35f + hu[2] * 0.94f
            };
            lsNorm3(dirW, dirW);
            const float activeLen = 91.0f * s_ls.ignite[h].extend + 24.0f;
            const float tipW[3] = {
                hPos[0] + dirW[0] * activeLen,
                hPos[1] + dirW[1] * activeLen,
                hPos[2] + dirW[2] * activeLen
            };
            memcpy(s_ls.vrHiltW[h], hPos, 12);
            memcpy(s_ls.vrTipW[h], tipW, 12);
            memcpy(s_ls.vrDirW[h], dirW, 12);
            memcpy(s_ls.vrAimW[h], hf, 12);
            memcpy(s_ls.vrRightW[h], hr, 12);
            s_ls.vrPoseValid[h] = 1;
            lsTrailPushWorld(h, hPos, tipW);

            if (!s_ls.thrown.active && trigReleased && s_ls.vrThrowGuard[ctrl] <= 0.0f
                && gevrLsVrWristThrow(velCm, hf, 1)) {
                lsLaunchThrow(h, hPos, hf, ctrl);
                continue;
            }

            const float spd = sqrtf(lsDot3(velCm, velCm));
            const float sw01 = fminf(1.0f, spd / 240.0f);
            if (sw01 > maxSwing01) maxSwing01 = sw01;

            if (spd >= 115.0f && s_ls.ignite[h].extend > 0.35f) {
                float velDir[3];
                lsNorm3(velCm, velDir);
                if (s_ls.vrWhooshCd[h] <= 0.0f) {
                    gevrLsAudioTrigger(GEVR_LS_SFX_SLASH, 0.75f + 0.45f * sw01);
                    s_ls.vrWhooshCd[h] = 0.22f;
                }
                const s32 n = get_numguards();
                for (s32 i = 0; i < n; i++) {
                    ChrRecord *c = &g_ChrSlots[i];
                    if (!lsChrEnemy(c) || !lsVrCanHitChr(h, c)) continue;
                    const float r = (c->chrwidth > 12.0f ? c->chrwidth : 20.0f) + 18.0f;
                    float hitPt[3];
                    int hit = gevrLsSegmentHitCylinder(hPos, tipW, c->prop->pos.f, r, 185.0f, hitPt);
                    if (!hit && hasPrev) hit = gevrLsSegmentHitCylinder(prevTip, tipW, c->prop->pos.f, r, 185.0f, hitPt);
                    if (!hit) continue;
                    const float tgt[3] = { c->prop->pos.x, c->prop->pos.y + 115.0f, c->prop->pos.z };
                    if (!lsHasLos(hPos, pStan, pOrigin, tgt)) continue;

                    lsVrMarkHitChr(h, c, 0.24f);
                    lsHitGuard(c, hitPt, velDir, 6.0f + 8.0f * sw01, 2);
                    lsHaptic(ctrl, 0.85f, 0.08f);
                }
                const float midPt[3] = { (hPos[0] + tipW[0]) * 0.5f, (hPos[1] + tipW[1]) * 0.5f, (hPos[2] + tipW[2]) * 0.5f };
                if (gevrRagdollForceCutNear(midPt, 52.0f, velCm, 1) > 0
                    || gevrRagdollForceCutNear(tipW, 42.0f, velCm, 1) > 0) {
                    lsHaptic(ctrl, 0.65f, 0.05f);
                }
            }

            if (pdphysIsReady() && s_ls.ignite[h].extend > 0.5f && s_ls.wallSparkCd[h] <= 0.0f) {
                float wallPos[3];
                if (pdphysRaycastAnyRoom(hPos, tipW, wallPos, NULL, NULL)) {
                    lsAddSpark(wallPos, 0xFF9838FFu, 15.0f, 0.14f);
                    lsHaptic(ctrl, 0.25f, 0.02f);
                    s_ls.wallSparkCd[h] = 0.055f;
                }
            }
        }
    } else {
        const int h = lsHandHoldsKnife(GUNRIGHT) ? GUNRIGHT : GUNLEFT;
        const int inHand = !(s_ls.thrown.active && s_ls.thrown.hand == h);
        maxExtend = s_ls.ignite[h].extend;

        int rtDown = 0, ltDown = 0, isMouse = 0;
        gevrLsInputReadFlat(&rtDown, &ltDown, &isMouse);
        (void)isMouse;
        const int rtEvt = gevrLsTriggerUpdate(&s_ls.rtTrig, rtDown && inHand, dt);
        const int ltEvt = gevrLsTriggerUpdate(&s_ls.ltTrig, ltDown && inHand, dt);
        s_ls.blocking = (ltDown && inHand) ? 1 : 0;

        if (rtEvt == GEVR_LS_TRIG_TAP && inHand && gevrLsSlashStart(&s_ls.slash, s_ls.blocking)) {
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
                if (lsDot3(fwd, to) < 0.35f || !lsHasLos(eye, pStan, pOrigin, tgt)) continue;
                float aimY = eye[1] + fwd[1] * fminf(d, GEVR_LS_REACH_CM) + 8.0f;
                const float yLo = c->prop->pos.y + 45.0f, yHi = c->prop->pos.y + 162.0f;
                if (aimY < yLo) aimY = yLo;
                if (aimY > yHi) aimY = yHi;
                const float hitPt[3] = { c->prop->pos.x, aimY, c->prop->pos.z };
                lsHitGuard(c, hitPt, to, 14.0f, 2);
            }
        }

        const float sp = gevrLsSlashStep(&s_ls.slash, dt);
        if (sp >= 0.0f) maxSwing01 = sinf(sp * 3.14159f);

        if (inHand) {
            const float sideSign = (h == GUNRIGHT) ? 1.0f : -1.0f;
            const float len = 91.0f * s_ls.ignite[h].extend + 24.0f;
            float hw[3], tw[3], wDir[3];
            if (s_ls.slash.active) {
                const float swing = (s_ls.slash.dir == 0 ? 1.0f - 2.0f * sp : -1.0f + 2.0f * sp) * sideSign;
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

        if (!s_ls.thrown.active && inHand && (ltEvt == GEVR_LS_TRIG_DOUBLE_TAP || rtEvt == GEVR_LS_TRIG_HOLD_RELEASE)) {
            const float throwOrigin[3] = { eye[0] + fwd[0] * 36.0f, eye[1] - up[1] * 8.0f, eye[2] + fwd[2] * 36.0f };
            lsLaunchThrow(h, throwOrigin, fwd, -1);
        }
    }

    s_ls.blockHoldSec = s_ls.blocking ? (s_ls.blockHoldSec + dt) : 0.0f;

    if (s_ls.thrown.active) {
        for (int i = 0; i < s_ls.throwHitCount; i++) {
            if (s_ls.throwHitCd[i] > 0.0f) s_ls.throwHitCd[i] -= dt;
        }
        float catchPos[3];
        if (s_ls.vrPoseValid[s_ls.thrown.hand]) {
            memcpy(catchPos, s_ls.vrHiltW[s_ls.thrown.hand], 12);
        } else {
            for (int k = 0; k < 3; k++) catchPos[k] = eye[k] + right[k] * 16.0f - up[k] * 10.0f + fwd[k] * 28.0f;
        }
        int hitWall = 0;
        if (!s_ls.thrown.returning && pdphysIsReady()) {
            const float nxt[3] = {
                s_ls.thrown.pos[0] + s_ls.thrown.vel[0] * dt,
                s_ls.thrown.pos[1] + s_ls.thrown.vel[1] * dt,
                s_ls.thrown.pos[2] + s_ls.thrown.vel[2] * dt
            };
            float hitPos[3];
            if (pdphysRaycastAnyRoom(s_ls.thrown.pos, nxt, hitPos, NULL, NULL)) {
                hitWall = 1;
                lsAddSpark(hitPos, 0xFF9838FFu, 22.0f, 0.20f);
                gevrLsAudioTrigger(GEVR_LS_SFX_CLASH, 0.9f);
            }
        }
        gevrLsThrownStep(&s_ls.thrown, catchPos, hitWall, dt);

        const s32 n = get_numguards();
        for (s32 i = 0; i < n; i++) {
            ChrRecord *c = &g_ChrSlots[i];
            if (!lsChrEnemy(c)) continue;
            const float dx = c->prop->pos.x - s_ls.thrown.pos[0];
            const float dz = c->prop->pos.z - s_ls.thrown.pos[2];
            const float dy = s_ls.thrown.pos[1] - c->prop->pos.y;
            if (dx * dx + dz * dz > 85.0f * 85.0f || dy < -12.0f || dy > 195.0f) continue;

            int cdIdx = -1;
            for (int k = 0; k < s_ls.throwHitCount; k++) {
                if (s_ls.throwHitChr[k] == c) { cdIdx = k; break; }
            }
            if (cdIdx >= 0 && s_ls.throwHitCd[cdIdx] > 0.0f) continue;
            if (cdIdx < 0 && s_ls.throwHitCount < 12) { cdIdx = s_ls.throwHitCount++; s_ls.throwHitChr[cdIdx] = c; }
            if (cdIdx >= 0) s_ls.throwHitCd[cdIdx] = 0.28f;

            float dDir[3];
            lsNorm3(s_ls.thrown.vel, dDir);
            const float cutPt[3] = { c->prop->pos.x, s_ls.thrown.pos[1], c->prop->pos.z };
            lsHitGuard(c, cutPt, dDir, 24.0f, 2);
        }
    }

    gevrLsAudioSetState(1, fmaxf(maxExtend, s_ls.thrown.active ? 1.0f : 0.0f), maxSwing01, s_ls.thrown.active);
}

/* -------------------------------------------------------------------------- */
/* Damage Interception (Bullet Deflection)                                    */
/* -------------------------------------------------------------------------- */

s32 gevrLsInterceptPlayerDamage(f32 damageAmount, f32 vecX, f32 vecZ)
{
    (void)damageAmount;
    if (!lsActive()) return 0;

    int blockHand = -1;
    for (int h = 0; h < 2; h++) {
        if (!lsHandHoldsKnife(h) || (s_ls.thrown.active && s_ls.thrown.hand == h)) continue;
        if (g_gevrStereo ? s_ls.vrBlocking[h] : s_ls.blocking) { blockHand = h; break; }
    }
    if (blockHand < 0) return 0;

    float eye[3], right[3], up[3], fwd[3];
    lsGetCameraWorld(eye, right, up, fwd);

    const float travel[3] = { vecX, 0.0f, vecZ };
    float toShooter[3], reflectDir[3] = { 0.0f, 0.0f, 0.0f };
    if (!gevrLsBulletTravelToShooter(travel, toShooter)) memcpy(toShooter, fwd, 12);

    const int res = (g_gevrStereo && s_ls.vrPoseValid[blockHand])
        ? gevrLsCheckVrBladeBlock(1, fwd, s_ls.vrDirW[blockHand], s_ls.vrAimW[blockHand], toShooter, reflectDir)
        : gevrLsCheckBlock(1, fwd, toShooter, reflectDir);
    if (res != GEVR_LS_BLOCK_DEFLECT) return 0;

    const int perfect = (s_ls.blockHoldSec <= LS_PARRY_WINDOW);
    float bladePt[3];
    if (g_gevrStereo && s_ls.vrPoseValid[blockHand]) {
        for (int k = 0; k < 3; k++) bladePt[k] = s_ls.vrHiltW[blockHand][k] + s_ls.vrDirW[blockHand][k] * 55.0f;
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
    if (!shooter) shooter = lsFindShooter(eye, fwd, 2200.0f, 0.70f);
    const unsigned int boltCol = VrLaserswordNpcLasers ? (perfect ? 0xFFF480FFu : 0x58E8FFFFu) : glow;
    if (shooter) {
        const float tgt[3] = { shooter->prop->pos.x, shooter->prop->pos.y + 115.0f, shooter->prop->pos.z };
        const float d[3] = { tgt[0] - bladePt[0], tgt[1] - bladePt[1], tgt[2] - bladePt[2] };
        float nd[3];
        lsNorm3(d, nd);
        lsAddTracer(bladePt, tgt, boltCol, 0.18f);
        lsHitGuard(shooter, tgt, nd, perfect ? 18.0f : 3.5f, 0);
    } else {
        const float farPt[3] = { bladePt[0] + reflectDir[0] * 900.0f, bladePt[1] + reflectDir[1] * 900.0f, bladePt[2] + reflectDir[2] * 900.0f };
        lsAddTracer(bladePt, farPt, boltCol, 0.16f);
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Fast3D Rendering                                                           */
/* -------------------------------------------------------------------------- */

static void lsSetVtx(Vtx *v, f32 x, f32 y, f32 z, u32 rgba)
{
    v->v.ob[0] = (s16)x; v->v.ob[1] = (s16)y; v->v.ob[2] = (s16)z;
    v->v.flag = 0; v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = (rgba >> 24) & 0xff;
    v->v.cn[1] = (rgba >> 16) & 0xff;
    v->v.cn[2] = (rgba >> 8) & 0xff;
    v->v.cn[3] = rgba & 0xff;
}

static Gfx *lsBeginGeom(Gfx *gdl, const float originView[3], float scale, int translucent, int cullBack)
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
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | (cullBack ? G_CULL_BACK : 0));
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, translucent ? G_RM_ZB_XLU_SURF : G_RM_AA_ZB_OPA_SURF,
                     translucent ? G_RM_ZB_XLU_SURF2 : G_RM_AA_ZB_OPA_SURF2);
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    return gdl;
}

static void lsBuildBasis(const float tipDir[3], const float refRight[3], float bx[3], float by[3], float bz[3])
{
    lsNorm3(tipDir, by);
    float rx[3] = { 1.0f, 0.0f, 0.0f };
    if (refRight && lsDot3(refRight, refRight) > 0.01f) lsNorm3(refRight, rx);
    if (fabsf(lsDot3(rx, by)) > 0.94f) {
        rx[0] = fabsf(by[0]) < 0.9f ? 1.0f : 0.0f;
        rx[1] = 0.0f;
        rx[2] = fabsf(by[0]) < 0.9f ? 0.0f : 1.0f;
    }
    lsCross3(rx, by, bz);
    lsNorm3(bz, bz);
    lsCross3(by, bz, bx);
    lsNorm3(bx, bx);
}

static Gfx *lsDrawBladePass(Gfx *gdl, const float aMm[3], const float dir[3], float lenMm,
                            float r0Mm, float r1Mm, u32 cBase, u32 cTip)
{
    Vtx *v = dynAllocateVertices(12);
    if (!v) return gdl;
    float u[3], w[3], f[3];
    lsBuildBasis(dir, NULL, u, f, w);
    const float bMm[3] = { aMm[0] + f[0] * lenMm, aMm[1] + f[1] * lenMm, aMm[2] + f[2] * lenMm };
    for (int i = 0; i < 6; i++) {
        const float th = (float)i * (6.2831853f / 6.0f);
        const float nx = u[0] * cosf(th) + w[0] * sinf(th);
        const float ny = u[1] * cosf(th) + w[1] * sinf(th);
        const float nz = u[2] * cosf(th) + w[2] * sinf(th);
        lsSetVtx(&v[i], aMm[0] + nx * r0Mm, aMm[1] + ny * r0Mm, aMm[2] + nz * r0Mm, cBase);
        lsSetVtx(&v[6 + i], bMm[0] + nx * r1Mm, bMm[1] + ny * r1Mm, bMm[2] + nz * r1Mm, cTip);
    }
    gSPVertex(gdl++, osVirtualToPhysical(v), 12, 0);
    for (int i = 0; i < 6; i++) {
        const int j = (i + 1) % 6;
        gSP2Triangles(gdl++, i, j, 6 + j, 0, i, 6 + j, 6 + i, 0);
    }
    return gdl;
}

static Gfx *lsDrawPackMesh(Gfx *gdl, const GevrLsPack *pk, const float bx[3], const float by[3], const float bz[3])
{
    for (int t0 = 0; t0 < pk->triCount; t0 += 10) {
        const int batch = (pk->triCount - t0 > 10) ? 10 : (pk->triCount - t0);
        Vtx *v = dynAllocateVertices(batch * 3);
        if (!v) return gdl;
        for (int k = 0; k < batch * 3; k++) {
            const GevrLsVtx *s = &pk->verts[t0 * 3 + k];
            const u32 col = ((u32)s->rgba[0] << 24) | ((u32)s->rgba[1] << 16)
                          | ((u32)s->rgba[2] << 8) | (u32)s->rgba[3];
            lsSetVtx(&v[k],
                     (s->p[0] * bx[0] + s->p[1] * by[0] + s->p[2] * bz[0]) * 10.0f,
                     (s->p[0] * bx[1] + s->p[1] * by[1] + s->p[2] * bz[1]) * 10.0f,
                     (s->p[0] * bx[2] + s->p[1] * by[2] + s->p[2] * bz[2]) * 10.0f, col);
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

static Gfx *lsDrawLaserswordView(Gfx *gdl, const GevrLsPack *pk, const float hiltView[3],
                                 const float dirView[3], const float rightRef[3],
                                 float lenCm, float extend01, int colorIdx, float scale)
{
    if (!pk || !pk->loaded || !pk->verts) return gdl;
    unsigned int core = 0xFFFFFFFFu, glow = 0x2890FFE8u;
    gevrLsPalette(colorIdx, &core, &glow);

    float bx[3], by[3], bz[3];
    lsBuildBasis(dirView, rightRef, bx, by, bz);

    Gfx *beg = lsBeginGeom(gdl, hiltView, scale, 0, 1);
    if (!beg) return gdl;
    gdl = lsDrawPackMesh(beg, pk, bx, by, bz);

    if (extend01 > 0.02f) {
        const float emMm = pk->emitterOffsetCm * 10.0f;
        const float emitter[3] = { by[0] * emMm, by[1] * emMm, by[2] * emMm };
        const float bladeMm = fmaxf(0.0f, lenCm * 10.0f - emMm) * extend01;
        const float rMm = pk->bladeRadiusCm * 10.0f;

        beg = lsBeginGeom(gdl, hiltView, scale, 0, 0);
        if (beg) gdl = lsDrawBladePass(beg, emitter, by, bladeMm, rMm * 0.45f, rMm * 0.25f, core, core);

        beg = lsBeginGeom(gdl, hiltView, scale, 1, 0);
        if (beg) {
            const float pulse = 1.0f + 0.08f * sinf(s_ls.time * 38.0f);
            gdl = lsDrawBladePass(beg, emitter, by, bladeMm + 25.0f,
                                  rMm * pulse, rMm * 0.56f * pulse, glow, glow & 0xFFFFFF90u);
        }
    }
    return gdl;
}

static void lsWorldToView(const Mtxf *w2s, const float w[3], float s, float out[3])
{
    coord3d c = { { w[0], w[1], w[2] } };
    mtx4TransformVecInPlace((Mtxf *)w2s, &c);
    out[0] = c.x * s; out[1] = c.y * s; out[2] = c.z * s;
}

Gfx *gevrLsDrawHand(Gfx *gdl, s32 hand)
{
    if (!lsHandHoldsKnife(hand) || (s_ls.thrown.active && s_ls.thrown.hand == hand)) return gdl;
    const GevrLsPack *pk = gevrLsPackGet();
    if (!pk) return gdl;

    const Mtxf *w2s = camGetWorldToScreenMtxf();
    const float s = lsViewScale();
    const float sideSign = (hand == GUNRIGHT) ? 1.0f : -1.0f;
    const int ctrl = (hand == GUNRIGHT) ? 1 : 0;
    Gfx *const startGdl = gdl;

    float hilt[3], tipDir[3], rRef[3] = { sideSign, 0.0f, 0.0f };
    int haveVr = 0;
    if (g_gevrStereo) {
        float r[3], u[3], b[3];
        if (gevrLsGetGripAxes(ctrl, hilt, r, u, b)) {
            for (int k = 0; k < 3; k++) {
                tipDir[k] = -b[k] * 0.35f + u[k] * 0.94f;
                rRef[k] = r[k];
            }
            lsNorm3(tipDir, tipDir);
            haveVr = 1;
        }
    }
    if (!haveVr) {
        if (s_ls.blocking) {
            hilt[0] = -14.0f * sideSign * s; hilt[1] = -6.0f * s; hilt[2] = -34.0f * s;
            tipDir[0] = 0.65f * sideSign; tipDir[1] = 0.72f; tipDir[2] = -0.22f;
        } else if (s_ls.slash.active) {
            const float u = s_ls.slash.duration > 0.0f ? s_ls.slash.t / s_ls.slash.duration : 0.0f;
            const float swing = (s_ls.slash.dir == 0 ? 1.0f - 2.0f * u : -1.0f + 2.0f * u) * sideSign;
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
    gdl = lsDrawLaserswordView(gdl, pk, hilt, tipDir, rRef, GEVR_LS_REACH_CM, s_ls.ignite[hand].extend, VrLaserswordColor, s);

    if (w2s && s_ls.trails[hand].count >= 2) {
        float origV[3];
        lsWorldToView(w2s, s_ls.trails[hand].hilt[0], s, origV);
        Gfx *beg = lsBeginGeom(gdl, origV, s, 1, 0);
        Vtx *v = beg ? dynAllocateVertices(s_ls.trails[hand].count * 2) : NULL;
        if (v) {
            gdl = beg;
            const float invS = 10.0f / s;
            const unsigned int rgb = glow & 0xFFFFFF00u, maxA = glow & 0xFFu;
            for (int i = 0; i < s_ls.trails[hand].count; i++) {
                float hv[3], tv[3];
                lsWorldToView(w2s, s_ls.trails[hand].hilt[i], s, hv);
                lsWorldToView(w2s, s_ls.trails[hand].tip[i], s, tv);
                const float fade = 1.0f - (float)i / (float)s_ls.trails[hand].count;
                lsSetVtx(&v[i * 2], (hv[0] - origV[0]) * invS, (hv[1] - origV[1]) * invS, (hv[2] - origV[2]) * invS,
                         rgb | (unsigned int)((float)maxA * fade * 0.16f));
                lsSetVtx(&v[i * 2 + 1], (tv[0] - origV[0]) * invS, (tv[1] - origV[1]) * invS, (tv[2] - origV[2]) * invS,
                         rgb | (unsigned int)((float)maxA * fade * 0.72f));
            }
            gSPVertex(gdl++, osVirtualToPhysical(v), s_ls.trails[hand].count * 2, 0);
            for (int i = 0; i < s_ls.trails[hand].count - 1; i++) {
                const int o = i * 2;
                gSP2Triangles(gdl++, o, o + 1, o + 3, 0, o, o + 3, o + 2, 0);
            }
        }
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
    if (!lsActive()) return gdl;
    const GevrLsPack *pk = gevrLsPackGet();
    const Mtxf *w2s = camGetWorldToScreenMtxf();
    if (!pk || !w2s) return gdl;

    const float s = lsViewScale();
    Gfx *const startGdl = gdl;

    if (s_ls.thrown.active) {
        float pv[3];
        lsWorldToView(w2s, s_ls.thrown.pos, s, pv);
        if (pv[2] < -10.0f) {
            const float spin[3] = { cosf(s_ls.thrown.spinAngle), 0.0f, sinf(s_ls.thrown.spinAngle) };
            gdl = lsDrawLaserswordView(gdl, pk, pv, spin, NULL, 105.0f, 1.0f, VrLaserswordColor, s);
        }
    }

    for (int i = 0; i < LS_MAX_SPARKS; i++) {
        if (!s_ls.sparks[i].used) continue;
        float pv[3];
        lsWorldToView(w2s, s_ls.sparks[i].pos, s, pv);
        if (pv[2] >= -10.0f) continue;
        Gfx *beg = lsBeginGeom(gdl, pv, s, 1, 0);
        Vtx *v = beg ? dynAllocateVertices(11) : NULL;
        if (!v) continue;
        gdl = beg;
        const float rad = s_ls.sparks[i].radius * (0.4f + 0.8f * (1.0f - s_ls.sparks[i].age / s_ls.sparks[i].maxAge));
        lsSetVtx(&v[0], 0, 0, 0, 0xFFFFFFFFu);
        for (int k = 0; k < 10; k++) {
            const float a = (float)k * (6.2831853f / 10.0f) + s_ls.time * 8.0f;
            const float r = (k & 1) ? rad * 10.0f : rad * 4.2f;
            lsSetVtx(&v[1 + k], cosf(a) * r, sinf(a) * r, 0.0f, s_ls.sparks[i].rgba);
        }
        gSPVertex(gdl++, osVirtualToPhysical(v), 11, 0);
        for (int k = 0; k < 10; k++) gSP1Triangle(gdl++, 0, 1 + k, 1 + ((k + 1) % 10), 0);
    }

    for (int i = 0; i < LS_MAX_TRACERS; i++) {
        if (!s_ls.tracers[i].used) continue;
        float av[3], bv[3];
        lsWorldToView(w2s, s_ls.tracers[i].from, s, av);
        lsWorldToView(w2s, s_ls.tracers[i].to, s, bv);
        Gfx *beg = lsBeginGeom(gdl, av, s, 1, 0);
        Vtx *v = beg ? dynAllocateVertices(4) : NULL;
        if (!v) continue;
        gdl = beg;
        const float d[3] = { (bv[0] - av[0]) * 10.0f / s, (bv[1] - av[1]) * 10.0f / s, (bv[2] - av[2]) * 10.0f / s };
        lsSetVtx(&v[0], 8.0f, 8.0f, 0.0f, s_ls.tracers[i].rgba);
        lsSetVtx(&v[1], -8.0f, -8.0f, 0.0f, 0xFFFFFFFFu);
        lsSetVtx(&v[2], d[0] + 8.0f, d[1] + 8.0f, d[2], s_ls.tracers[i].rgba);
        lsSetVtx(&v[3], d[0] - 8.0f, d[1] - 8.0f, d[2], 0xFFFFFFFFu);
        gSPVertex(gdl++, osVirtualToPhysical(v), 4, 0);
        gSP2Triangles(gdl++, 0, 1, 2, 0, 1, 3, 2, 0);
    }

    if (gdl != startGdl) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
        gdl = combiner_bayer_lod_perspective(gdl);
    }
    return gdl;
}
