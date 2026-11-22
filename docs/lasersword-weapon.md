# Lasersword Weapon Mod (Lean Branch)

> **Status:** Lean weapon mod branch (`feat/lasersword-weapon`) directly off `origin/main` in `e-ee/goldeneye-vr`, off by default (`VrLasersword = 0`) so vanilla GoldenEye 007 behavior is unchanged unless enabled.

Adds a selectable **Lasersword** melee weapon (`ITEM_KNIFE` inventory slot) while leaving Bond's normal guns, pickups, and NPC behaviors untouched.

## Features & Settings

Three settings under **Launcher -> Rules -> Game rules** and in-game **Mods and fun** (saved in `goldeneye-vr.ini`):

- **`Lasersword`** (`VrLasersword`, default `Off`): Adds the Lasersword to Bond's inventory at mission start (`bondinvAddInvItem(ITEM_KNIFE)`). Enables physics ragdolls & dismemberment internally in `port/src/gevr_ragdoll.c` without mutating saved `VrRagdolls`, `VrDismemberment`, or `VrBodiesStay` settings.
- **`  Blade colour`** (`VrLaserswordColor`, 8 colours: Blue, Green, Red, Purple, Yellow, White, Orange, Gold).
- **`  NPC laser shots`** (`VrLaserswordNpcLasers`, default `Off`): Switches enemy gunshots and autoguns to GoldenEye's native visible laser beams (`ITEM_LASER`) and laser SFX.

## Mechanics

- **Per-hand VR & Flat Drawing (`gevrLsDrawHand` in `gunfire.c`):** Renders as the held item on whichever hand equips it (`GUNRIGHT`, `GUNLEFT`, or dual wield), using tracked OpenXR hand pose/velocity in stereo VR or animated swings/block poses on flat screen, with world-space motion ribbons and wall-contact sparks (`pdphysRaycastAnyRoom`).
- **Spatial Decapitation & Dismemberment (`gevrRagdollForceCutNear`):**
  - **Stereo VR:** 3D swept blade segments (`gevrLsSegmentHitCylinder`) hit standing guards within line of sight (`stanTestLineUnobstructed` + wall/door/glass checks) and slice fallen ragdolls directly at the contact point.
  - **Closest-Joint Cuts:** Lethal cuts target the ragdoll joint nearest the 3D hit point (`hitPt`) rather than fixed waist/root cuts, so head-height slashes sever the head/neck joint (`skel == 2`) while torso/limb slashes sever the struck region.
- **Bullet Deflection (`gevrLsInterceptPlayerDamage`):** While holding block (`trigger` on the VR blade hand, `LT`/RMB in flat) and facing the shooter (`gevrLsBulletTravelToShooter` + `gevrLsCheckVrBladeBlock` / `gevrLsCheckBlock`), incoming bullets deflect back toward the shooter with a `0.22s` Perfect Parry crit window and reflected beam tracers.
- **Lasersword Throw:** Flicking forward on trigger release in VR (or double-tapping `LT` / holding & releasing `RT` on pad/mouse) launches the spinning Lasersword (`1200 cm` range), slicing guards in its path before bouncing back to the throwing hand.
- **Procedural Audio (`port/src/gevr_lasersword.c`):** Synthesizes blade ignition, dual-detuned idle hum + Doppler swing modulation, slashes, clashes, and deflections at `22050 Hz` when a Lasersword is active.

## Hook Surface & Verification

- **Core:** `port/include/gevr_lasersword.h`, `port/src/gevr_lasersword.c` (pure C11, zero game-state headers), `src/game/gevr_lasersword_game.{h,c}`.
- **Hooks:** `CMakeLists.txt`, `port/src/audio.c`, `port/src/input.c`, `port/src/gevr_ragdoll.c`, `port/src/gevr_settings.c`, `port/vr/vr_settings*`, `src/game/{bg.c,bondview2.c,chraction.c,gunfire.c,initguards.c,lv.c,propobj.c}` (with zero launcher C++ UI changes).
- **Host Verification:** `port/tests/test_lasersword.py` verifies the pure C11 core (`port/tests/lasersword_native.c`), settings defaults/unmutated ragdoll flags/reset, and INI round-trip/clamping. Full multi-platform CMake builds (`cmake`/`sdl2` not installed locally) and on-headset VR playtesting still remain on target toolchains/hardware.
