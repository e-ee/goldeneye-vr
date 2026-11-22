# Lasersword Weapon Mod (Lean Quest VR Branch)

> **Status:** Lean weapon mod branch (`feat/lasersword-weapon`) directly off `origin/main` in `e-ee/goldeneye-vr`, off by default (`VrLasersword = 0`), strictly **Quest VR APK-only**, and following GoldenEye VR's optional pack model (`oddjob-hat`).

Adds a selectable **Lasersword** melee weapon (`ITEM_KNIFE` inventory slot) for Quest VR while leaving flat-screen, mobile, Steam Frame, desktop/iOS/tvOS, Bond's normal guns, pickups, and NPC behaviors untouched.

## Downloadable Asset Pack (`lasersword.bin`)

Following the `oddjob-hat` pattern (`ModManager.java`), no 3D hilt mesh or audio synth lives in the APK or upstream C code:

- **Quest VR APK only (`BuildConfig.FLAVOR == "quest"`, `gevrIsQuestVrApk()`):** The `"lasersword"` mod entry is excluded from `mobile`, `frame`, and desktop/iOS/tvOS builds.
- **Launcher → Mods (`Get Lasersword`) & Launcher → Rules → Game rules (`Get Lasersword` beside `Original rules`):** Downloads `lasersword-1.0.zip` (`lasersword.bin`, `GEVRLS1` format) from `e-ee/goldeneye-vr-mods` (`release lasersword-1.0`) into `/sdcard/Android/data/com.gevr.port/files/texture-packs/lasersword/lasersword.bin` (`files/mods/lasersword.bin` overrides for local adb/testing).
- Until the pack is present, `gevrLsAvailable()` returns `0`, `Lasersword` stays blocked (`"Get it in Mods"`), and vanilla `ITEM_KNIFE` gameplay is untouched.
- Rebuild or customize the copyright-safe hilt `.obj` and 22050 Hz `.wav` SFX clips via `tools/gevr_lasersword_pack.py`.

## Settings

Three settings under **Launcher -> Rules -> Game rules** and in-game **Mods and fun** (Quest VR APK-only; saved in `goldeneye-vr.ini`):

- **`Lasersword`** (`VrLasersword`, default `Off`): Grants the Lasersword at mission start (`bondinvAddInvItem(ITEM_KNIFE)`). Activates physics ragdolls & dismemberment internally in `port/src/gevr_ragdoll.c` without mutating saved `VrRagdolls`, `VrDismemberment`, or `VrBodiesStay`.
- **`  Blade colour`** (`VrLaserswordColor`, 8 colours: Blue, Green, Red, Purple, Yellow, White, Orange, Gold).
- **`  NPC laser shots`** (`VrLaserswordNpcLasers`, default `Off`): Switches enemy gunshots and autoguns to GoldenEye's native visible laser beams (`ITEM_LASER`) and laser SFX.

## VR Mechanics

- **Per-hand Tracked VR Drawing (`gevrLsDrawHand` in `gunfire.c`):** Renders the downloaded 3D hilt mesh + two-layer energy blade on whichever hand equips it (`GUNRIGHT`, `GUNLEFT`, or dual wield), with world-space motion ribbons and wall-contact sparks (`pdphysRaycastAnyRoom`).
- **Spatial Decapitation & Dismemberment (`gevrRagdollForceCutNear`):**
  - 3D swept blade segments (`gevrLsSegmentHitCylinder`) hit standing guards within line of sight (`stanTestLineUnobstructed` + wall/door/glass checks) and slice fallen ragdolls directly at contact.
  - Lethal cuts target the ragdoll joint nearest the 3D hit point (`hitPt`), so head-height slashes sever the head/neck joint (`skel == 2`) while lower hits sever the struck limb/waist.
- **Bullet Deflection (`gevrLsInterceptPlayerDamage`):** Holding `trigger` on the VR blade hand deflects incoming bullets toward the shooter with a `0.22s` Perfect Parry window and reflected beam tracers.
- **Lasersword Throw:** Flicking forward on trigger release launches the spinning Lasersword (`1200 cm` range), slicing guards before rebounding back to the hand.
- **Pack Clip Audio (`port/src/gevr_lasersword.c`):** Mixes the downloaded `hum` loop (pitch-modulated by swing speed) and `ignite`, `slash`, `deflect`, and `clash` 22050 Hz clips into stereo PCM in `port/src/audio.c`.

## Hook Surface & Verification

- **Core:** `port/include/gevr_lasersword.h`, `port/src/gevr_lasersword.c` (pure ISO C11), `src/game/gevr_lasersword_game.{h,c}`, `tools/gevr_lasersword_pack.py`.
- **Hooks:** `CMakeLists.txt`, `port/src/audio.c`, `port/src/input.c`, `port/src/gevr_ragdoll.c`, `port/src/gevr_settings.c`, `port/vr/{vr_launcher.cpp,vr_settings*}`, `android/app/src/main/java/com/gevr/port/ModManager.java`, `src/game/{bg.c,bondview2.c,chraction.c,gunfire.c,initguards.c,lv.c,propobj.c}`.
- **Host Verification:** `port/tests/test_lasersword.py` verifies real `lasersword.bin` pack generation/loading/corruption rejection, pure C11 core math, clip PCM mixing, settings defaults/hiding/reset, and INI round-trip/clamping. Quest ARM64 compilation + Java flavor checks verify the Android VR APK target.
