#ifndef GEVR_LASERSWORD_H
#define GEVR_LASERSWORD_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lean Quest VR Lasersword melee weapon core: pure C11 math, gesture, throw,
 * deflection, 3D blade hit detection, and pack clip audio. Free of game-state
 * headers and atan2f/acosf/asinf overrides. Units are GoldenEye world units (~cm), Y up.
 */

/* Nonzero ONLY on the Quest VR APK build (excluded on flat/mobile/frame/desktop). */
int gevrIsQuestVrApk(void);

enum {
    GEVR_LS_COLOR_BLUE = 0,
    GEVR_LS_COLOR_GREEN,
    GEVR_LS_COLOR_RED,
    GEVR_LS_COLOR_PURPLE,
    GEVR_LS_COLOR_YELLOW,
    GEVR_LS_COLOR_WHITE,
    GEVR_LS_COLOR_ORANGE,
    GEVR_LS_COLOR_GOLD,
    GEVR_LS_COLOR_COUNT
};

#define GEVR_LS_IGNITE_SEC  0.42f
#define GEVR_LS_REACH_CM    115.0f

/* Palette */
void gevrLsPalette(int color, unsigned int *coreRgba, unsigned int *glowRgba);

/* VR wrist-snap throw detection (cm/s, unit aimFwd) */
int gevrLsVrWristThrow(const float handVelCmS[3], const float aimFwd[3], int releasedFromHold);

/* Blade ignition curve */
typedef struct GevrLsIgnition {
    float extend;    /* 0..1 */
    int active;
} GevrLsIgnition;

float gevrLsIgnitionUpdate(GevrLsIgnition *ig, int drawn, float dt);

/* Thrown spinning lasersword (1200 cm range, wall bounce, return to owner) */
typedef struct GevrLsThrown {
    int active;
    int returning;
    int hand;
    float pos[3];
    float vel[3];
    float dir[3];
    float distTraveled;
    float maxRange;
    float speed;
    float spinAngle;
    float spinRate;
    float age;
    float maxAge;
} GevrLsThrown;

void gevrLsThrownLaunch(GevrLsThrown *th, const float origin[3], const float aimDir[3]);
int gevrLsThrownStep(GevrLsThrown *th, const float ownerPos[3], int hitWall, float dt);

/* Bullet deflection & 3D blade hit geometry */
enum {
    GEVR_LS_BLOCK_NONE = 0,
    GEVR_LS_BLOCK_DEFLECT
};

int gevrLsBulletTravelToShooter(const float bulletTravel[3], float outToShooter[3]);
int gevrLsCheckVrBladeBlock(int blocking, const float playerFwd[3], const float bladeDirW[3],
                            const float handAimW[3], const float toShooterDir[3],
                            float outReflectDir[3]);
int gevrLsSegmentHitCylinder(const float segA[3], const float segB[3],
                             const float cylBase[3], float cylRadius, float cylHeight,
                             float outHitPt[3]);
void gevrLsReflectVec(const float v[3], const float n[3], float out[3]);

/* Downloadable mod pack (texture-packs/lasersword/lasersword.bin, GEVRLS1 format) */
enum {
    GEVR_LS_CLIP_HUM = 0,
    GEVR_LS_CLIP_IGNITE,
    GEVR_LS_CLIP_SLASH,
    GEVR_LS_CLIP_DEFLECT,
    GEVR_LS_CLIP_CLASH,
    GEVR_LS_CLIP_COUNT
};

typedef struct GevrLsVtx {
    float p[3];
    unsigned char rgba[4];
} GevrLsVtx;

typedef struct GevrLsPack {
    int loaded;
    float emitterOffsetCm;
    float bladeRadiusCm;
    int triCount;
    GevrLsVtx *verts;
    int clipLen[GEVR_LS_CLIP_COUNT];
    int clipRate[GEVR_LS_CLIP_COUNT];
    short *clips[GEVR_LS_CLIP_COUNT];
} GevrLsPack;

int gevrLsAvailable(void);
const GevrLsPack *gevrLsPackGet(void);
int gevrLsPackLoadFile(const char *path);
void gevrLsPackUnload(void);
void gevrLsPackRefresh(void);

/* 22050 Hz stereo PCM sample mixer reading from loaded pack clips */
enum {
    GEVR_LS_SFX_IGNITE = 1,
    GEVR_LS_SFX_SLASH,
    GEVR_LS_SFX_DEFLECT,
    GEVR_LS_SFX_CLASH
};

void gevrLsAudioReset(void);
void gevrLsAudioSetVolume(float volume);
void gevrLsAudioSetState(int active, float bladeExtend, float swingSpeed01, int thrownSpinning);
void gevrLsAudioTrigger(int eventId, float intensity);
void gevrLsAudioMix(short *pcmStereo, int frames, int sampleRate);

#ifdef __cplusplus
}
#endif

#endif
