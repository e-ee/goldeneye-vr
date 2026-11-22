#include "gevr_lasersword.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_palette_and_gestures(void)
{
    for (int c = 0; c < GEVR_LS_COLOR_COUNT; c++) {
        unsigned int core = 0, glow = 0;
        gevrLsPalette(c, &core, &glow);
        assert((core & 0xFFu) == 0xFFu);
        assert((glow & 0xFFu) > 0u);
    }

    GevrLsTrigger t;
    gevrLsTriggerReset(&t);
    assert(gevrLsTriggerUpdate(&t, 1, 0.016f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 1, 0.050f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 0, 0.016f) == GEVR_LS_TRIG_TAP);

    assert(gevrLsTriggerUpdate(&t, 0, 0.35f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 1, 0.016f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 0, 0.016f) == GEVR_LS_TRIG_TAP);

    assert(gevrLsTriggerUpdate(&t, 1, 0.05f) == GEVR_LS_TRIG_DOUBLE_TAP);
    assert(gevrLsTriggerUpdate(&t, 1, 0.30f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 0, 0.016f) == GEVR_LS_TRIG_NONE);

    gevrLsTriggerReset(&t);
    assert(gevrLsTriggerUpdate(&t, 1, 0.016f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 1, 0.10f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 1, 0.10f) == GEVR_LS_TRIG_NONE);
    assert(gevrLsTriggerUpdate(&t, 1, 0.05f) == GEVR_LS_TRIG_HOLD_START);
    assert(gevrLsTriggerUpdate(&t, 1, 0.05f) == GEVR_LS_TRIG_HOLD_ACTIVE);
    assert(gevrLsTriggerUpdate(&t, 0, 0.016f) == GEVR_LS_TRIG_HOLD_RELEASE);

    GevrLsSlash s = {0};
    assert(gevrLsSlashStart(&s, 1) == 0);
    assert(gevrLsSlashStart(&s, 0) == 1);
    assert(s.dir == 0);
    assert(gevrLsSlashStart(&s, 0) == 0);
    assert(gevrLsSlashStep(&s, 0.13f) > 0.45f);
    assert(gevrLsSlashStep(&s, 0.20f) == 1.0f);
    assert(gevrLsSlashStep(&s, 0.016f) < 0.0f);
    assert(gevrLsSlashStart(&s, 0) == 1);
    assert(s.dir == 1);
}

static void test_wrist_ignition_and_throw(void)
{
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    const float throwVel[3] = { 0.0f, 0.0f, -240.0f };
    const float slowVel[3] = { 0.0f, 0.0f, 100.0f };
    assert(gevrLsVrWristThrow(throwVel, fwd, 1) == 1);
    assert(gevrLsVrWristThrow(throwVel, fwd, 0) == 0);
    assert(gevrLsVrWristThrow(slowVel, fwd, 1) == 0);

    GevrLsIgnition ig = {0};
    float ext = gevrLsIgnitionUpdate(&ig, 1, 0.21f);
    assert(ext > 0.45f && ext < 0.55f);
    ext = gevrLsIgnitionUpdate(&ig, 1, 0.25f);
    assert(fabsf(ext - 1.0f) < 1e-5f);
    assert(gevrLsIgnitionUpdate(&ig, 0, 0.016f) == 0.0f);
    ext = gevrLsIgnitionUpdate(&ig, 1, 0.10f);
    assert(ext > 0.15f && ext < 0.35f);

    GevrLsThrown th;
    float owner[3] = { 0.0f, 150.0f, 0.0f };
    const float aim[3] = { 0.0f, 0.0f, -1.0f };
    gevrLsThrownLaunch(&th, owner, aim);
    assert(th.active && !th.returning);
    for (int i = 0; i < 200; i++) {
        int caught = gevrLsThrownStep(&th, owner, 0, 0.02f);
        if (i < 65) assert(!caught);
        if (th.returning) owner[0] = 200.0f;
        if (caught) break;
    }
    assert(!th.active);
    assert(fabsf(th.pos[0] - 200.0f) < 60.0f);

    gevrLsThrownLaunch(&th, owner, aim);
    assert(gevrLsThrownStep(&th, owner, 1, 0.02f) == 0);
    assert(th.returning == 1);
}

static void test_block_deflect_and_blade_hit(void)
{
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    const float frontTravel[3] = { 0.0f, 0.0f, 1.0f };
    const float rearTravel[3] = { 0.0f, 0.0f, -1.0f };
    const float zeroVec[3] = { 0.0f, 0.0f, 0.0f };
    float toShooter[3] = {0}, refl[3] = {0};

    assert(gevrLsBulletTravelToShooter(zeroVec, toShooter) == 0);
    assert(gevrLsBulletTravelToShooter(frontTravel, toShooter) == 1);
    assert(fabsf(toShooter[2] + 1.0f) < 1e-4f);
    assert(gevrLsCheckBlock(0, fwd, toShooter, refl) == GEVR_LS_BLOCK_NONE);
    assert(gevrLsCheckBlock(1, fwd, toShooter, refl) == GEVR_LS_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);

    assert(gevrLsBulletTravelToShooter(rearTravel, toShooter) == 1);
    assert(gevrLsCheckBlock(1, fwd, toShooter, refl) == GEVR_LS_BLOCK_NONE);

    const float bladeAcross[3] = { 0.0f, 1.0f, 0.0f };
    const float bladeInline[3] = { 0.0f, 0.0f, -1.0f };
    const float toFront[3] = { 0.0f, 0.0f, -1.0f };
    const float toRear[3] = { 0.0f, 0.0f, 1.0f };
    const float aimFwd[3] = { 0.0f, 0.0f, -1.0f };
    assert(gevrLsCheckVrBladeBlock(0, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_LS_BLOCK_NONE);
    assert(gevrLsCheckVrBladeBlock(1, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_LS_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrLsCheckVrBladeBlock(1, fwd, bladeInline, aimFwd, toFront, refl) == GEVR_LS_BLOCK_NONE);
    assert(gevrLsCheckVrBladeBlock(1, fwd, bladeAcross, aimFwd, toRear, refl) == GEVR_LS_BLOCK_NONE);

    const float cylBase[3] = { 0.0f, 0.0f, -100.0f };
    float hitPt[3] = {0};
    const float hSegA[3] = { -60.0f, 150.0f, -100.0f }, hSegB[3] = { 60.0f, 150.0f, -100.0f };
    assert(gevrLsSegmentHitCylinder(hSegA, hSegB, cylBase, 40.0f, 175.0f, hitPt) == 1);
    assert(fabsf(hitPt[0]) < 1e-3f && fabsf(hitPt[1] - 150.0f) < 1e-3f);
    const float vSegA[3] = { 10.0f, -50.0f, -100.0f }, vSegB[3] = { 10.0f, 250.0f, -100.0f };
    assert(gevrLsSegmentHitCylinder(vSegA, vSegB, cylBase, 40.0f, 175.0f, hitPt) == 1);
    assert(gevrLsSegmentHitCylinder((const float[3]){60,100,-100}, (const float[3]){120,100,-100}, cylBase, 40.0f, 175.0f, hitPt) == 0);
    assert(gevrLsSegmentHitCylinder((const float[3]){-60,220,-100}, (const float[3]){60,220,-100}, cylBase, 40.0f, 175.0f, hitPt) == 0);

    const float v[3] = { 10.0f, 0.0f, -10.0f }, n[3] = { -1.0f, 0.0f, 0.0f };
    float bounce[3] = {0};
    gevrLsReflectVec(v, n, bounce);
    assert(fabsf(bounce[0] + 10.0f) < 1e-4f && fabsf(bounce[2] + 10.0f) < 1e-4f);

    int rt = 0, lt = 0, mouse = 0;
    gevrLsInputFeedFlat(1, 0, 1);
    gevrLsInputReadFlat(&rt, &lt, &mouse);
    assert(rt == 1 && lt == 0 && mouse == 1);
}

static void test_procedural_audio(void)
{
    short buf[1024];
    gevrLsAudioReset();
    memset(buf, 0, sizeof(buf));
    gevrLsAudioMix(buf, 512, 22050);
    for (int i = 0; i < 1024; i++) assert(buf[i] == 0);

    gevrLsAudioSetState(1, 1.0f, 0.8f, 1);
    memset(buf, 0, sizeof(buf));
    gevrLsAudioMix(buf, 512, 22050);
    long long energy = 0;
    for (int i = 0; i < 1024; i++) {
        assert(buf[i] >= -32767 && buf[i] <= 32767);
        energy += abs(buf[i]);
    }
    assert(energy > 40000);

    for (int sfx = GEVR_LS_SFX_IGNITE; sfx <= GEVR_LS_SFX_CLASH; sfx++) {
        gevrLsAudioReset();
        gevrLsAudioTrigger(sfx, 1.0f);
        memset(buf, 0, sizeof(buf));
        gevrLsAudioMix(buf, 512, 22050);
        long long e = 0;
        for (int i = 0; i < 1024; i++) e += abs(buf[i]);
        assert(e > 10000);
        for (int pass = 0; pass < 30; pass++) {
            memset(buf, 0, sizeof(buf));
            gevrLsAudioMix(buf, 512, 22050);
        }
        memset(buf, 0, sizeof(buf));
        gevrLsAudioMix(buf, 512, 22050);
        for (int i = 0; i < 1024; i++) assert(buf[i] == 0);
    }
}

int main(void)
{
    test_palette_and_gestures();
    test_wrist_ignition_and_throw();
    test_block_deflect_and_blade_hit();
    test_procedural_audio();
    puts("lasersword core: all passed");
    return 0;
}
