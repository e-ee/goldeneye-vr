#include "gevr_lasersword.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GEVR_LS_SFX_VOICES     4
#define GEVR_LS_MAX_TRIS       4096
#define GEVR_LS_MAX_CLIP_SMPS  88200
#define GEVR_LS_MAX_PACK_BYTES (4L * 1024L * 1024L)

#ifdef __ANDROID__
#define LS_DIR "/sdcard/Android/data/com.gevr.port/files/"
#else
#define LS_DIR ""
#endif

int gevrIsQuestVrApk(void)
{
#if defined(__ANDROID__) && !defined(GEVR_FLAT) && !defined(GEVR_MOBILE) && !defined(GEVR_FRAME)
    return 1;
#else
    return 0;
#endif
}

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

/* -------------------------------------------------------------------------- */
/* Downloadable Asset Pack Loader (GEVRLS1\0: 3D hilt mesh + 5 PCM clips)     */
/* -------------------------------------------------------------------------- */

static const char *const s_lsPackFiles[] = {
    LS_DIR "mods/lasersword.bin",
    LS_DIR "texture-packs/lasersword/lasersword.bin",
};

static GevrLsPack s_pack;
static time_t s_nextStatSec;

static uint32_t lsRdU32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float lsRdF32(const unsigned char *p)
{
    uint32_t u = lsRdU32(p);
    float f = 0.0f;
    memcpy(&f, &u, 4);
    return f;
}

static void lsFreePack(GevrLsPack *pk)
{
    if (!pk) return;
    free(pk->verts);
    for (int i = 0; i < GEVR_LS_CLIP_COUNT; i++) {
        free(pk->clips[i]);
    }
    memset(pk, 0, sizeof(*pk));
}

void gevrLsPackUnload(void)
{
    lsFreePack(&s_pack);
    s_nextStatSec = 0;
    gevrLsAudioReset();
}

void gevrLsPackRefresh(void)
{
    if (!s_pack.loaded) s_nextStatSec = 0;
}

int gevrLsPackLoadFile(const char *path)
{
    if (!path || !*path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long size = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0 || size < 24L || size > GEVR_LS_MAX_PACK_BYTES) {
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
    GevrLsPack next;
    memset(&next, 0, sizeof(next));

    if (memcmp(p, "GEVRLS1\0", 8) != 0) goto bad;
    next.emitterOffsetCm = lsRdF32(p + 8);
    next.bladeRadiusCm = lsRdF32(p + 12);
    const uint32_t ntri = lsRdU32(p + 16);
    const uint32_t nclip = lsRdU32(p + 20);
    p += 24;

    if (!isfinite(next.emitterOffsetCm) || next.emitterOffsetCm < 2.0f || next.emitterOffsetCm > 40.0f) goto bad;
    if (!isfinite(next.bladeRadiusCm) || next.bladeRadiusCm < 0.4f || next.bladeRadiusCm > 8.0f) goto bad;
    if (ntri < 1u || ntri > GEVR_LS_MAX_TRIS || nclip != GEVR_LS_CLIP_COUNT) goto bad;

    const size_t nverts = (size_t)ntri * 3u;
    if (nverts * 16u > (size_t)(end - p)) goto bad;
    next.verts = (GevrLsVtx *)malloc(nverts * sizeof(GevrLsVtx));
    if (!next.verts) goto bad;
    next.triCount = (int)ntri;
    for (size_t i = 0; i < nverts; i++, p += 16) {
        for (int k = 0; k < 3; k++) {
            next.verts[i].p[k] = lsRdF32(p + (size_t)k * 4u);
            if (!isfinite(next.verts[i].p[k]) || fabsf(next.verts[i].p[k]) > 200.0f) goto bad;
        }
        memcpy(next.verts[i].rgba, p + 12, 4);
    }

    for (int c = 0; c < GEVR_LS_CLIP_COUNT; c++) {
        if ((size_t)(end - p) < 8u) goto bad;
        const uint32_t sr = lsRdU32(p);
        const uint32_t cnt = lsRdU32(p + 4);
        p += 8;
        if (sr < 8000u || sr > 48000u || cnt < 1u || cnt > GEVR_LS_MAX_CLIP_SMPS) goto bad;
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
    lsFreePack(&s_pack);
    next.loaded = 1;
    s_pack = next;
    return 1;

bad:
    free(raw);
    lsFreePack(&next);
    return 0;
}

static const GevrLsPack *lsEnsurePack(void)
{
    if (s_pack.loaded) return &s_pack;
    if (!gevrIsQuestVrApk()) return NULL;
    const time_t now = time(NULL);
    if (s_nextStatSec != 0 && now < s_nextStatSec) return NULL;
    s_nextStatSec = now + 2;
    for (unsigned i = 0; i < (unsigned)(sizeof(s_lsPackFiles) / sizeof(s_lsPackFiles[0])); i++) {
        if (gevrLsPackLoadFile(s_lsPackFiles[i])) return &s_pack;
    }
    return NULL;
}

int gevrLsAvailable(void)
{
    return gevrIsQuestVrApk() && lsEnsurePack() != NULL;
}

const GevrLsPack *gevrLsPackGet(void)
{
    return lsEnsurePack();
}

/* -------------------------------------------------------------------------- */
/* Pack Clip Stereo PCM Audio Mixer (22050 Hz)                                */
/* -------------------------------------------------------------------------- */

typedef struct {
    int active;
    int clip;
    float pos;
    float intensity;
} LsSfxVoice;

static struct {
    int active;
    float bladeExtend;
    float swingTarget;
    float swingSmooth;
    int thrownSpin;
    float humEnv;
    float humPos;
    LsSfxVoice voices[GEVR_LS_SFX_VOICES];
    int nextVoice;
} s_audio;

static float s_audioVol = 1.0f;

static float lsClamp01(float x)
{
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

void gevrLsAudioSetVolume(float volume)
{
    s_audioVol = lsClamp01(volume);
}

static float lsSampleClip(const short *smp, int len, float pos)
{
    if (!smp || len <= 0) return 0.0f;
    const int i0 = ((int)pos) % len;
    const int i1 = (i0 + 1) % len;
    const float frac = pos - (float)(int)pos;
    const float a = (float)smp[i0] * (1.0f / 32768.0f);
    const float b = (float)smp[i1] * (1.0f / 32768.0f);
    return a + (b - a) * frac;
}

void gevrLsAudioReset(void)
{
    memset(&s_audio, 0, sizeof(s_audio));
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
    v->active = 1;
    v->clip = eventId;
    v->pos = 0.0f;
    v->intensity = intensity;
}

void gevrLsAudioMix(short *pcmStereo, int frames, int sampleRate)
{
    if (!pcmStereo || frames <= 0 || sampleRate <= 0 || !s_pack.loaded || s_audioVol <= 0.001f) return;

    int anyVoice = 0;
    for (int i = 0; i < GEVR_LS_SFX_VOICES; i++) {
        if (s_audio.voices[i].active) { anyVoice = 1; break; }
    }
    if (!s_audio.active && !anyVoice && s_audio.humEnv < 1e-4f) return;

    const float dt = 1.0f / (float)sampleRate;
    const int humLen = s_pack.clipLen[GEVR_LS_CLIP_HUM];
    const float humStepBase = humLen > 0 ? (float)s_pack.clipRate[GEVR_LS_CLIP_HUM] * dt : 0.0f;

    for (int n = 0; n < frames; n++) {
        const float humTgt = s_audio.active ? fmaxf(s_audio.bladeExtend, s_audio.thrownSpin ? 0.85f : 0.0f) : 0.0f;
        s_audio.humEnv += (humTgt - s_audio.humEnv) * (dt * 14.0f);
        s_audio.swingSmooth += (s_audio.swingTarget - s_audio.swingSmooth) * (dt * 22.0f);

        float sig = 0.0f;
        if (s_audio.humEnv > 0.002f && humLen > 0) {
            const float pitch = 0.96f + 0.34f * s_audio.swingSmooth + (s_audio.thrownSpin ? 0.14f : 0.0f);
            s_audio.humPos += humStepBase * pitch;
            while (s_audio.humPos >= (float)humLen) s_audio.humPos -= (float)humLen;
            sig += lsSampleClip(s_pack.clips[GEVR_LS_CLIP_HUM], humLen, s_audio.humPos)
                 * s_audio.humEnv * (0.22f + 0.24f * s_audio.swingSmooth);
        }

        for (int i = 0; i < GEVR_LS_SFX_VOICES; i++) {
            LsSfxVoice *v = &s_audio.voices[i];
            if (!v->active) continue;
            const int c = v->clip;
            const int len = (c >= 0 && c < GEVR_LS_CLIP_COUNT) ? s_pack.clipLen[c] : 0;
            if (len <= 0 || v->pos >= (float)(len - 1)) {
                v->active = 0;
                continue;
            }
            sig += lsSampleClip(s_pack.clips[c], len, v->pos) * v->intensity * 0.42f;
            v->pos += (float)s_pack.clipRate[c] * dt;
        }
        sig *= s_audioVol;

        for (int ch = 0; ch < 2; ch++) {
            float out = (float)pcmStereo[n * 2 + ch] * (1.0f / 32768.0f) + sig;
            if (out > 1.0f) out = 1.0f - 0.1f * expf(-(out - 1.0f));
            else if (out < -1.0f) out = -1.0f + 0.1f * expf(out + 1.0f);
            const int s = (int)(out * 32767.0f);
            pcmStereo[n * 2 + ch] = (short)(s < -32767 ? -32767 : s > 32767 ? 32767 : s);
        }
    }
}
