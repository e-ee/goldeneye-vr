#include "gevr_lasersword.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_palette_and_vr_core(void)
{
    assert(gevrIsQuestVrApk() == 0);
    assert(gevrLsAvailable() == 0);

    for (int c = 0; c < GEVR_LS_COLOR_COUNT; c++) {
        unsigned int core = 0, glow = 0;
        gevrLsPalette(c, &core, &glow);
        assert((core & 0xFFu) == 0xFFu);
        assert((glow & 0xFFu) > 0u);
    }
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
    assert(gevrLsBulletTravelToShooter(rearTravel, toShooter) == 1);
    assert(fabsf(toShooter[2] - 1.0f) < 1e-4f);

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
}

static void test_pack_and_audio(const char *packPath)
{
    short buf[1024];

    gevrLsPackUnload();
    gevrLsAudioReset();
    assert(gevrLsPackGet() == NULL);
    gevrLsAudioSetState(1, 1.0f, 0.8f, 1);
    gevrLsAudioTrigger(GEVR_LS_SFX_IGNITE, 1.0f);
    memset(buf, 0, sizeof(buf));
    gevrLsAudioMix(buf, 512, 22050);
    for (int i = 0; i < 1024; i++) assert(buf[i] == 0);

    assert(gevrLsPackLoadFile(packPath) == 1);
    /* Non-Quest hosts load explicitly via gevrLsPackLoadFile; gevrLsAvailable stays Quest-only */
    assert(gevrLsAvailable() == 0);
    const GevrLsPack *pk = gevrLsPackGet();
    assert(pk && pk->loaded && pk->triCount >= 100 && pk->verts != NULL);
    assert(pk->emitterOffsetCm > 10.0f && pk->bladeRadiusCm > 0.5f);
    for (int c = 0; c < GEVR_LS_CLIP_COUNT; c++) {
        assert(pk->clips[c] != NULL && pk->clipLen[c] > 1000 && pk->clipRate[c] == 22050);
    }

    gevrLsAudioReset();
    gevrLsAudioSetState(1, 1.0f, 0.8f, 1);
    memset(buf, 0, sizeof(buf));
    gevrLsAudioMix(buf, 512, 22050);
    long long energy = 0;
    for (int i = 0; i < 1024; i++) {
        assert(buf[i] >= -32767 && buf[i] <= 32767);
        energy += abs(buf[i]);
    }
    assert(energy > 40000);

    gevrLsAudioSetVolume(0.0f);
    memset(buf, 0, sizeof(buf));
    gevrLsAudioMix(buf, 512, 22050);
    for (int i = 0; i < 1024; i++) assert(buf[i] == 0);
    gevrLsAudioSetVolume(1.0f);
    memset(buf, 0, sizeof(buf));
    gevrLsAudioMix(buf, 512, 22050);
    energy = 0;
    for (int i = 0; i < 1024; i++) energy += abs(buf[i]);
    assert(energy > 40000);

    for (int sfx = GEVR_LS_SFX_IGNITE; sfx <= GEVR_LS_SFX_CLASH; sfx++) {
        gevrLsAudioReset();
        gevrLsAudioTrigger(sfx, 1.0f);
        memset(buf, 0, sizeof(buf));
        gevrLsAudioMix(buf, 512, 22050);
        long long e = 0;
        for (int i = 0; i < 1024; i++) e += abs(buf[i]);
        assert(e > 10000);
        for (int pass = 0; pass < 35; pass++) {
            memset(buf, 0, sizeof(buf));
            gevrLsAudioMix(buf, 512, 22050);
        }
        memset(buf, 0, sizeof(buf));
        gevrLsAudioMix(buf, 512, 22050);
        for (int i = 0; i < 1024; i++) assert(buf[i] == 0);
    }

    assert(gevrLsPackLoadFile("no-such-lasersword-pack.bin") == 0);
    assert(gevrLsPackGet() == pk);
    gevrLsPackUnload();
}

int main(int argc, char **argv)
{
    assert(argc >= 2);
    test_palette_and_vr_core();
    test_wrist_ignition_and_throw();
    test_block_deflect_and_blade_hit();
    test_pack_and_audio(argv[1]);
    puts("lasersword core: all passed");
    return 0;
}
