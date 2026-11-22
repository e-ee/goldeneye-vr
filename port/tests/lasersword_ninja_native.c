#include "gevr_lasersword_ninja.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_palette_and_metrics(void)
{
    for (int c = 0; c < GEVR_LN_COLOR_COUNT; c++) {
        unsigned int core = 0, glow = 0;
        gevrLnLaserswordPalette(c, &core, &glow);
        assert((core & 0xFFu) == 0xFFu);
        assert((glow & 0xFFu) > 0u);
    }
    assert(gevrLnWeaponReachCm(GEVR_LN_LASERSWORD, 0) > gevrLnWeaponReachCm(GEVR_LN_NINJA, GEVR_LN_SWORD_SHORT));
    assert(gevrLnWeaponReachCm(GEVR_LN_NINJA, GEVR_LN_SWORD_KATANA) > gevrLnWeaponReachCm(GEVR_LN_NINJA, GEVR_LN_SWORD_SHORT));
    assert(gevrLnIsDualSword(GEVR_LN_NINJA, GEVR_LN_SWORD_DUAL_SHORT));
    assert(gevrLnIsDualSword(GEVR_LN_NINJA, GEVR_LN_SWORD_DUAL_GOLD_SHORT));
    assert(!gevrLnIsDualSword(GEVR_LN_NINJA, GEVR_LN_SWORD_KATANA));
    assert(GEVR_LN_STYLE_COUNT == 3);
    assert(GEVR_LN_NINJA_SLOT_SHADOW < GEVR_LN_NINJA_SLOT_COUNT);
    assert(GEVR_LN_NINJA_SLOT_COUNT == 6);
    assert(gevrLnIsGoldSword(GEVR_LN_SWORD_GOLD_KATANA));
    assert(gevrLnIsGoldSword(GEVR_LN_SWORD_GOLD_SHORT));
    assert(gevrLnIsGoldSword(GEVR_LN_SWORD_DUAL_GOLD_SHORT));
    assert(!gevrLnIsGoldSword(GEVR_LN_SWORD_SHORT));

    assert(fabsf(gevrLnRivalScaleFactor(GEVR_LN_SCALE_NORMAL, 1) - 1.0f) < 1e-4f);
    assert(gevrLnRivalScaleFactor(GEVR_LN_SCALE_GIANT, 1) > 1.2f);
    assert(gevrLnRivalScaleFactor(GEVR_LN_SCALE_TINY, 1) < 0.7f);
    int seenTiny = 0, seenNormal = 0, seenGiant = 0;
    for (unsigned int s = 0; s < 64; s++) {
        float sc = gevrLnRivalScaleFactor(GEVR_LN_SCALE_RANDOM, s);
        if (sc < 0.7f) seenTiny = 1;
        else if (sc > 1.2f) seenGiant = 1;
        else seenNormal = 1;
    }
    assert(seenTiny && seenNormal && seenGiant);
}

static void test_triggers_and_slash(void)
{
    GevrLnTrigger t;
    gevrLnTriggerReset(&t);

    /* Single quick tap emits TAP immediately on release */
    assert(gevrLnTriggerUpdate(&t, 1, 0.016f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 1, 0.050f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 0, 0.016f) == GEVR_LN_TRIG_TAP);

    /* Wait past double-tap window -> fresh press does not double-tap */
    assert(gevrLnTriggerUpdate(&t, 0, 0.35f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 1, 0.016f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 0, 0.016f) == GEVR_LN_TRIG_TAP);

    /* Second quick press within 0.28s emits DOUBLE_TAP, release emits NONE */
    assert(gevrLnTriggerUpdate(&t, 1, 0.05f) == GEVR_LN_TRIG_DOUBLE_TAP);
    assert(gevrLnTriggerUpdate(&t, 1, 0.30f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 0, 0.016f) == GEVR_LN_TRIG_NONE);

    /* Hold >= 0.22s emits HOLD_START, HOLD_ACTIVE, then HOLD_RELEASE */
    gevrLnTriggerReset(&t);
    assert(gevrLnTriggerUpdate(&t, 1, 0.016f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 1, 0.10f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 1, 0.10f) == GEVR_LN_TRIG_NONE);
    assert(gevrLnTriggerUpdate(&t, 1, 0.05f) == GEVR_LN_TRIG_HOLD_START);
    assert(gevrLnTriggerUpdate(&t, 1, 0.05f) == GEVR_LN_TRIG_HOLD_ACTIVE);
    assert(gevrLnTriggerUpdate(&t, 0, 0.016f) == GEVR_LN_TRIG_HOLD_RELEASE);

    /* Alternating slash: blocked while blocking=1, alternates R->L / L->R otherwise */
    GevrLnSlash s = {0};
    assert(gevrLnSlashStart(&s, 1) == 0);
    assert(gevrLnSlashStart(&s, 0) == 1);
    assert(s.dir == 0);
    assert(gevrLnSlashStart(&s, 0) == 0); /* cannot start while active */
    assert(gevrLnSlashStep(&s, 0.13f) > 0.45f);
    assert(gevrLnSlashStep(&s, 0.20f) == 1.0f);
    assert(gevrLnSlashStep(&s, 0.016f) < 0.0f);
    assert(gevrLnSlashStart(&s, 0) == 1);
    assert(s.dir == 1);
}

static void test_vr_wrist_and_ignition(void)
{
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    const float throwVel[3] = { 0.0f, 0.0f, -240.0f };
    const float pullVel[3] = { 0.0f, 0.0f, 200.0f };
    assert(gevrLnVrWristThrow(throwVel, fwd, 1) == 1);
    assert(gevrLnVrWristThrow(throwVel, fwd, 0) == 0);
    assert(gevrLnVrWristThrow(pullVel, fwd, 1) == 0);
    assert(gevrLnVrWristPull(pullVel, fwd, 1) == 1);
    assert(gevrLnVrWristPull(pullVel, fwd, 0) == 0);

    /* 1P ignition animates once, subsequent 1P re-equips snap to 1.0 */
    GevrLnIgnition fp = {0};
    float ext = gevrLnIgnitionUpdate(&fp, 1, 1, 0.21f);
    assert(ext > 0.45f && ext < 0.55f);
    ext = gevrLnIgnitionUpdate(&fp, 1, 1, 0.25f);
    assert(fabsf(ext - 1.0f) < 1e-5f);
    assert(gevrLnIgnitionUpdate(&fp, 0, 1, 0.016f) == 0.0f);
    assert(gevrLnIgnitionUpdate(&fp, 1, 1, 0.016f) == 1.0f);

    /* NPC / MP ignition replays every draw */
    GevrLnIgnition npc = {0};
    assert(gevrLnIgnitionUpdate(&npc, 1, 0, 0.21f) < 0.6f);
    assert(gevrLnIgnitionUpdate(&npc, 1, 0, 0.25f) == 1.0f);
    assert(gevrLnIgnitionUpdate(&npc, 0, 0, 0.016f) == 0.0f);
    assert(gevrLnIgnitionUpdate(&npc, 1, 0, 0.10f) < 0.35f);
}

static void test_thrown_lasersword_and_boomerang(void)
{
    GevrLnThrown th;
    float owner[3] = { 0.0f, 150.0f, 0.0f };
    const float aim[3] = { 0.0f, 0.0f, -1.0f };

    /* Lasersword: straight out up to 1200 cm, steers back to moved owner */
    gevrLnThrownLaunch(&th, 0, owner, aim);
    assert(th.active && !th.returning);
    for (int i = 0; i < 200; i++) {
        int caught = gevrLnThrownStep(&th, owner, 0, 0.02f);
        if (i < 65) assert(!caught);
        if (th.returning) {
            owner[0] = 200.0f; /* strafe right while returning */
        }
        if (caught) break;
    }
    assert(!th.active);
    assert(fabsf(th.pos[0] - 200.0f) < 60.0f);

    /* Wall bounce turns immediately into returning */
    gevrLnThrownLaunch(&th, 0, owner, aim);
    assert(gevrLnThrownStep(&th, owner, 1, 0.02f) == 0);
    assert(th.returning == 1);

    /* Boomerang reaches ~1400 cm and returns cleanly */
    owner[0] = 0.0f;
    gevrLnThrownLaunch(&th, 1, owner, aim);
    float maxZ = 0.0f;
    int caught = 0;
    for (int i = 0; i < 300 && !caught; i++) {
        caught = gevrLnThrownStep(&th, owner, 0, 0.02f);
        if (-th.pos[2] > maxZ) maxZ = -th.pos[2];
    }
    assert(caught && maxZ >= 1350.0f);
}

static void test_choke_limbs_and_thrown_body(void)
{
    const float origin[3] = { 10.0f, 150.0f, 20.0f };
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    float choke[3];
    gevrLnChokeTargetPos(origin, fwd, choke);
    assert(fabsf(choke[0] - 10.0f) < 1e-3f);
    assert(fabsf(choke[1] - 165.0f) < 1e-3f);
    assert(fabsf(choke[2] - (20.0f - GEVR_LN_CHOKE_DIST_CM)) < 1e-3f);

    /* 24 limbs orbit cleanly without NaN or duplicates */
    float pts[GEVR_LN_MAX_LIMBS][3];
    for (int i = 0; i < GEVR_LN_MAX_LIMBS; i++) {
        gevrLnLimbOrbitPos(choke, i, GEVR_LN_MAX_LIMBS, 0.5f, pts[i]);
        for (int k = 0; k < 3; k++) assert(isfinite(pts[i][k]));
        float dx = pts[i][0] - choke[0], dy = pts[i][1] - choke[1], dz = pts[i][2] - choke[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        assert(d >= 18.0f && d <= 45.0f);
        for (int j = 0; j < i; j++) {
            float ddx = pts[i][0] - pts[j][0];
            float ddy = pts[i][1] - pts[j][1];
            float ddz = pts[i][2] - pts[j][2];
            assert(ddx * ddx + ddy * ddy + ddz * ddz > 1.0f);
        }
    }
    assert(gevrLnLimbBlastDamage(150.0f) >= 100.0f);
    assert(gevrLnLimbBlastDamage(500.0f) > gevrLnLimbBlastDamage(1000.0f));
    assert(gevrLnLimbBlastDamage(1300.0f) == 0.0f);

    /* Thrown body: dismembers at most 2 close NPCs, then ragdoll-kills/knocks down */
    GevrLnThrownBody tb;
    float knock = 0.0f;
    gevrLnThrownBodyInit(&tb, 80.0f, 1600.0f);
    assert(gevrLnThrownBodyImpact(&tb, 100.0f, &knock) == GEVR_LN_IMPACT_DISMEMBER);
    assert(knock > 500.0f);
    assert(gevrLnThrownBodyImpact(&tb, 150.0f, &knock) == GEVR_LN_IMPACT_DISMEMBER);
    assert(gevrLnThrownBodyImpact(&tb, 180.0f, &knock) == GEVR_LN_IMPACT_RAGDOLL_KILL);
    assert(tb.dismemberedCount == 2);

    /* Thrown body past arm's reach does not dismember */
    gevrLnThrownBodyInit(&tb, 80.0f, 1600.0f);
    assert(gevrLnThrownBodyImpact(&tb, 450.0f, &knock) == GEVR_LN_IMPACT_RAGDOLL_KILL);
}

static void test_block_and_deflect(void)
{
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    const float shooterFront[3] = { 0.1f, 0.0f, -0.99f };
    const float shooterBack[3] = { 0.0f, 0.0f, 1.0f };
    float refl[3] = {0};

    assert(gevrLnCheckBlock(GEVR_LN_LASERSWORD, 0, 0, fwd, shooterFront, refl) == GEVR_LN_BLOCK_NONE);
    assert(gevrLnCheckBlock(GEVR_LN_LASERSWORD, 1, 0, fwd, shooterBack, refl) == GEVR_LN_BLOCK_NONE);
    assert(gevrLnCheckBlock(GEVR_LN_LASERSWORD, 1, 0, fwd, shooterFront, refl) == GEVR_LN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrLnCheckBlock(GEVR_LN_NINJA, 1, 0, fwd, shooterFront, refl) == GEVR_LN_BLOCK_ABSORB);
    assert(gevrLnCheckBlock(GEVR_LN_NINJA, 1, 1, fwd, shooterFront, refl) == GEVR_LN_BLOCK_DEFLECT);

    int rt = 0, lt = 0, mouse = 0;
    gevrLnInputFeedFlat(1, 0, 1);
    gevrLnInputReadFlat(&rt, &lt, &mouse);
    assert(rt == 1 && lt == 0 && mouse == 1);

    /* Corridor boomerang reflection */
    const float v[3] = { 10.0f, 0.0f, -10.0f };
    const float wallN[3] = { -1.0f, 0.0f, 0.0f };
    float bounce[3] = {0};
    gevrLnReflectVec(v, wallN, bounce);
    assert(fabsf(bounce[0] + 10.0f) < 1e-4f);
    assert(fabsf(bounce[1]) < 1e-4f);
    assert(fabsf(bounce[2] + 10.0f) < 1e-4f);

    /* Bullet travel vector (shooter -> player) must invert to shooter direction */
    const float zeroVec[3] = { 0.0f, 0.0f, 0.0f };
    const float frontTravel[3] = { 0.0f, 0.0f, 1.0f };
    const float rearTravel[3] = { 0.0f, 0.0f, -1.0f };
    float toShooter[3] = {0};
    assert(gevrLnBulletTravelToShooter(zeroVec, toShooter) == 0);
    assert(gevrLnBulletTravelToShooter(frontTravel, toShooter) == 1);
    assert(fabsf(toShooter[0]) < 1e-4f && fabsf(toShooter[1]) < 1e-4f && fabsf(toShooter[2] + 1.0f) < 1e-4f);
    assert(gevrLnCheckBlock(GEVR_LN_LASERSWORD, 1, 0, fwd, toShooter, refl) == GEVR_LN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrLnBulletTravelToShooter(rearTravel, toShooter) == 1);
    assert(fabsf(toShooter[0]) < 1e-4f && fabsf(toShooter[1]) < 1e-4f && fabsf(toShooter[2] - 1.0f) < 1e-4f);
    assert(gevrLnCheckBlock(GEVR_LN_LASERSWORD, 1, 0, fwd, toShooter, refl) == GEVR_LN_BLOCK_NONE);

    /* Tracked 3D VR blade / shield orientation blocking */
    const float bladeAcross[3] = { 0.0f, 1.0f, 0.0f };
    const float bladeInline[3] = { 0.0f, 0.0f, -1.0f };
    const float toFront[3] = { 0.0f, 0.0f, -1.0f };
    const float toRear[3] = { 0.0f, 0.0f, 1.0f };
    const float aimFwd[3] = { 0.0f, 0.0f, -1.0f };
    const float shieldAim[3] = { 0.2f, 0.0f, -0.98f };
    const float shieldSide[3] = { 1.0f, 0.0f, 0.0f };
    assert(gevrLnCheckVrBladeBlock(GEVR_LN_LASERSWORD, 0, 0, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_LN_BLOCK_NONE);
    assert(gevrLnCheckVrBladeBlock(GEVR_LN_LASERSWORD, 1, 0, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_LN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrLnCheckVrBladeBlock(GEVR_LN_LASERSWORD, 1, 0, fwd, bladeInline, aimFwd, toFront, refl) == GEVR_LN_BLOCK_NONE);
    assert(gevrLnCheckVrBladeBlock(GEVR_LN_LASERSWORD, 1, 0, fwd, bladeAcross, aimFwd, toRear, refl) == GEVR_LN_BLOCK_NONE);
    assert(gevrLnCheckVrBladeBlock(GEVR_LN_NINJA, 1, 1, fwd, bladeAcross, shieldAim, toFront, refl) == GEVR_LN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrLnCheckVrBladeBlock(GEVR_LN_NINJA, 1, 1, fwd, bladeAcross, shieldSide, toFront, refl) == GEVR_LN_BLOCK_NONE);
    assert(gevrLnCheckVrBladeBlock(GEVR_LN_NINJA, 1, 0, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_LN_BLOCK_ABSORB);

    /* Tracked 3D blade segment vs NPC upright cylinder hit tests */
    const float cylBase[3] = { 0.0f, 0.0f, -100.0f };
    float hitPt[3] = {0};
    const float hSegA[3] = { -60.0f, 100.0f, -100.0f }, hSegB[3] = { 60.0f, 100.0f, -100.0f };
    assert(gevrLnSegmentHitCylinder(hSegA, hSegB, cylBase, 40.0f, 175.0f, hitPt) == 1);
    assert(fabsf(hitPt[0]) < 1e-3f && fabsf(hitPt[1] - 100.0f) < 1e-3f && fabsf(hitPt[2] + 100.0f) < 1e-3f);
    const float vSegA[3] = { 10.0f, -50.0f, -100.0f }, vSegB[3] = { 10.0f, 250.0f, -100.0f };
    assert(gevrLnSegmentHitCylinder(vSegA, vSegB, cylBase, 40.0f, 175.0f, hitPt) == 1);
    assert(hitPt[1] >= -12.01f && hitPt[1] <= 187.01f);
    const float outRadA[3] = { 60.0f, 100.0f, -100.0f }, outRadB[3] = { 120.0f, 100.0f, -100.0f };
    assert(gevrLnSegmentHitCylinder(outRadA, outRadB, cylBase, 40.0f, 175.0f, hitPt) == 0);
    const float hiA[3] = { -60.0f, 220.0f, -100.0f }, hiB[3] = { 60.0f, 220.0f, -100.0f };
    assert(gevrLnSegmentHitCylinder(hiA, hiB, cylBase, 40.0f, 175.0f, hitPt) == 0);
    const float loA[3] = { -60.0f, -40.0f, -100.0f }, loB[3] = { 60.0f, -40.0f, -100.0f };
    assert(gevrLnSegmentHitCylinder(loA, loB, cylBase, 40.0f, 175.0f, hitPt) == 0);
}

static void test_procedural_audio(void)
{
    short buf[1024];

    /* Inactive with no triggers leaves buffer untouched */
    gevrLnAudioReset();
    memset(buf, 0, sizeof(buf));
    gevrLnAudioMix(buf, 512, 22050);
    for (int i = 0; i < 1024; i++) assert(buf[i] == 0);

    /* Active Lasersword hum + swing Doppler + Force Lightning + thrown boomerang produces bounded nonzero PCM */
    gevrLnAudioSetState(1, 1, 1.0f, 0.9f, 1, 1, 1, 1);
    memset(buf, 0, sizeof(buf));
    gevrLnAudioMix(buf, 512, 22050);
    long long energy = 0;
    for (int i = 0; i < 1024; i++) {
        assert(buf[i] >= -32767 && buf[i] <= 32767);
        energy += abs(buf[i]);
    }
    assert(energy > 50000);

    /* Every one-shot procedural SFX produces bounded nonzero energy and decays cleanly */
    for (int sfx = GEVR_LN_SFX_IGNITE; sfx <= GEVR_LN_SFX_SHADOW_STEP; sfx++) {
        gevrLnAudioReset();
        gevrLnAudioTrigger(sfx, 1.0f);
        memset(buf, 0, sizeof(buf));
        gevrLnAudioMix(buf, 512, 22050);
        long long e = 0;
        for (int i = 0; i < 1024; i++) {
            assert(buf[i] >= -32767 && buf[i] <= 32767);
            e += abs(buf[i]);
        }
        assert(e > 10000);
        for (int pass = 0; pass < 30; pass++) {
            memset(buf, 0, sizeof(buf));
            gevrLnAudioMix(buf, 512, 22050);
        }
        memset(buf, 0, sizeof(buf));
        gevrLnAudioMix(buf, 512, 22050);
        for (int i = 0; i < 1024; i++) assert(buf[i] == 0);
    }
}

int main(void)
{
    test_palette_and_metrics();
    test_triggers_and_slash();
    test_vr_wrist_and_ignition();
    test_thrown_lasersword_and_boomerang();
    test_choke_limbs_and_thrown_body();
    test_block_and_deflect();
    test_procedural_audio();
    puts("lasersword & ninja core: all passed");
    return 0;
}
