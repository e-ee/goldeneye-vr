#include "gevr_lasersword.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define GEVR_LS_HOLD_SEC       0.22f
#define GEVR_LS_DOUBLETAP_SEC  0.28f
#define GEVR_LS_SLASH_SEC      0.26f
#define GEVR_LS_BLOCK_COS      0.42f   /* ~65 deg half-cone */
#define GEVR_LS_TAU            6.28318530718f
#define GEVR_LS_SFX_VOICES     6

static float lsNormalize3(const float v[3], float out[3])
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

static float lsDot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void gevrLsPalette(int color, unsigned int *coreRgba, unsigned int *glowRgba)
{
    static const unsigned int kCore[GEVR_LS_COLOR_COUNT] = {
        0xE8F6FFFF, /* blue */
        0xEAFFE8FF, /* green */
        0xFFEAEAFF, /* red */
        0xF6E8FFFF, /* purple */
        0xFFFFE8FF, /* yellow */
        0xFFFFFFFF, /* white */
        0xFFF2E6FF, /* orange */
        0xFFF8E0FF, /* gold */
    };
    static const unsigned int kGlow[GEVR_LS_COLOR_COUNT] = {
        0x2890FFE8, /* blue */
        0x28FF50E8, /* green */
        0xFF2828E8, /* red */
        0xB038FFE8, /* purple */
        0xFFE620E8, /* yellow */
        0xD8E8FFE8, /* white */
        0xFF7018E8, /* orange */
        0xFFD030E8, /* gold */
    };
    const int idx = (color >= 0 && color < GEVR_LS_COLOR_COUNT) ? color : GEVR_LS_COLOR_BLUE;
    if (coreRgba) *coreRgba = kCore[idx];
    if (glowRgba) *glowRgba = kGlow[idx];
}

void gevrLsTriggerReset(GevrLsTrigger *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
}

int gevrLsTriggerUpdate(GevrLsTrigger *t, int pressed, float dt)
{
    if (!t) return GEVR_LS_TRIG_NONE;
    if (dt < 0.0f) dt = 0.0f;

    if (t->tapTimer > 0.0f) {
        t->tapTimer -= dt;
        if (t->tapTimer <= 0.0f) {
            t->tapTimer = 0.0f;
            t->pendingTap = 0;
        }
    }

    if (pressed && !t->down) {
        t->down = 1;
        t->heldSec = 0.0f;
        if (t->pendingTap && t->tapTimer > 0.0f) {
            t->pendingTap = 0;
            t->tapTimer = 0.0f;
            t->holdFired = 2; /* suppress release tap/hold after double-tap */
            return GEVR_LS_TRIG_DOUBLE_TAP;
        }
        t->holdFired = 0;
        return GEVR_LS_TRIG_NONE;
    }

    if (pressed && t->down) {
        if (t->holdFired == 2) {
            return GEVR_LS_TRIG_NONE;
        }
        t->heldSec += dt;
        if (t->holdFired == 0 && t->heldSec >= GEVR_LS_HOLD_SEC) {
            t->holdFired = 1;
            t->pendingTap = 0;
            t->tapTimer = 0.0f;
            return GEVR_LS_TRIG_HOLD_START;
        }
        if (t->holdFired == 1) {
            return GEVR_LS_TRIG_HOLD_ACTIVE;
        }
        return GEVR_LS_TRIG_NONE;
    }

    if (!pressed && t->down) {
        const int wasHold = t->holdFired;
        t->down = 0;
        t->heldSec = 0.0f;
        t->holdFired = 0;
        if (wasHold == 1) {
            return GEVR_LS_TRIG_HOLD_RELEASE;
        }
        if (wasHold == 2) {
            return GEVR_LS_TRIG_NONE;
        }
        t->pendingTap = 1;
        t->tapTimer = GEVR_LS_DOUBLETAP_SEC;
        return GEVR_LS_TRIG_TAP;
    }

    return GEVR_LS_TRIG_NONE;
}

int gevrLsSlashStart(GevrLsSlash *s, int blocking)
{
    if (!s || blocking || s->active) {
        return 0;
    }
    s->active = 1;
    s->dir = s->nextSide ? 1 : 0;
    s->nextSide = !s->dir;
    s->t = 0.0f;
    s->duration = GEVR_LS_SLASH_SEC;
    return 1;
}

float gevrLsSlashStep(GevrLsSlash *s, float dt)
{
    if (!s || !s->active) {
        return -1.0f;
    }
    if (dt < 0.0f) dt = 0.0f;
    if (s->duration <= 0.0f) s->duration = GEVR_LS_SLASH_SEC;
    s->t += dt;
    if (s->t >= s->duration) {
        s->active = 0;
        s->t = 0.0f;
        return 1.0f;
    }
    return s->t / s->duration;
}

int gevrLsVrWristThrow(const float handVelCmS[3], const float aimFwd[3], int releasedFromHold)
{
    float dir[3];
    if (!releasedFromHold || !handVelCmS || !aimFwd) return 0;
    if (lsNormalize3(aimFwd, dir) <= 0.0f) return 0;
    return lsDot3(handVelCmS, dir) >= 180.0f;
}

float gevrLsIgnitionUpdate(GevrLsIgnition *ig, int drawn, float dt)
{
    if (!ig) return 0.0f;
    if (dt < 0.0f) dt = 0.0f;

    if (!drawn) {
        ig->active = 0;
        ig->extend = 0.0f;
        return 0.0f;
    }

    if (!ig->active) {
        ig->active = 1;
        ig->extend = 0.0f;
    }

    ig->extend += dt / GEVR_LS_IGNITE_SEC;
    if (ig->extend >= 1.0f) {
        ig->extend = 1.0f;
    }
    return ig->extend;
}

void gevrLsThrownLaunch(GevrLsThrown *th, const float origin[3], const float aimDir[3])
{
    if (!th || !origin || !aimDir) return;
    memset(th, 0, sizeof(*th));
    th->active = 1;
    th->returning = 0;
    memcpy(th->pos, origin, sizeof(th->pos));
    lsNormalize3(aimDir, th->dir);
    th->speed = 850.0f;
    th->maxRange = 1200.0f;
    th->spinRate = 16.0f;
    th->maxAge = 4.0f;
    for (int i = 0; i < 3; i++) {
        th->vel[i] = th->dir[i] * th->speed;
    }
}

int gevrLsThrownStep(GevrLsThrown *th, const float ownerPos[3], int hitWall, float dt)
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

    const float toOwner[3] = {
        ownerPos[0] - th->pos[0],
        ownerPos[1] - th->pos[1],
        ownerPos[2] - th->pos[2]
    };
    const float dist = sqrtf(lsDot3(toOwner, toOwner));
    if (dist <= 55.0f) {
        th->active = 0;
        th->returning = 0;
        return 1;
    }

    float steer[3];
    lsNormalize3(toOwner, steer);
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

int gevrLsBulletTravelToShooter(const float bulletTravel[3], float outToShooter[3])
{
    float dir[3];
    if (!bulletTravel || !outToShooter) return 0;
    if (lsNormalize3(bulletTravel, dir) <= 0.0f) return 0;
    outToShooter[0] = -dir[0];
    outToShooter[1] = -dir[1];
    outToShooter[2] = -dir[2];
    return 1;
}

int gevrLsCheckBlock(int blocking, const float playerFwd[3], const float toShooterDir[3],
                     float outReflectDir[3])
{
    float fwd[3], toShooter[3];
    if (!blocking || !playerFwd || !toShooterDir) {
        return GEVR_LS_BLOCK_NONE;
    }
    if (lsNormalize3(playerFwd, fwd) <= 0.0f || lsNormalize3(toShooterDir, toShooter) <= 0.0f) {
        return GEVR_LS_BLOCK_NONE;
    }
    if (lsDot3(fwd, toShooter) < GEVR_LS_BLOCK_COS) {
        return GEVR_LS_BLOCK_NONE;
    }
    if (outReflectDir) {
        outReflectDir[0] = toShooter[0];
        outReflectDir[1] = toShooter[1];
        outReflectDir[2] = toShooter[2];
    }
    return GEVR_LS_BLOCK_DEFLECT;
}

int gevrLsCheckVrBladeBlock(int blocking, const float playerFwd[3], const float bladeDirW[3],
                            const float handAimW[3], const float toShooterDir[3],
                            float outReflectDir[3])
{
    float fwdN[3], bladeN[3], aimN[3], toShooterN[3];
    if (!blocking || !playerFwd || !bladeDirW || !handAimW || !toShooterDir) {
        return GEVR_LS_BLOCK_NONE;
    }
    if (lsNormalize3(playerFwd, fwdN) <= 0.0f
        || lsNormalize3(bladeDirW, bladeN) <= 0.0f
        || lsNormalize3(handAimW, aimN) <= 0.0f
        || lsNormalize3(toShooterDir, toShooterN) <= 0.0f) {
        return GEVR_LS_BLOCK_NONE;
    }
    if (lsDot3(fwdN, toShooterN) < 0.30f) {
        return GEVR_LS_BLOCK_NONE;
    }
    if (fabsf(lsDot3(bladeN, toShooterN)) > 0.88f) {
        return GEVR_LS_BLOCK_NONE;
    }
    if (outReflectDir) {
        const float *src = (lsDot3(aimN, fwdN) >= 0.25f) ? aimN : toShooterN;
        outReflectDir[0] = src[0];
        outReflectDir[1] = src[1];
        outReflectDir[2] = src[2];
    }
    return GEVR_LS_BLOCK_DEFLECT;
}

int gevrLsSegmentHitCylinder(const float segA[3], const float segB[3],
                             const float cylBase[3], float cylRadius, float cylHeight,
                             float outHitPt[3])
{
    if (!segA || !segB || !cylBase || cylRadius <= 0.0f || cylHeight <= 0.0f) {
        return 0;
    }
    const float yLo = cylBase[1] - 12.0f;
    const float yHi = cylBase[1] + cylHeight + 12.0f;
    const float dy = segB[1] - segA[1];
    float tMin = 0.0f;
    float tMax = 1.0f;

    if (fabsf(dy) < 1e-6f) {
        if (segA[1] < yLo || segA[1] > yHi) return 0;
    } else {
        float t0 = (yLo - segA[1]) / dy;
        float t1 = (yHi - segA[1]) / dy;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tMin) tMin = t0;
        if (t1 < tMax) tMax = t1;
        if (tMin > tMax) return 0;
    }

    const float dx = segB[0] - segA[0];
    const float dz = segB[2] - segA[2];
    const float len2xz = dx * dx + dz * dz;
    float t = 0.5f * (tMin + tMax);
    if (len2xz > 1e-8f) {
        t = ((cylBase[0] - segA[0]) * dx + (cylBase[2] - segA[2]) * dz) / len2xz;
        if (t < tMin) t = tMin;
        if (t > tMax) t = tMax;
    }

    const float hx = segA[0] + dx * t;
    const float hy = segA[1] + dy * t;
    const float hz = segA[2] + dz * t;
    const float ex = hx - cylBase[0];
    const float ez = hz - cylBase[2];
    if (ex * ex + ez * ez > cylRadius * cylRadius) {
        return 0;
    }
    if (outHitPt) {
        outHitPt[0] = hx;
        outHitPt[1] = hy;
        outHitPt[2] = hz;
    }
    return 1;
}

void gevrLsReflectVec(const float v[3], const float n[3], float out[3])
{
    float nn[3];
    if (!v || !n || !out) return;
    if (lsNormalize3(n, nn) <= 0.0f) {
        out[0] = -v[0];
        out[1] = -v[1];
        out[2] = -v[2];
        return;
    }
    const float d = 2.0f * lsDot3(v, nn);
    out[0] = v[0] - d * nn[0];
    out[1] = v[1] - d * nn[1];
    out[2] = v[2] - d * nn[2];
}

static int s_lsFlatRtDown;
static int s_lsFlatLtDown;
static int s_lsFlatIsMouse;

void gevrLsInputFeedFlat(int rtDown, int ltDown, int isMouse)
{
    s_lsFlatRtDown = rtDown ? 1 : 0;
    s_lsFlatLtDown = ltDown ? 1 : 0;
    s_lsFlatIsMouse = isMouse ? 1 : 0;
}

void gevrLsInputReadFlat(int *rtDown, int *ltDown, int *isMouse)
{
    if (rtDown) *rtDown = s_lsFlatRtDown;
    if (ltDown) *ltDown = s_lsFlatLtDown;
    if (isMouse) *isMouse = s_lsFlatIsMouse;
}

/* -------------------------------------------------------------------------- */
/* Procedural DSP Audio Synthesizer (22050 Hz stereo PCM)                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    int active;
    int id;
    float intensity;
    float t;
    float dur;
    float p0;
    float p1;
} LsSfxVoice;

static struct {
    int active;
    float bladeExtend;
    float swingTarget;
    float swingSmooth;
    int thrownSpin;
    float humEnv;
    float thrownEnv;
    float phHum1;
    float phHum2;
    float phHumLfo;
    float phThrown;
    unsigned int rng;
    LsSfxVoice voices[GEVR_LS_SFX_VOICES];
    int nextVoice;
} s_audio = { .rng = 0x2468ACE1u };

static float lsClamp01(float x)
{
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float lsNoise(unsigned int *state)
{
    *state = (*state) * 1664525u + 1013904223u;
    const int s = (int)((*state >> 16) & 0xFFFFu) - 32768;
    return (float)s * (1.0f / 32768.0f);
}

static float lsWrapPhase(float p)
{
    if (p >= GEVR_LS_TAU) p -= GEVR_LS_TAU * (float)((int)(p / GEVR_LS_TAU));
    return p;
}

void gevrLsAudioReset(void)
{
    const unsigned int seed = s_audio.rng ? s_audio.rng : 0x2468ACE1u;
    memset(&s_audio, 0, sizeof(s_audio));
    s_audio.rng = seed;
}

void gevrLsAudioSetState(int active, float bladeExtend, float swingSpeed01, int thrownSpinning)
{
    s_audio.active = active ? 1 : 0;
    s_audio.bladeExtend = lsClamp01(bladeExtend);
    s_audio.swingTarget = lsClamp01(swingSpeed01);
    s_audio.thrownSpin = thrownSpinning ? 1 : 0;
}

void gevrLsAudioTrigger(int eventId, float intensity)
{
    if (eventId < GEVR_LS_SFX_IGNITE || eventId > GEVR_LS_SFX_CLASH) return;
    if (intensity < 0.15f) intensity = 0.15f;
    if (intensity > 2.0f) intensity = 2.0f;
    LsSfxVoice *v = &s_audio.voices[s_audio.nextVoice];
    s_audio.nextVoice = (s_audio.nextVoice + 1) % GEVR_LS_SFX_VOICES;
    memset(v, 0, sizeof(*v));
    v->active = 1;
    v->id = eventId;
    v->intensity = intensity;
    v->t = 0.0f;
    switch (eventId) {
        case GEVR_LS_SFX_IGNITE:  v->dur = 0.42f; break;
        case GEVR_LS_SFX_SLASH:   v->dur = 0.28f; break;
        case GEVR_LS_SFX_DEFLECT: v->dur = 0.26f; break;
        case GEVR_LS_SFX_CLASH:   v->dur = 0.30f; break;
        default:                  v->dur = 0.20f; break;
    }
}

void gevrLsAudioMix(short *pcmStereo, int frames, int sampleRate)
{
    if (!pcmStereo || frames <= 0 || sampleRate <= 0) return;

    int anyVoice = 0;
    for (int i = 0; i < GEVR_LS_SFX_VOICES; i++) {
        if (s_audio.voices[i].active) { anyVoice = 1; break; }
    }
    if (!s_audio.active && !anyVoice
        && s_audio.humEnv < 1e-4f && s_audio.thrownEnv < 1e-4f) {
        return;
    }

    const float dt = 1.0f / (float)sampleRate;
    const float envStep = dt * 14.0f;

    for (int n = 0; n < frames; n++) {
        const float humTgt = s_audio.active ? s_audio.bladeExtend : 0.0f;
        const float thrTgt = (s_audio.active && s_audio.thrownSpin) ? 1.0f : 0.0f;

        s_audio.humEnv += (humTgt - s_audio.humEnv) * envStep;
        s_audio.thrownEnv += (thrTgt - s_audio.thrownEnv) * (dt * 18.0f);
        s_audio.swingSmooth += (s_audio.swingTarget - s_audio.swingSmooth) * (dt * 22.0f);

        float sigL = 0.0f, sigR = 0.0f;

        /* Dual-detuned blade hum + Doppler swing modulation */
        if (s_audio.humEnv > 0.002f) {
            const float sw = s_audio.swingSmooth;
            const float fBase = 92.0f + sw * 46.0f;
            s_audio.phHum1 = lsWrapPhase(s_audio.phHum1 + GEVR_LS_TAU * fBase * dt);
            s_audio.phHum2 = lsWrapPhase(s_audio.phHum2 + GEVR_LS_TAU * (fBase * 2.015f + 1.8f) * dt);
            s_audio.phHumLfo = lsWrapPhase(s_audio.phHumLfo + GEVR_LS_TAU * (6.2f + sw * 5.0f) * dt);

            const float lfo = 0.86f + 0.14f * sinf(s_audio.phHumLfo);
            const float o1 = sinf(s_audio.phHum1);
            const float o2 = sinf(s_audio.phHum2);
            const float hum = (0.58f * o1 + 0.28f * o2 + 0.14f * o1 * o2) * lfo;
            const float amp = s_audio.humEnv * (0.045f + 0.095f * sw);
            const float pan = 0.12f * sinf(s_audio.phHumLfo);
            sigL += hum * amp * (1.0f - pan);
            sigR += hum * amp * (1.0f + pan);
        }

        /* Thrown blade spin whoosh */
        if (s_audio.thrownEnv > 0.002f) {
            s_audio.phThrown = lsWrapPhase(s_audio.phThrown + GEVR_LS_TAU * 14.0f * dt);
            const float mod = 0.5f + 0.5f * sinf(s_audio.phThrown);
            const float tone = sinf(s_audio.phHum1 * 1.35f) * 0.75f;
            const float whoosh = tone * mod * s_audio.thrownEnv * 0.095f;
            const float pan = 0.45f * cosf(s_audio.phThrown);
            sigL += whoosh * (1.0f - pan);
            sigR += whoosh * (1.0f + pan);
        }

        for (int i = 0; i < GEVR_LS_SFX_VOICES; i++) {
            LsSfxVoice *v = &s_audio.voices[i];
            if (!v->active) continue;
            v->t += dt;
            if (v->t >= v->dur) {
                v->active = 0;
                continue;
            }
            const float u = v->t / v->dur;
            float mono = 0.0f;
            const float nz = lsNoise(&s_audio.rng);

            if (v->id == GEVR_LS_SFX_IGNITE) {
                const float f = 140.0f + 560.0f * (1.0f - (1.0f - u) * (1.0f - u));
                v->p0 = lsWrapPhase(v->p0 + GEVR_LS_TAU * f * dt);
                const float hissEnv = expf(-u * 6.5f);
                const float sweepEnv = sinf(lsClamp01(u) * 3.14159f);
                mono = (0.65f * nz * hissEnv + 0.55f * sinf(v->p0) * sweepEnv) * 0.24f;
            } else if (v->id == GEVR_LS_SFX_SLASH) {
                const float f = 110.0f + 160.0f * sinf(u * 3.14159f);
                v->p0 = lsWrapPhase(v->p0 + GEVR_LS_TAU * f * dt);
                const float env = sinf(u * 3.14159f);
                mono = (0.70f * sinf(v->p0) + 0.30f * sinf(v->p0 * 2.02f) + 0.18f * nz) * env * 0.16f;
            } else if (v->id == GEVR_LS_SFX_DEFLECT) {
                const float f = 2200.0f * expf(-u * 3.4f) + 260.0f;
                v->p0 = lsWrapPhase(v->p0 + GEVR_LS_TAU * f * dt);
                const float env = expf(-u * 5.2f);
                mono = (0.75f * sinf(v->p0) + 0.35f * nz * expf(-u * 11.0f)) * env * 0.22f;
            } else if (v->id == GEVR_LS_SFX_CLASH) {
                v->p0 = lsWrapPhase(v->p0 + GEVR_LS_TAU * 310.0f * dt);
                v->p1 = lsWrapPhase(v->p1 + GEVR_LS_TAU * 437.0f * dt);
                const float crackle = fabsf(nz) > 0.70f ? nz * 1.3f : nz * 0.35f;
                const float env = expf(-u * 4.5f);
                mono = (0.45f * sinf(v->p0 + 1.5f * sinf(v->p1)) + 0.65f * crackle) * env * 0.23f;
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
