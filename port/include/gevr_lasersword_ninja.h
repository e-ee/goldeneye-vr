#ifndef GEVR_LASERSWORD_NINJA_H
#define GEVR_LASERSWORD_NINJA_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lasersword and Ninja modes (#270): pure C11 math, gesture, projectile and physics
 * core, kept free of game-state headers and atan2f/acosf/asinf overrides
 * (see port/include/gevr_pull.h). Units are Perfect Dark / GoldenEye world
 * units (roughly centimetres), Y up.
 */

enum {
    GEVR_LN_OFF = 0,
    GEVR_LN_LASERSWORD = 1,
    GEVR_LN_NINJA = 2
};

enum {
    GEVR_LN_COLOR_BLUE = 0,
    GEVR_LN_COLOR_GREEN,
    GEVR_LN_COLOR_RED,
    GEVR_LN_COLOR_PURPLE,
    GEVR_LN_COLOR_YELLOW,
    GEVR_LN_COLOR_WHITE,
    GEVR_LN_COLOR_ORANGE,
    GEVR_LN_COLOR_GOLD,
    GEVR_LN_COLOR_COUNT
};

enum {
    GEVR_LN_SWORD_KATANA = 0,
    GEVR_LN_SWORD_GOLD_KATANA,
    GEVR_LN_SWORD_SHORT,
    GEVR_LN_SWORD_GOLD_SHORT,
    GEVR_LN_SWORD_DUAL_SHORT,
    GEVR_LN_SWORD_DUAL_GOLD_SHORT,
    GEVR_LN_SWORD_COUNT
};

enum {
    GEVR_LN_BOOM_SHURIKEN = 0,
    GEVR_LN_BOOM_WING,
    GEVR_LN_BOOM_GOLD_SHURIKEN,
    GEVR_LN_BOOM_GOLD_WING,
    GEVR_LN_BOOM_COUNT
};

/* Weapon-wheel slots in Lasersword mode */
enum {
    GEVR_LN_FORCE_TK = 0,        /* Force Grab / Choke / Pull / Push */
    GEVR_LN_FORCE_LIGHTNING,     /* Force Lightning */
    GEVR_LN_FORCE_THROW,         /* Lasersword Throw focus */
    GEVR_LN_FORCE_GUNS,          /* Normal GoldenEye guns */
    GEVR_LN_FORCE_COUNT
};

enum {
    GEVR_LN_STYLE_SINGLE = 0,
    GEVR_LN_STYLE_DOUBLE,
    GEVR_LN_STYLE_DUAL,
    GEVR_LN_STYLE_COUNT
};

/* Ninja weapon wheel slots */
enum {
    GEVR_LN_NINJA_SLOT_KATANA = 0,
    GEVR_LN_NINJA_SLOT_SHORT,
    GEVR_LN_NINJA_SLOT_DUAL,
    GEVR_LN_NINJA_SLOT_SHIELD,
    GEVR_LN_NINJA_SLOT_BOOMERANG,
    GEVR_LN_NINJA_SLOT_SHADOW,
    GEVR_LN_NINJA_SLOT_COUNT
};

enum {
    GEVR_LN_SCALE_NORMAL = 0,
    GEVR_LN_SCALE_RANDOM,
    GEVR_LN_SCALE_GIANT,
    GEVR_LN_SCALE_TINY,
    GEVR_LN_SCALE_COUNT
};

#define GEVR_LN_MAX_LIMBS          24
#define GEVR_LN_IGNITE_SEC         0.42f
#define GEVR_LN_CHOKE_DIST_CM      92.0f
#define GEVR_LN_PULL_RANGE_CM      1400.0f
#define GEVR_LN_GRAB_RANGE_CM      380.0f

/* Palette, weapon metrics and NPC scaling */
void gevrLnLaserswordPalette(int color, unsigned int *coreRgba, unsigned int *glowRgba);
float gevrLnWeaponReachCm(int mode, int sword);
int gevrLnIsDualSword(int mode, int sword);
int gevrLnIsGoldSword(int sword);
float gevrLnRivalScaleFactor(int setting, unsigned int seed);

/* Flat controller & mouse trigger gesture state machine */
enum {
    GEVR_LN_TRIG_NONE = 0,
    GEVR_LN_TRIG_TAP,
    GEVR_LN_TRIG_DOUBLE_TAP,
    GEVR_LN_TRIG_HOLD_START,
    GEVR_LN_TRIG_HOLD_ACTIVE,
    GEVR_LN_TRIG_HOLD_RELEASE
};

typedef struct GevrLnTrigger {
    int down;
    float heldSec;
    int holdFired;   /* 0 none, 1 hold active, 2 suppressed after double-tap */
    float tapTimer;
    int pendingTap;
} GevrLnTrigger;

void gevrLnTriggerReset(GevrLnTrigger *t);
int gevrLnTriggerUpdate(GevrLnTrigger *t, int pressed, float dt);

/* Alternating right-to-left / left-to-right melee slash */
typedef struct GevrLnSlash {
    int nextSide;    /* 0: right-to-left, 1: left-to-right */
    int active;
    int dir;
    float t;
    float duration;
} GevrLnSlash;

int gevrLnSlashStart(GevrLnSlash *s, int blocking);
float gevrLnSlashStep(GevrLnSlash *s, float dt);

/* VR wrist-snap gesture detection (units: cm/s, unit aimFwd) */
int gevrLnVrWristThrow(const float handVelCmS[3], const float aimFwd[3], int releasedFromHold);
int gevrLnVrWristPull(const float handVelCmS[3], const float aimFwd[3], int triggerHeld);

/* Lasersword ignition curve */
typedef struct GevrLnIgnition {
    float extend;    /* 0..1 */
    int ignitedOnce;
    int active;
} GevrLnIgnition;

float gevrLnIgnitionUpdate(GevrLnIgnition *ig, int drawn, int isFirstPerson, float dt);

/* Lasersword Throw & Ninja Boomerang flight */
typedef struct GevrLnThrown {
    int active;
    int isBoomerang;
    int returning;
    float pos[3];
    float vel[3];
    float dir[3];
    float startPos[3];
    float distTraveled;
    float maxRange;
    float speed;
    float spinAngle;
    float spinRate;
    float age;
    float maxAge;
} GevrLnThrown;

void gevrLnThrownLaunch(GevrLnThrown *th, int isBoomerang, const float origin[3], const float aimDir[3]);
int gevrLnThrownStep(GevrLnThrown *th, const float ownerPos[3], int hitWall, float dt);

/* Force Choke, 24-limb orbiting ball, and thrown-body energy/momentum */
void gevrLnChokeTargetPos(const float origin[3], const float fwd[3], float outPos[3]);
void gevrLnLimbOrbitPos(const float center[3], int slot, int count, float timeSec, float outPos[3]);
float gevrLnLimbBlastDamage(float distCm);

enum {
    GEVR_LN_IMPACT_NONE = 0,
    GEVR_LN_IMPACT_KNOCKDOWN,
    GEVR_LN_IMPACT_RAGDOLL_KILL,
    GEVR_LN_IMPACT_DISMEMBER
};

typedef struct GevrLnThrownBody {
    float massKg;
    float speedCmS;
    float energyJ;
    float travelCm;
    int dismemberedCount;
    int hitCount;
} GevrLnThrownBody;

void gevrLnThrownBodyInit(GevrLnThrownBody *tb, float massKg, float speedCmS);
int gevrLnThrownBodyImpact(GevrLnThrownBody *tb, float travelCm, float *outKnockSpeedCmS);

/* Bullet block & deflection cone check */
enum {
    GEVR_LN_BLOCK_NONE = 0,
    GEVR_LN_BLOCK_ABSORB,
    GEVR_LN_BLOCK_DEFLECT
};

int gevrLnCheckBlock(int mode, int blocking, int hasNinjaShield,
                     const float playerFwd[3], const float toShooterDir[3],
                     float outReflectDir[3]);
int gevrLnBulletTravelToShooter(const float bulletTravel[3], float outToShooter[3]);
int gevrLnCheckVrBladeBlock(int mode, int blocking, int hasNinjaShield,
                            const float playerFwd[3], const float bladeDirW[3],
                            const float handAimW[3], const float toShooterDir[3],
                            float outReflectDir[3]);
int gevrLnSegmentHitCylinder(const float segA[3], const float segB[3],
                             const float cylBase[3], float cylRadius, float cylHeight,
                             float outHitPt[3]);

/* Flat trigger input feed from port/src/input.c */
void gevrLnInputFeedFlat(int rtDown, int ltDown, int isMouse);
void gevrLnInputReadFlat(int *rtDown, int *ltDown, int *isMouse);

/* Vector reflection for corridor boomerang ricochets & directional deflection */
void gevrLnReflectVec(const float v[3], const float n[3], float out[3]);

/* Procedural DSP audio synth (22050 Hz stereo PCM mixer, zero external assets) */
enum {
    GEVR_LN_SFX_IGNITE = 1,
    GEVR_LN_SFX_SLASH_LASERSWORD,
    GEVR_LN_SFX_SLASH_STEEL,
    GEVR_LN_SFX_DEFLECT,
    GEVR_LN_SFX_CLASH_LASERSWORD,
    GEVR_LN_SFX_CLASH_METAL,
    GEVR_LN_SFX_FORCE_PUSH,
    GEVR_LN_SFX_SHADOW_STEP
};

void gevrLnAudioReset(void);
void gevrLnAudioSetState(int active, int isLasersword, float bladeExtend, float swingSpeed01,
                         int forceChokeOrPull, int forceLightning,
                         int thrownSpinning, int thrownIsBoomerang);
void gevrLnAudioTrigger(int eventId, float intensity);
void gevrLnAudioMix(short *pcmStereo, int frames, int sampleRate);

#ifdef __cplusplus
}
#endif

#endif
