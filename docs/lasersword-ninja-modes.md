# Lasersword and Ninja Modes (Experimental Fork Feature)

> **Status:** Active development on the `e-ee/goldeneye-vr` fork (`feat/lasersword-ninja-modes`). Off by default (`VrLaserswordNinjaMode = 0`) so vanilla GoldenEye 007 gameplay, speedruns, co-op, Horde, and deathmatch are unchanged unless explicitly enabled in **Launcher -> Mods**.

Lasersword Mode and Ninja Mode are mutually exclusive opt-in combat modes built around a pure C11 simulation core (`port/include/gevr_lasersword_ninja.h`, `port/src/gevr_lasersword_ninja.c`) wired into GoldenEye's world tick, damage interception, projectile handling, NPC AI hooks, procedural audio mixer, and Fast3D render passes (`src/game/gevr_lasersword_ninja_game.c`).

---

## Lasersword Mode (`VrLaserswordNinjaMode = 1`)

- **Blade Styles & Colours:** Choose **Single**, **Double-Bladed**, or **Dual Blades** across 8 blade colours (**Blue**, **Green**, **Red**, **Purple**, **Yellow**, **White**, **Orange**, **Gold**) and select **Right** or **Left** primary hand (`VrLaserswordHand`).
- **Ignition & Blade Trails:** Opening ignition extends the blade smoothly (`GEVR_LN_IGNITE_SEC = 0.42s`). World-space blade motion ribbons (`VrLnBladeTrails`) are pushed once per gameplay tick in `gevrLnGameTick()` and read-only transformed per eye in `gevrLnDrawWorld()` / `gevrLnDrawFirstPerson()`. Dragging an active blade across nearby world walls spawns sparks using `stanTestLineUnobstructed` / wall checks.
- **Tracked 3D VR Melee & Flat Swings:**
  - **Stereo VR:** Uses tracked controller pose (`gevrLnGetHandPoseWorld`) and play-space velocity (`gevrLnGetHandVelocityWorld`) to sweep 3D world-space blade segments against NPC cylinders via `gevrLnSegmentHitCylinder`, gated by line-of-sight (`lnHasLos`) through walls, doors, and glass.
  - **Flat / Pad:** Right trigger (`RT` / left mouse button) taps alternate right-to-left / left-to-right slashes; rapid swings chain combos cleanly and respect wall/door/glass line of sight.
- **Deflection & Blocking (`gevrLnInterceptPlayerDamage`):**
  - GoldenEye passes bullet travel direction (`shooter -> player`); `gevrLnBulletTravelToShooter()` inverts it toward the shooter before checking facing/blocking geometry.
  - **Stereo VR:** `gevrLnCheckVrBladeBlock()` checks both that the block input is active and that the tracked blade is angled across the incoming line of fire (rather than pointing straight down it) and aims reflected bolts from controller pose (`handAimW`) / shooter vector.
  - **Flat / Pad:** Holding block (`LT` / right mouse button) while facing incoming gunfire deflects bolts back along the reflected vector. Timed parries within `0.22s` (`VrLnPerfectParry`) boost deflection damage and trigger blade locks on enemy sword clashes.
- **Lasersword Throw:**
  - Triggered by a VR forward wrist flick while blocking or via the Lasersword weapon wheel (`THROW` slot) / flat throw gesture (`gevrLnThrownLaunch`). Rebounds off walls via `gevrLnThrownStep()` and curves back to the player's current position.
- **Force Powers (`VrLaserswordForcePowers` on Opposite Hand / Lasersword Wheel):**
  - Lasersword wheel slots: `FORCE TK`, `LIGHTNING`, `THROW`, and `LASER` (or `GUNS` when `Laser guns` is off).
  - **Stereo VR Aim:** Force Push, Pull, Grab/Choke, and Lightning project along the tracked off-hand aim axis rather than camera center when hand poses are available.
  - **Telekinesis & Projectiles (`VrLaserswordCatchProjectiles`):** Lift/choke live guards (`GEVR_LN_CHOKE_DIST_CM = 92 cm`), orbit/launch dismembered limbs (`GEVR_LN_MAX_LIMBS = 24`), grab & throw ragdolls, and catch/reflect airborne rockets and grenades within line of sight (`lnHasLos`).
- **Laser Guns (`VrLaserswordLasers`):**
  - When enabled, enemy gunfire uses GoldenEye's native laser beams and laser sound effects, and starting a Lasersword mission grants GoldenEye's native Moonraker Laser (`ITEM_LASER`) in the right hand alongside your lasersword options.
- **Special NPCs:**
  - **Rival NPCs (`VrRivalNpc`):** Red-lasersword (including double-bladed variants) high-health enemies with size options (**Normal**, **Random**, **Giant**, **Tiny**), optional spawning in normal Bond missions (`Rivals in normal`), ground/stair-validated lunges (`stanTestLineUnobstructed` + `stanFindGroundAtCyl`), Force attacks gated by `lnHasLos`, suppressed gunfire (`gevrLnIsMeleeNpc`), and hand-attached blade transforms (`gevrLnNoteNpcHand`).
  - **Lasersword Ally (`VrAllyLasersword`):** Equips the AI Ally (`#194`) with a lasersword and LOS-checked Force Push / Pull abilities.

---

## Ninja Mode (`VrLaserswordNinjaMode = 2`)

- **Swords (`VrNinjaSword`):** **Katana**, **Gold Katana**, **Short**, **Gold Short**, **Dual Short**, and **Dual Gold** (`gevrLnWeaponReachCm` scales melee reach; dual styles arm both hands).
- **Ninja Wheel Slots:** `KATANA`, `SHORT`, `DUAL`, `SHIELD`, `BOOMERANG`, and `SHADOW`.
- **Ninja Shield (`VrNinjaShield`):** Off-hand shield (`gevrLnCheckVrBladeBlock` checks tracked off-hand orientation in VR) absorbs incoming gunfire or reflects shots back toward attackers.
- **Bullet Slice (`VrNinjaBulletSlice`):** Swinging a VR blade above threshold speed or active flat slash slashes incoming bullets out of the air with metallic sparks and SFX.
- **Boomerang Blades (`VrNinjaBoomerang`, `VrNinjaRicochet`):** **Shuriken**, **Wing**, **Gold Shuriken**, and **Gold Wing** spin out to ~`1400 cm`, ricochet cleanly off corridor walls (`gevrLnReflectVec`), and steer back to the thrower.
- **Shadow-Step (`VrNinjaShadowStep`):** Double-tap off-hand / `LT` or trigger from the `SHADOW` wheel slot for a speed burst inside a timed smoke cloud.
- **Evil Ninjas (`VrNinjaNpc`, `Ninjas in normal`):** Sword-and-shuriken adversaries with katana lunges and thrown boomerangs.

---

## Settings & Ragdoll Integration

All settings default to safe/vanilla values (`VrLaserswordNinjaMode = 0`, `VrRivalNpc = 0`, `VrNinjaNpc = 0`, `VrAllyLasersword = 0`) and live under **Launcher -> Mods** (`port/src/gevr_settings.c`, `port/vr/vr_settings.cpp`, `port/vr/vr_settings_defaults.c`):

- **Non-destructive Ragdolls & Dismemberment:** Toggling **Lasersword mode** or **Ninja mode** does **not** mutate your saved `VrRagdolls`, `VrDismemberment`, or `VrBodiesStay` settings. Instead, `port/src/gevr_ragdoll.c` activates ragdolls and dismemberment internally while `VrLaserswordNinjaMode != 0` and honors your configured `VrBodiesStay` body-count cap directly.
- **Flat-Screen Mode Wheel Only:** The flat-screen weapon/mode wheel (`LB` / `Q`) is active only when `VrLaserswordNinjaMode != 0`, without introducing an unrelated global controller-wheel setting.
- **Non-lethal & Lethal Melee Integration:** Blade hits route through GoldenEye's `handles_shot_actors()` with `ITEM_KNIFE` / `ITEM_FIST` (preserving shield/health proportionality, hit flinches, audio, and kill attribution).

---

## Current Geometry, Audio, Build & Verification Notes

- **Placeholder Procedural Geometry:** First-person and world laserswords, katanas, shields, boomerangs, and lightning currently use self-contained 6-sided directionally shaded Fast3D prisms and polygons rendered inside `bondview2.c`'s 3D viewmodel block (`gevrLnDrawFirstPerson`) and `bg.c`'s world pass (`gevrLnDrawWorld`) — intended as placeholder geometry pending finished copyright-safe 3D models.
- **Procedural Audio (`VrLnAudio`):** `gevrLnAudioMix()` mixes a synthesized blade hum, swing Doppler shift, clashes, Force Push/Lightning, and Shadow-Step SFX directly into the stereo PCM buffer in `port/src/audio.c`.
- **Host Checks vs. Full Device Testing:**
  - `port/tests/test_lasersword_ninja.py` verifies the pure C11 core (`port/tests/lasersword_ninja_native.c`: triggers, ignition, thrown/boomerang flight & bounce, choke/limb blast math, bullet-travel negation, VR blade/shield block angles, 3D cylinder segment hit tests, and PCM audio synthesis), settings defaults/exclusivity/unmutated ragdoll flags, and `goldeneye-vr.ini` round-trip/clamping.
  - Full multi-platform CMake builds (Quest, PC VR/flat, Apple platforms, server) and live in-headset VR playtesting still require building and running on target toolchains/hardware before upstream consideration.
