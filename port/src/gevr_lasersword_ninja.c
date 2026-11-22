#include "gevr_lasersword_ninja.h"
#include "gevr_lasersword.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define GEVR_LN_HOLD_SEC       0.22f
#define GEVR_LN_DOUBLETAP_SEC  0.28f
#define GEVR_LN_SLASH_SEC      0.26f
#define GEVR_LN_BLOCK_COS      0.42f   /* ~65 deg half-cone */

static float lnNormalize3(const float v[3], float out[3])
{
    const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-6f) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = -1.0f;
        return 0.0f;
    }
    const float inv = 1.0f / len;
    out[0] = v[0] * inv;
    out[1] = v[1] * inv;
    out[2] = v[2] * inv;
    return len;
}

static float lnDot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void gevrLnLaserswordPalette(int color, unsigned int *coreRgba, unsigned int *glowRgba)
{
    gevrLsPalette(color, coreRgba, glowRgba);
}

float gevrLnWeaponReachCm(int mode, int sword)
{
    if (mode == GEVR_LN_LASERSWORD) {
        return 115.0f;
    }
    if (mode == GEVR_LN_NINJA) {
        switch (sword) {
            case GEVR_LN_SWORD_SHORT:
            case GEVR_LN_SWORD_GOLD_SHORT:
            case GEVR_LN_SWORD_DUAL_SHORT:
            case GEVR_LN_SWORD_DUAL_GOLD_SHORT:
                return 70.0f;
            case GEVR_LN_SWORD_KATANA:
            case GEVR_LN_SWORD_GOLD_KATANA:
            default:
                return 108.0f;
        }
    }
    return 65.0f;
}

int gevrLnIsDualSword(int mode, int sword)
{
    return mode == GEVR_LN_NINJA
        && (sword == GEVR_LN_SWORD_DUAL_SHORT || sword == GEVR_LN_SWORD_DUAL_GOLD_SHORT);
}

int gevrLnIsGoldSword(int sword)
{
    return sword == GEVR_LN_SWORD_GOLD_KATANA
        || sword == GEVR_LN_SWORD_GOLD_SHORT
        || sword == GEVR_LN_SWORD_DUAL_GOLD_SHORT;
}

float gevrLnRivalScaleFactor(int setting, unsigned int seed)
{
    switch (setting) {
        case GEVR_LN_SCALE_GIANT:
            return 1.35f;
        case GEVR_LN_SCALE_TINY:
            return 0.62f;
        case GEVR_LN_SCALE_RANDOM: {
            static const float kPool[3] = { 0.62f, 1.0f, 1.35f };
            unsigned int h = seed ^ (seed >> 16);
            h *= 0x45d9f3bu;
            h ^= h >> 16;
            return kPool[h % 3u];
        }
        case GEVR_LN_SCALE_NORMAL:
        default:
            return 1.0f;
    }
}

void gevrLnTriggerReset(GevrLnTrigger *t)
{
    GevrLsTrigger ls;
    if (!t) return;
    gevrLsTriggerReset(&ls);
    t->down = ls.down;
    t->heldSec = ls.heldSec;
    t->holdFired = ls.holdFired;
    t->tapTimer = ls.tapTimer;
    t->pendingTap = ls.pendingTap;
}

int gevrLnTriggerUpdate(GevrLnTrigger *t, int pressed, float dt)
{
    GevrLsTrigger ls;
    if (!t) return GEVR_LN_TRIG_NONE;
    ls.down = t->down;
    ls.heldSec = t->heldSec;
    ls.holdFired = t->holdFired;
    ls.tapTimer = t->tapTimer;
    ls.pendingTap = t->pendingTap;
    const int ev = gevrLsTriggerUpdate(&ls, pressed, dt);
    t->down = ls.down;
    t->heldSec = ls.heldSec;
    t->holdFired = ls.holdFired;
    t->tapTimer = ls.tapTimer;
    t->pendingTap = ls.pendingTap;
    return ev;
}

int gevrLnSlashStart(GevrLnSlash *s, int blocking)
{
    GevrLsSlash ls;
    if (!s) return 0;
    ls.nextSide = s->nextSide;
    ls.active = s->active;
    ls.dir = s->dir;
    ls.t = s->t;
    ls.duration = s->duration;
    const int started = gevrLsSlashStart(&ls, blocking);
    s->nextSide = ls.nextSide;
    s->active = ls.active;
    s->dir = ls.dir;
    s->t = ls.t;
    s->duration = ls.duration;
    return started;
}

float gevrLnSlashStep(GevrLnSlash *s, float dt)
{
    GevrLsSlash ls;
    if (!s) return -1.0f;
    ls.nextSide = s->nextSide;
    ls.active = s->active;
    ls.dir = s->dir;
    ls.t = s->t;
    ls.duration = s->duration;
    const float phase = gevrLsSlashStep(&ls, dt);
    s->nextSide = ls.nextSide;
    s->active = ls.active;
    s->dir = ls.dir;
    s->t = ls.t;
    s->duration = ls.duration;
    return phase;
}

int gevrLnVrWristThrow(const float handVelCmS[3], const float aimFwd[3], int releasedFromHold)
{
    return gevrLsVrWristThrow(handVelCmS, aimFwd, releasedFromHold);
}

int gevrLnVrWristPull(const float handVelCmS[3], const float aimFwd[3], int triggerHeld)
{
    float dir[3];
    if (!triggerHeld || !handVelCmS || !aimFwd) return 0;
    if (lnNormalize3(aimFwd, dir) <= 0.0f) return 0;
    return lnDot3(handVelCmS, dir) <= -160.0f;
}

float gevrLnIgnitionUpdate(GevrLnIgnition *ig, int drawn, int isFirstPerson, float dt)
{
    if (!ig) return 0.0f;
    if (dt < 0.0f) dt = 0.0f;

    if (!drawn) {
        ig->active = 0;
        ig->extend = 0.0f;
        return 0.0f;
    }

    if (isFirstPerson && ig->ignitedOnce) {
        ig->active = 1;
        ig->extend = 1.0f;
        return 1.0f;
    }

    if (!ig->active) {
        ig->active = 1;
        ig->extend = 0.0f;
    }

    ig->extend += dt / GEVR_LN_IGNITE_SEC;
    if (ig->extend >= 1.0f) {
        ig->extend = 1.0f;
        if (isFirstPerson) {
            ig->ignitedOnce = 1;
        }
    }
    return ig->extend;
}

void gevrLnThrownLaunch(GevrLnThrown *th, int isBoomerang, const float origin[3], const float aimDir[3])
{
    if (!th || !origin || !aimDir) return;
    memset(th, 0, sizeof(*th));
    th->active = 1;
    th->isBoomerang = isBoomerang ? 1 : 0;
    th->returning = 0;
    memcpy(th->pos, origin, sizeof(th->pos));
    memcpy(th->startPos, origin, sizeof(th->startPos));
    lnNormalize3(aimDir, th->dir);
    th->speed = isBoomerang ? 950.0f : 850.0f;
    th->maxRange = isBoomerang ? 1400.0f : 1200.0f;
    th->spinRate = isBoomerang ? 14.0f : 16.0f;
    th->maxAge = isBoomerang ? 4.5f : 4.0f;
    for (int i = 0; i < 3; i++) {
        th->vel[i] = th->dir[i] * th->speed;
    }
}

int gevrLnThrownStep(GevrLnThrown *th, const float ownerPos[3], int hitWall, float dt)
{
    if (!th || !th->active || !ownerPos) return 0;
    if (dt < 0.0f) dt = 0.0f;

    th->age += dt;
    th->spinAngle += th->spinRate * dt;
    if (th->age >= th->maxAge) {
        th->active = 0;
        th->returning = 0;
        return 1;
    }

    if (!th->returning) {
        const float step = th->speed * dt;
        for (int i = 0; i < 3; i++) {
            th->pos[i] += th->dir[i] * step;
            th->vel[i] = th->dir[i] * th->speed;
        }
        th->distTraveled += step;
        if (hitWall || th->distTraveled >= th->maxRange) {
            th->returning = 1;
        }
        return 0;
    }

    float toOwner[3] = {
        ownerPos[0] - th->pos[0],
        ownerPos[1] - th->pos[1],
        ownerPos[2] - th->pos[2]
    };
    const float dist = sqrtf(lnDot3(toOwner, toOwner));
    if (dist <= 55.0f) {
        th->active = 0;
        th->returning = 0;
        return 1;
    }

    float steer[3];
    lnNormalize3(toOwner, steer);
    if (th->isBoomerang && dist > 140.0f) {
        float t = (dist - 140.0f) / 400.0f;
        if (t > 1.0f) t = 1.0f;
        const float lat = 0.28f * t;
        steer[0] += -steer[2] * lat;
        steer[2] += steer[0] * lat;
        lnNormalize3(steer, steer);
    }

    const float retSpeed = th->speed * 1.12f;
    const float step = retSpeed * dt;
    if (step >= dist - 5.0f) {
        memcpy(th->pos, ownerPos, sizeof(th->pos));
        th->active = 0;
        th->returning = 0;
        return 1;
    }

    for (int i = 0; i < 3; i++) {
        th->dir[i] = steer[i];
        th->vel[i] = steer[i] * retSpeed;
        th->pos[i] += steer[i] * step;
    }
    return 0;
}

void gevrLnChokeTargetPos(const float origin[3], const float fwd[3], float outPos[3])
{
    float dir[3];
    if (!origin || !fwd || !outPos) return;
    lnNormalize3(fwd, dir);
    outPos[0] = origin[0] + dir[0] * GEVR_LN_CHOKE_DIST_CM;
    outPos[1] = origin[1] + dir[1] * GEVR_LN_CHOKE_DIST_CM + 15.0f;
    outPos[2] = origin[2] + dir[2] * GEVR_LN_CHOKE_DIST_CM;
}

void gevrLnLimbOrbitPos(const float center[3], int slot, int count, float timeSec, float outPos[3])
{
    if (!center || !outPos) return;
    if (count < 1) count = 1;
    if (count > GEVR_LN_MAX_LIMBS) count = GEVR_LN_MAX_LIMBS;
    if (slot < 0) slot = 0;
    if (slot >= count) slot %= count;

    /* Fibonacci sphere with slow temporal swirl */
    const float y = 1.0f - 2.0f * ((float)slot + 0.5f) / (float)count;
    const float rHoriz = sqrtf(fmaxf(0.0f, 1.0f - y * y));
    const float goldenAngle = 2.39996323f;
    const float swirl = timeSec * (1.6f + 0.15f * (float)(slot & 3));
    const float theta = (float)slot * goldenAngle + swirl;
    const float radius = 22.0f + 16.0f * ((float)(slot % 5) / 4.0f);

    outPos[0] = center[0] + cosf(theta) * rHoriz * radius;
    outPos[1] = center[1] + y * radius + sinf(timeSec * 2.5f + (float)slot) * 3.0f;
    outPos[2] = center[2] + sinf(theta) * rHoriz * radius;
}

float gevrLnLimbBlastDamage(float distCm)
{
    if (distCm < 0.0f) distCm = 0.0f;
    if (distCm <= 250.0f) return 120.0f;
    if (distCm >= 1200.0f) return 0.0f;
    if (distCm <= 900.0f) {
        const float u = (distCm - 250.0f) / (900.0f - 250.0f);
        return 120.0f - 100.0f * u;
    }
    const float u = (distCm - 900.0f) / (1200.0f - 900.0f);
    return 20.0f * (1.0f - u);
}

void gevrLnThrownBodyInit(GevrLnThrownBody *tb, float massKg, float speedCmS)
{
    if (!tb) return;
    tb->massKg = massKg > 1.0f ? massKg : 80.0f;
    tb->speedCmS = speedCmS > 0.0f ? speedCmS : 0.0f;
    const float vm = tb->speedCmS * 0.01f;
    tb->energyJ = 0.5f * tb->massKg * vm * vm;
    tb->travelCm = 0.0f;
    tb->dismemberedCount = 0;
    tb->hitCount = 0;
}

int gevrLnThrownBodyImpact(GevrLnThrownBody *tb, float travelCm, float *outKnockSpeedCmS)
{
    if (!tb) {
        if (outKnockSpeedCmS) *outKnockSpeedCmS = 0.0f;
        return GEVR_LN_IMPACT_NONE;
    }
    tb->travelCm = travelCm;
    tb->hitCount++;

    int outcome = GEVR_LN_IMPACT_NONE;
    if (travelCm <= 200.0f && tb->dismemberedCount < 2 && tb->energyJ >= 2500.0f) {
        outcome = GEVR_LN_IMPACT_DISMEMBER;
        tb->dismemberedCount++;
        tb->energyJ *= 0.52f;
    } else if (tb->energyJ >= 900.0f) {
        outcome = GEVR_LN_IMPACT_RAGDOLL_KILL;
        tb->energyJ *= 0.65f;
    } else if (tb->energyJ >= 250.0f) {
        outcome = GEVR_LN_IMPACT_KNOCKDOWN;
        tb->energyJ *= 0.60f;
    } else {
        tb->energyJ = 0.0f;
    }

    const float vCmS = tb->energyJ > 0.0f ? sqrtf(2.0f * tb->energyJ / tb->massKg) * 100.0f : 0.0f;
    tb->speedCmS = vCmS;
    if (outKnockSpeedCmS) *outKnockSpeedCmS = vCmS;
    return outcome;
}

int gevrLnCheckBlock(int mode, int blocking, int hasNinjaShield,
                     const float playerFwd[3], const float toShooterDir[3],
                     float outReflectDir[3])
{
    float fwd[3], toShooter[3];
    if (!blocking || !playerFwd || !toShooterDir) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (mode != GEVR_LN_LASERSWORD && mode != GEVR_LN_NINJA) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (lnNormalize3(playerFwd, fwd) <= 0.0f || lnNormalize3(toShooterDir, toShooter) <= 0.0f) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (lnDot3(fwd, toShooter) < GEVR_LN_BLOCK_COS) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (mode == GEVR_LN_LASERSWORD || hasNinjaShield) {
        if (outReflectDir) {
            outReflectDir[0] = toShooter[0];
            outReflectDir[1] = toShooter[1];
            outReflectDir[2] = toShooter[2];
        }
        return GEVR_LN_BLOCK_DEFLECT;
    }
    return GEVR_LN_BLOCK_ABSORB;
}

int gevrLnBulletTravelToShooter(const float bulletTravel[3], float outToShooter[3])
{
    return gevrLsBulletTravelToShooter(bulletTravel, outToShooter);
}

int gevrLnCheckVrBladeBlock(int mode, int blocking, int hasNinjaShield,
                            const float playerFwd[3], const float bladeDirW[3],
                            const float handAimW[3], const float toShooterDir[3],
                            float outReflectDir[3])
{
    float fwdN[3], bladeN[3], aimN[3], toShooterN[3];
    if (!blocking || !playerFwd || !bladeDirW || !handAimW || !toShooterDir) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (mode != GEVR_LN_LASERSWORD && mode != GEVR_LN_NINJA) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (lnNormalize3(playerFwd, fwdN) <= 0.0f
        || lnNormalize3(bladeDirW, bladeN) <= 0.0f
        || lnNormalize3(handAimW, aimN) <= 0.0f
        || lnNormalize3(toShooterDir, toShooterN) <= 0.0f) {
        return GEVR_LN_BLOCK_NONE;
    }

    if (mode == GEVR_LN_NINJA && hasNinjaShield) {
        if (lnDot3(aimN, toShooterN) < 0.45f) {
            return GEVR_LN_BLOCK_NONE;
        }
        if (outReflectDir) {
            const float blend[3] = {
                aimN[0] * 0.65f + toShooterN[0] * 0.35f,
                aimN[1] * 0.65f + toShooterN[1] * 0.35f,
                aimN[2] * 0.65f + toShooterN[2] * 0.35f
            };
            if (lnNormalize3(blend, outReflectDir) <= 0.0f) {
                memcpy(outReflectDir, toShooterN, sizeof(toShooterN));
            }
        }
        return GEVR_LN_BLOCK_DEFLECT;
    }

    if (lnDot3(fwdN, toShooterN) < 0.30f) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (fabsf(lnDot3(bladeN, toShooterN)) > 0.88f) {
        return GEVR_LN_BLOCK_NONE;
    }
    if (mode == GEVR_LN_LASERSWORD) {
        if (outReflectDir) {
            const float *src = (lnDot3(aimN, fwdN) >= 0.25f) ? aimN : toShooterN;
            outReflectDir[0] = src[0];
            outReflectDir[1] = src[1];
            outReflectDir[2] = src[2];
        }
        return GEVR_LN_BLOCK_DEFLECT;
    }
    return GEVR_LN_BLOCK_ABSORB;
}

int gevrLnSegmentHitCylinder(const float segA[3], const float segB[3],
                             const float cylBase[3], float cylRadius, float cylHeight,
                             float outHitPt[3])
{
    return gevrLsSegmentHitCylinder(segA, segB, cylBase, cylRadius, cylHeight, outHitPt);
}

static int s_lnFlatRtDown;
static int s_lnFlatLtDown;
static int s_lnFlatIsMouse;

void gevrLnInputFeedFlat(int rtDown, int ltDown, int isMouse)
{
    s_lnFlatRtDown = rtDown ? 1 : 0;
    s_lnFlatLtDown = ltDown ? 1 : 0;
    s_lnFlatIsMouse = isMouse ? 1 : 0;
}

void gevrLnInputReadFlat(int *rtDown, int *ltDown, int *isMouse)
{
    if (rtDown) *rtDown = s_lnFlatRtDown;
    if (ltDown) *ltDown = s_lnFlatLtDown;
    if (isMouse) *isMouse = s_lnFlatIsMouse;
}

void gevrLnReflectVec(const float v[3], const float n[3], float out[3])
{
    gevrLsReflectVec(v, n, out);
}

/* -------------------------------------------------------------------------- */
/* Procedural DSP Audio Synthesizer (Lasersword Hum, Doppler, Clashes, Force) */
/* -------------------------------------------------------------------------- */

#define GEVR_LN_TAU        6.28318530718f
#define GEVR_LN_SFX_VOICES 6

typedef struct {
    int active;
    int id;
    float intensity;
    float t;
    float dur;
    float p0;
    float p1;
} LnSfxVoice;

static struct {
    int active;
    int isLasersword;
    float bladeExtend;
    float swingTarget;
    float swingSmooth;
    int forceHold;
    int forceLightning;
    int thrownSpin;
    int thrownBoom;
    float humEnv;
    float forceEnv;
    float lightEnv;
    float thrownEnv;
    float phHum1;
    float phHum2;
    float phHumLfo;
    float phForce;
    float phThrown;
    unsigned int rng;
    LnSfxVoice voices[GEVR_LN_SFX_VOICES];
    int nextVoice;
} s_audio = { .rng = 0x13579BDFu };

static float lnClamp01(float x)
{
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float lnNoise(unsigned int *state)
{
    *state = (*state) * 1664525u + 1013904223u;
    const int s = (int)((*state >> 16) & 0xFFFFu) - 32768;
    return (float)s * (1.0f / 32768.0f);
}

static float lnWrapPhase(float p)
{
    if (p >= GEVR_LN_TAU) p -= GEVR_LN_TAU * (float)((int)(p / GEVR_LN_TAU));
    return p;
}

void gevrLnAudioReset(void)
{
    const unsigned int seed = s_audio.rng ? s_audio.rng : 0x13579BDFu;
    memset(&s_audio, 0, sizeof(s_audio));
    s_audio.rng = seed;
}

void gevrLnAudioSetState(int active, int isLasersword, float bladeExtend, float swingSpeed01,
                         int forceChokeOrPull, int forceLightning,
                         int thrownSpinning, int thrownIsBoomerang)
{
    s_audio.active = active ? 1 : 0;
    s_audio.isLasersword = isLasersword ? 1 : 0;
    s_audio.bladeExtend = lnClamp01(bladeExtend);
    s_audio.swingTarget = lnClamp01(swingSpeed01);
    s_audio.forceHold = forceChokeOrPull ? 1 : 0;
    s_audio.forceLightning = forceLightning ? 1 : 0;
    s_audio.thrownSpin = thrownSpinning ? 1 : 0;
    s_audio.thrownBoom = thrownIsBoomerang ? 1 : 0;
}

void gevrLnAudioTrigger(int eventId, float intensity)
{
    if (eventId < GEVR_LN_SFX_IGNITE || eventId > GEVR_LN_SFX_SHADOW_STEP) return;
    if (intensity < 0.15f) intensity = 0.15f;
    if (intensity > 2.0f) intensity = 2.0f;
    LnSfxVoice *v = &s_audio.voices[s_audio.nextVoice];
    s_audio.nextVoice = (s_audio.nextVoice + 1) % GEVR_LN_SFX_VOICES;
    memset(v, 0, sizeof(*v));
    v->active = 1;
    v->id = eventId;
    v->intensity = intensity;
    v->t = 0.0f;
    switch (eventId) {
        case GEVR_LN_SFX_IGNITE:      v->dur = 0.42f; break;
        case GEVR_LN_SFX_SLASH_LASERSWORD: v->dur = 0.28f; break;
        case GEVR_LN_SFX_SLASH_STEEL: v->dur = 0.22f; break;
        case GEVR_LN_SFX_DEFLECT:     v->dur = 0.26f; break;
        case GEVR_LN_SFX_CLASH_LASERSWORD: v->dur = 0.30f; break;
        case GEVR_LN_SFX_CLASH_METAL: v->dur = 0.34f; break;
        case GEVR_LN_SFX_FORCE_PUSH:  v->dur = 0.46f; break;
        case GEVR_LN_SFX_SHADOW_STEP: v->dur = 0.38f; break;
        default:                      v->dur = 0.20f; break;
    }
}

void gevrLnAudioMix(short *pcmStereo, int frames, int sampleRate)
{
    if (!pcmStereo || frames <= 0 || sampleRate <= 0) return;

    int anyVoice = 0;
    for (int i = 0; i < GEVR_LN_SFX_VOICES; i++) {
        if (s_audio.voices[i].active) { anyVoice = 1; break; }
    }
    if (!s_audio.active && !anyVoice
        && s_audio.humEnv < 1e-4f && s_audio.forceEnv < 1e-4f
        && s_audio.lightEnv < 1e-4f && s_audio.thrownEnv < 1e-4f) {
        return;
    }

    const float dt = 1.0f / (float)sampleRate;
    const float envStep = dt * 14.0f;

    for (int n = 0; n < frames; n++) {
        const float humTgt = (s_audio.active && s_audio.isLasersword) ? s_audio.bladeExtend : 0.0f;
        const float forceTgt = (s_audio.active && s_audio.forceHold) ? 1.0f : 0.0f;
        const float lightTgt = (s_audio.active && s_audio.forceLightning) ? 1.0f : 0.0f;
        const float thrTgt = (s_audio.active && s_audio.thrownSpin) ? 1.0f : 0.0f;

        s_audio.humEnv += (humTgt - s_audio.humEnv) * envStep;
        s_audio.forceEnv += (forceTgt - s_audio.forceEnv) * envStep;
        s_audio.lightEnv += (lightTgt - s_audio.lightEnv) * (dt * 24.0f);
        s_audio.thrownEnv += (thrTgt - s_audio.thrownEnv) * (dt * 18.0f);
        s_audio.swingSmooth += (s_audio.swingTarget - s_audio.swingSmooth) * (dt * 22.0f);

        float sigL = 0.0f, sigR = 0.0f;

        /* Warm dual-detuned lasersword hum + swing Doppler modulation */
        if (s_audio.humEnv > 0.002f) {
            const float sw = s_audio.swingSmooth;
            const float fBase = 92.0f + sw * 46.0f;
            s_audio.phHum1 = lnWrapPhase(s_audio.phHum1 + GEVR_LN_TAU * fBase * dt);
            s_audio.phHum2 = lnWrapPhase(s_audio.phHum2 + GEVR_LN_TAU * (fBase * 2.015f + 1.8f) * dt);
            s_audio.phHumLfo = lnWrapPhase(s_audio.phHumLfo + GEVR_LN_TAU * (6.2f + sw * 5.0f) * dt);

            const float lfo = 0.86f + 0.14f * sinf(s_audio.phHumLfo);
            const float o1 = sinf(s_audio.phHum1);
            const float o2 = sinf(s_audio.phHum2);
            const float hum = (0.58f * o1 + 0.28f * o2 + 0.14f * o1 * o2) * lfo;
            const float amp = s_audio.humEnv * (0.045f + 0.095f * sw);
            const float pan = 0.12f * sinf(s_audio.phHumLfo);
            sigL += hum * amp * (1.0f - pan);
            sigR += hum * amp * (1.0f + pan);
        }

        /* Force Choke / Pull sub-bass rumble */
        if (s_audio.forceEnv > 0.002f) {
            s_audio.phForce = lnWrapPhase(s_audio.phForce + GEVR_LN_TAU * 56.0f * dt);
            const float rumble = (0.72f * sinf(s_audio.phForce) + 0.28f * sinf(s_audio.phForce * 1.98f));
            const float amp = s_audio.forceEnv * 0.09f;
            sigL += rumble * amp;
            sigR += rumble * amp;
        }

        /* Force Lightning continuous arc crackle */
        if (s_audio.lightEnv > 0.002f) {
            const float z = lnNoise(&s_audio.rng);
            const float snap = fabsf(z) > 0.84f ? z * 1.5f : z * 0.25f;
            const float arc = 0.55f * sinf(s_audio.phHum2 * 1.7f) + 0.45f * snap;
            const float amp = s_audio.lightEnv * 0.095f;
            sigL += arc * amp;
            sigR += -arc * 0.7f * amp + lnNoise(&s_audio.rng) * amp * 0.35f;
        }

        /* Thrown lasersword or boomerang spin whoosh */
        if (s_audio.thrownEnv > 0.002f) {
            const float spinHz = s_audio.thrownBoom ? 11.5f : 14.0f;
            s_audio.phThrown = lnWrapPhase(s_audio.phThrown + GEVR_LN_TAU * spinHz * dt);
            const float mod = 0.5f + 0.5f * sinf(s_audio.phThrown);
            const float tone = s_audio.thrownBoom ? lnNoise(&s_audio.rng) * 0.6f : sinf(s_audio.phHum1 * 1.35f) * 0.75f;
            const float whoosh = tone * mod * s_audio.thrownEnv * 0.095f;
            const float pan = 0.45f * cosf(s_audio.phThrown);
            sigL += whoosh * (1.0f - pan);
            sigR += whoosh * (1.0f + pan);
        }

        /* One-shot event synthesizers */
        for (int i = 0; i < GEVR_LN_SFX_VOICES; i++) {
            LnSfxVoice *v = &s_audio.voices[i];
            if (!v->active) continue;
            v->t += dt;
            if (v->t >= v->dur) {
                v->active = 0;
                continue;
            }
            const float u = v->t / v->dur;
            float mono = 0.0f;
            const float nz = lnNoise(&s_audio.rng);

            if (v->id == GEVR_LN_SFX_IGNITE) {
                const float f = 140.0f + 560.0f * (1.0f - (1.0f - u) * (1.0f - u));
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * f * dt);
                const float hissEnv = expf(-u * 6.5f);
                const float sweepEnv = sinf(lnClamp01(u) * 3.14159f);
                mono = (0.65f * nz * hissEnv + 0.55f * sinf(v->p0) * sweepEnv) * 0.24f;
            } else if (v->id == GEVR_LN_SFX_SLASH_LASERSWORD) {
                const float f = 110.0f + 160.0f * sinf(u * 3.14159f);
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * f * dt);
                const float env = sinf(u * 3.14159f);
                mono = (0.70f * sinf(v->p0) + 0.30f * sinf(v->p0 * 2.02f) + 0.18f * nz) * env * 0.16f;
            } else if (v->id == GEVR_LN_SFX_SLASH_STEEL) {
                const float env = sinf(u * 3.14159f) * (1.0f - 0.4f * u);
                const float f = 900.0f + 700.0f * (1.0f - u);
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * f * dt);
                mono = (0.70f * nz + 0.30f * sinf(v->p0)) * env * 0.16f;
            } else if (v->id == GEVR_LN_SFX_DEFLECT) {
                const float f = 2200.0f * expf(-u * 3.4f) + 260.0f;
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * f * dt);
                const float env = expf(-u * 5.2f);
                mono = (0.75f * sinf(v->p0) + 0.35f * nz * expf(-u * 11.0f)) * env * 0.22f;
            } else if (v->id == GEVR_LN_SFX_CLASH_LASERSWORD) {
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * 310.0f * dt);
                v->p1 = lnWrapPhase(v->p1 + GEVR_LN_TAU * 437.0f * dt);
                const float crackle = fabsf(nz) > 0.70f ? nz * 1.3f : nz * 0.35f;
                const float env = expf(-u * 4.5f);
                mono = (0.45f * sinf(v->p0 + 1.5f * sinf(v->p1)) + 0.65f * crackle) * env * 0.23f;
            } else if (v->id == GEVR_LN_SFX_CLASH_METAL) {
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * 1380.0f * dt);
                v->p1 = lnWrapPhase(v->p1 + GEVR_LN_TAU * 2137.0f * dt);
                const float env = expf(-u * 6.0f);
                mono = (0.60f * sinf(v->p0 + 1.8f * sinf(v->p1)) + 0.35f * sinf(v->p1) + 0.30f * nz * expf(-u * 14.0f)) * env * 0.22f;
            } else if (v->id == GEVR_LN_SFX_FORCE_PUSH) {
                const float f = 140.0f * expf(-u * 3.5f) + 38.0f;
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * f * dt);
                const float env = expf(-u * 3.2f);
                mono = (0.72f * sinf(v->p0) + 0.45f * nz * expf(-u * 5.5f)) * env * 0.26f;
            } else if (v->id == GEVR_LN_SFX_SHADOW_STEP) {
                v->p0 = lnWrapPhase(v->p0 + GEVR_LN_TAU * (620.0f + 500.0f * u) * dt);
                const float env = expf(-u * 4.8f);
                mono = (0.78f * nz + 0.28f * sinf(v->p0)) * env * 0.20f;
            }

            mono *= v->intensity;
            sigL += mono;
            sigR += mono;
        }

        const float baseL = (float)pcmStereo[n * 2 + 0] * (1.0f / 32768.0f);
        const float baseR = (float)pcmStereo[n * 2 + 1] * (1.0f / 32768.0f);
        float outL = baseL + sigL;
        float outR = baseR + sigR;
        if (outL > 1.0f) outL = 1.0f - 0.1f * expf(-(outL - 1.0f));
        else if (outL < -1.0f) outL = -1.0f + 0.1f * expf(outL + 1.0f);
        if (outR > 1.0f) outR = 1.0f - 0.1f * expf(-(outR - 1.0f));
        else if (outR < -1.0f) outR = -1.0f + 0.1f * expf(outR + 1.0f);
        const int iL = (int)(outL * 32767.0f);
        const int iR = (int)(outR * 32767.0f);
        pcmStereo[n * 2 + 0] = (short)(iL < -32767 ? -32767 : iL > 32767 ? 32767 : iL);
        pcmStereo[n * 2 + 1] = (short)(iR < -32767 ? -32767 : iR > 32767 ? 32767 : iR);
    }
}
