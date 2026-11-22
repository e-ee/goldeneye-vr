#include "gevr_lasersword_ninja.h"
#include "gevr_lasersword.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GEVR_LN_SFX_VOICES     6
#define GEVR_LN_MAX_TRIS_MESH  1024u
#define GEVR_LN_MAX_CLIP_SMPS  88200u
#define GEVR_LN_MAX_PACK_BYTES (4L * 1024L * 1024L)

#ifdef __ANDROID__
#define LN_DIR "/sdcard/Android/data/com.gevr.port/files/"
#else
#define LN_DIR ""
#endif

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

    const float toOwner[3] = {
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

void gevrLnReflectVec(const float v[3], const float n[3], float out[3])
{
    gevrLsReflectVec(v, n, out);
}

/* -------------------------------------------------------------------------- */
/* Downloadable Asset Pack Loader (GEVRLN1\0: 7 3D meshes + 12 PCM clips)     */
/* -------------------------------------------------------------------------- */

static const char *const s_lnPackFiles[] = {
    LN_DIR "mods/lasersword_ninja.bin",
    LN_DIR "texture-packs/lasersword-ninja/lasersword_ninja.bin",
};

static GevrLnPack s_lnPack;
static time_t s_lnNextStatSec;

static uint32_t lnRdU32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float lnRdF32(const unsigned char *p)
{
    uint32_t u = lnRdU32(p);
    float f = 0.0f;
    memcpy(&f, &u, 4);
    return f;
}

static void lnFreePack(GevrLnPack *pk)
{
    if (!pk) return;
    for (int i = 0; i < GEVR_LN_MESH_COUNT; i++) {
        free(pk->meshes[i].verts);
    }
    for (int i = 0; i < GEVR_LN_CLIP_COUNT; i++) {
        free(pk->clips[i]);
    }
    memset(pk, 0, sizeof(*pk));
}

void gevrLnPackUnload(void)
{
    lnFreePack(&s_lnPack);
    s_lnNextStatSec = 0;
}

int gevrLnPackLoadFile(const char *path)
{
    if (!path || !*path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long size = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0 || size < 16L || size > GEVR_LN_MAX_PACK_BYTES) {
        fclose(f);
        return 0;
    }
    unsigned char *raw = (unsigned char *)malloc((size_t)size);
    if (!raw || fread(raw, 1, (size_t)size, f) != (size_t)size) {
        free(raw);
        fclose(f);
        return 0;
    }
    fclose(f);

    const unsigned char *p = raw;
    const unsigned char *end = raw + size;
    GevrLnPack next;
    memset(&next, 0, sizeof(next));

    if (memcmp(p, "GEVRLN1\0", 8) != 0) goto bad;
    const uint32_t nmesh = lnRdU32(p + 8);
    const uint32_t nclip = lnRdU32(p + 12);
    p += 16;
    if (nmesh != GEVR_LN_MESH_COUNT || nclip != GEVR_LN_CLIP_COUNT) goto bad;

    for (int m = 0; m < GEVR_LN_MESH_COUNT; m++) {
        if ((size_t)(end - p) < 12u) goto bad;
        next.meshes[m].emitterOffsetCm = lnRdF32(p);
        next.meshes[m].bladeRadiusCm = lnRdF32(p + 4);
        const uint32_t ntri = lnRdU32(p + 8);
        p += 12;
        if (!isfinite(next.meshes[m].emitterOffsetCm) || fabsf(next.meshes[m].emitterOffsetCm) > 100.0f) goto bad;
        if (!isfinite(next.meshes[m].bladeRadiusCm) || next.meshes[m].bladeRadiusCm < 0.0f || next.meshes[m].bladeRadiusCm > 50.0f) goto bad;
        if (ntri < 1u || ntri > GEVR_LN_MAX_TRIS_MESH) goto bad;

        const size_t nverts = (size_t)ntri * 3u;
        if (nverts * 16u > (size_t)(end - p)) goto bad;
        next.meshes[m].verts = (GevrLnVtx *)malloc(nverts * sizeof(GevrLnVtx));
        if (!next.meshes[m].verts) goto bad;
        next.meshes[m].triCount = (int)ntri;
        for (size_t i = 0; i < nverts; i++, p += 16) {
            for (int k = 0; k < 3; k++) {
                next.meshes[m].verts[i].p[k] = lnRdF32(p + (size_t)k * 4u);
                if (!isfinite(next.meshes[m].verts[i].p[k]) || fabsf(next.meshes[m].verts[i].p[k]) > 250.0f) goto bad;
            }
            memcpy(next.meshes[m].verts[i].rgba, p + 12, 4);
        }
    }

    for (int c = 0; c < GEVR_LN_CLIP_COUNT; c++) {
        if ((size_t)(end - p) < 8u) goto bad;
        const uint32_t sr = lnRdU32(p);
        const uint32_t cnt = lnRdU32(p + 4);
        p += 8;
        if (sr < 8000u || sr > 48000u || cnt < 1u || cnt > GEVR_LN_MAX_CLIP_SMPS) goto bad;
        if ((size_t)cnt * 2u > (size_t)(end - p)) goto bad;
        next.clips[c] = (short *)malloc((size_t)cnt * sizeof(short));
        if (!next.clips[c]) goto bad;
        next.clipRate[c] = (int)sr;
        next.clipLen[c] = (int)cnt;
        for (uint32_t i = 0; i < cnt; i++, p += 2) {
            const uint16_t u = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            next.clips[c][i] = (short)(int16_t)u;
        }
    }

    free(raw);
    lnFreePack(&s_lnPack);
    next.loaded = 1;
    s_lnPack = next;
    return 1;

bad:
    free(raw);
    lnFreePack(&next);
    return 0;
}

static const GevrLnPack *lnEnsurePack(void)
{
    if (s_lnPack.loaded) return &s_lnPack;
    if (!gevrIsQuestVrApk()) return NULL;
    const time_t now = time(NULL);
    if (s_lnNextStatSec != 0 && now < s_lnNextStatSec) return NULL;
    s_lnNextStatSec = now + 2;
    for (unsigned i = 0; i < (unsigned)(sizeof(s_lnPackFiles) / sizeof(s_lnPackFiles[0])); i++) {
        if (gevrLnPackLoadFile(s_lnPackFiles[i])) return &s_lnPack;
    }
    return NULL;
}

int gevrLnAvailable(void)
{
    return gevrIsQuestVrApk() && lnEnsurePack() != NULL;
}

const GevrLnPack *gevrLnPackGet(void)
{
    return lnEnsurePack();
}

/* -------------------------------------------------------------------------- */
/* Pack Clip Stereo PCM Audio Mixer (22050 Hz mono clips -> stereo)           */
/* -------------------------------------------------------------------------- */

typedef struct {
    int active;
    int clip;
    float pos;
    float intensity;
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
    float humPos;
    float forcePos;
    float lightPos;
    float thrownPos;
    float spinPanPhase;
    LnSfxVoice voices[GEVR_LN_SFX_VOICES];
    int nextVoice;
} s_audio;

static float lnClamp01(float x)
{
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float lnSampleClip(const short *smp, int len, float pos)
{
    if (!smp || len <= 0) return 0.0f;
    const int i0 = ((int)pos) % len;
    const int i1 = (i0 + 1) % len;
    const float frac = pos - (float)(int)pos;
    const float a = (float)smp[i0] * (1.0f / 32768.0f);
    const float b = (float)smp[i1] * (1.0f / 32768.0f);
    return a + (b - a) * frac;
}

static float lnStepLoop(int clip, float *pos, float pitch, float dt)
{
    const int len = s_lnPack.clipLen[clip];
    if (len <= 0) return 0.0f;
    *pos += (float)s_lnPack.clipRate[clip] * pitch * dt;
    while (*pos >= (float)len) *pos -= (float)len;
    return lnSampleClip(s_lnPack.clips[clip], len, *pos);
}

void gevrLnAudioReset(void)
{
    memset(&s_audio, 0, sizeof(s_audio));
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
    v->active = 1;
    v->clip = 3 + eventId; /* eventId 1..8 -> clip 4..11 */
    v->pos = 0.0f;
    v->intensity = intensity;
}

void gevrLnAudioMix(short *pcmStereo, int frames, int sampleRate)
{
    if (!pcmStereo || frames <= 0 || sampleRate <= 0 || !s_lnPack.loaded) return;

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

        if (s_audio.humEnv > 0.002f) {
            const float pitch = 0.96f + 0.34f * s_audio.swingSmooth;
            const float hum = lnStepLoop(GEVR_LN_CLIP_HUM, &s_audio.humPos, pitch, dt);
            const float amp = s_audio.humEnv * (0.18f + 0.22f * s_audio.swingSmooth);
            sigL += hum * amp;
            sigR += hum * amp;
        }

        if (s_audio.forceEnv > 0.002f) {
            const float rmb = lnStepLoop(GEVR_LN_CLIP_FORCE_RUMBLE, &s_audio.forcePos, 1.0f, dt);
            const float amp = s_audio.forceEnv * 0.24f;
            sigL += rmb * amp;
            sigR += rmb * amp;
        }

        if (s_audio.lightEnv > 0.002f) {
            const float arc = lnStepLoop(GEVR_LN_CLIP_LIGHTNING, &s_audio.lightPos, 1.0f, dt);
            const float amp = s_audio.lightEnv * 0.24f;
            sigL += arc * amp;
            sigR += arc * amp;
        }

        if (s_audio.thrownEnv > 0.002f) {
            const float spinHz = s_audio.thrownBoom ? 11.5f : 14.0f;
            s_audio.spinPanPhase += 6.2831853f * spinHz * dt;
            if (s_audio.spinPanPhase >= 6.2831853f) s_audio.spinPanPhase -= 6.2831853f;
            const float whoosh = lnStepLoop(GEVR_LN_CLIP_SPIN, &s_audio.thrownPos, s_audio.thrownBoom ? 0.94f : 1.12f, dt);
            const float amp = s_audio.thrownEnv * 0.24f;
            const float pan = 0.38f * cosf(s_audio.spinPanPhase);
            sigL += whoosh * amp * (1.0f - pan);
            sigR += whoosh * amp * (1.0f + pan);
        }

        for (int i = 0; i < GEVR_LN_SFX_VOICES; i++) {
            LnSfxVoice *v = &s_audio.voices[i];
            if (!v->active) continue;
            const int c = v->clip;
            const int len = (c >= 0 && c < GEVR_LN_CLIP_COUNT) ? s_lnPack.clipLen[c] : 0;
            if (len <= 0 || v->pos >= (float)(len - 1)) {
                v->active = 0;
                continue;
            }
            const float s = lnSampleClip(s_lnPack.clips[c], len, v->pos) * v->intensity * 0.42f;
            sigL += s;
            sigR += s;
            v->pos += (float)s_lnPack.clipRate[c] * dt;
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
