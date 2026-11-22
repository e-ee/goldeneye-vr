# Lasersword Weapon Mod (Lean Branch)

> **Status:** Lean weapon mod branch (`feat/lasersword-weapon`) directly off `origin/main` in `e-ee/goldeneye-vr`, off by default (`VrLasersword = 0`) and following GoldenEye VR's optional pack model (`oddjob-hat`).

Adds a selectable **Lasersword** melee weapon (`ITEM_KNIFE` inventory slot) while leaving Bond's normal guns, pickups, and NPC behaviors untouched.

## Downloadable Asset Pack (`lasersword.bin`)

Following the `oddjob-hat` pattern (`ModManager.java` / `port/pc/pc_mods.cpp`), no 3D hilt mesh or audio synth lives in the APK or upstream C code:

- **Launcher -> Mods -> `Get Lasersword`:** Downloads `lasersword-1.0.zip` (`lasersword.bin`, `GEVRLS1` format) from `e-ee/goldeneye-vr-mods` (`release lasersword-1.0`) into `texture-packs/lasersword/lasersword.bin` (`files/mods/lasersword.bin` overrides for local adb/testing).
- Until the pack is present, `gevrLsAvailable()` returns `0`, `Lasersword` stays blocked (`"Get it in Mods"`), and vanilla `ITEM_KNIFE` gameplay is untouched.
- Rebuild or customize the copyright-safe hilt `.obj` and 22050 Hz `.wav` SFX clips via `tools/gevr_lasersword_pack.py`.

## Settings

Three settings under **Launcher -> Rules -> Game rules** and in-game **Mods and fun** (saved in `goldeneye-vr.ini`):

- **`Lasersword`** (`VrLasersword`, default `Off`): Grants the Lasersword at mission start (`bondinvAddInvItem(ITEM_KNIFE)`). Activates physics ragdolls & dismemberment internally in `port/src/gevr_ragdoll.c` without mutating saved `VrRagdolls`, `VrDismemberment`, or `VrBodiesStay`.
- **`  Blade colour`** (`VrLaserswordColor`, 8 colours: Blue, Green, Red, Purple, Yellow, White, Orange, Gold).
- **`  NPC laser shots`** (`VrLaserswordNpcLasers`, default `Off`): Switches enemy gunshots and autoguns to GoldenEye's native visible laser beams (`ITEM_LASER`) and laser SFX.

## Mechanics

- **Per-hand VR & Flat Drawing (`gevrLsDrawHand` in `gunfire.c`):** Renders the downloaded 3D hilt mesh + two-layer energy blade on whichever hand equips it (`GUNRIGHT`, `GUNLEFT`, or dual wield), with world-space motion ribbons and wall-contact sparks (`pdphysRaycastAnyRoom`).
- **Spatial Decapitation & Dismemberment (`gevrRagdollForceCutNear`):**
  - **Stereo VR:** 3D swept blade segments (`gevrLsSegmentHitCylinder`) hit standing guards within line of sight (`stanTestLineUnobstructed` + wall/door/glass checks) and slice fallen ragdolls directly at contact.
  - **Closest-Joint Cuts:** Lethal cuts target the ragdoll joint nearest the 3D hit point (`hitPt`), so head-height slashes sever the head/neck joint (`skel == 2`) while lower hits sever the struck limb/waist.
- **Bullet Deflection (`gevrLsInterceptPlayerDamage`):** Holding block (`trigger` on the VR blade hand, `LT`/RMB in flat) deflects incoming bullets toward the shooter with a `0.22s` Perfect Parry window and reflected beam tracers.
- **Lasersword Throw:** Flicking forward on trigger release in VR (or double-tapping `LT` / hold-releasing `RT` in flat) launches the spinning Lasersword (`1200 cm` range), slicing guards before rebounding back to the hand.
- **Pack Clip Audio (`port/src/gevr_lasersword.c`):** Mixes the downloaded `hum` loop (pitch-modulated by swing speed) and `ignite`, `slash`, `deflect`, and `clash` 22050 Hz clips into stereo PCM in `port/src/audio.c`.

## Hook Surface & Verification

- **Core:** `port/include/gevr_lasersword.h`, `port/src/gevr_lasersword.c` (pure ISO C11), `src/game/gevr_lasersword_game.{h,c}`, `tools/gevr_lasersword_pack.py`.
- **Hooks:** `CMakeLists.txt`, `port/src/audio.c`, `port/src/input.c`, `port/src/gevr_ragdoll.c`, `port/src/gevr_settings.c`, `port/vr/{vr_launcher.cpp,vr_settings*}`, `port/pc/pc_mods.cpp`, `android/app/src/main/java/com/gevr/port/ModManager.java`, `src/game/{bg.c,bondview2.c,chraction.c,gunfire.c,initguards.c,lv.c,propobj.c}`.
- **Host Verification:** `port/tests/test_lasersword.py` verifies real `lasersword.bin` pack generation/loading/corruption rejection, pure C11 core math, clip PCM mixing, settings defaults/blocking/reset, and INI round-trip/clamping. Full multi-platform CMake builds (`cmake`/`sdl2` not installed locally) and on-headset VR playtesting remain on target toolchains/hardware.
