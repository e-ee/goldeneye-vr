"""Compile Lasersword & Ninja core, settings table rules, and INI persistence."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import settings_stub_dir

ROOT = Path(__file__).resolve().parents[2]
CC = shutil.which("gcc") or "gcc"
CXX = shutil.which("g++") or "g++"

with tempfile.TemporaryDirectory(prefix="gevr-lasersword-ninja-") as temp:
    temp_dir = Path(temp)
    exe = temp_dir / "lasersword_ninja.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-Wall", "-Werror",
        "-I" + str(ROOT / "port/include"),
        str(ROOT / "port/tests/lasersword_ninja_native.c"),
        str(ROOT / "port/src/gevr_lasersword_ninja.c"),
        str(ROOT / "port/src/gevr_lasersword.c"),
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)

    # Verify compatibility when GoldenEye overrides math_atan2f/asinf/acosf.
    game = [str(ROOT / "src/game" / name) for name in ("math_atan2f.c", "math_asinfacosf.c", "math_asinacos.c")]
    exe = temp_dir / "lasersword_ninja_game_math.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-D_LANGUAGE_C", "-Wno-incompatible-pointer-types",
        "-I" + str(ROOT / "port/include"),
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "src"),
        "-I" + str(ROOT / "src/game"),
        str(ROOT / "port/tests/lasersword_ninja_native.c"),
        str(ROOT / "port/src/gevr_lasersword_ninja.c"),
        str(ROOT / "port/src/gevr_lasersword.c"),
        *game,
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)

    # Verify settings table defaults, Lasersword<->Ninja mutual exclusivity, and Reset all settings.
    stub_dir = temp_dir / "settings"
    stub_dir.mkdir()
    table = settings_stub_dir.make(stub_dir, ROOT)
    mutex_c = stub_dir / "lasersword_ninja_settings_test.c"
    mutex_c.write_text(r'''
#include "gevr_settings.h"
#include "settings_table_stubs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int find(const char *label) {
    GevrSettingInfo info;
    for (int i = 0; i < gevrSettingCount(); i++) {
        if (gevrSettingInfo(i, &info) && !strcmp(info.label, label)) return i;
    }
    return -1;
}

int main(void) {
    gevrSettingsReadBuiltIn();
    assert(VrLaserswordNinjaMode == 0);
    assert(VrLaserswordLasers == 1);
    assert(VrLaserswordPickupGuns == 0);
    assert(VrNinjaPickupGuns == 0);
    assert(VrRagdolls == 0 && VrDismemberment == 0 && VrBodiesStay == 0);

    const int lsword = find("Lasersword mode");
    const int ninja = find("Ninja mode");
    const int lasers = find("Laser guns");
    const int jPickup = find("Pick up guns");
    assert(lsword >= 0 && ninja >= 0 && lasers >= 0 && jPickup >= 0);
    assert(find("Weapon wheel") == -1);

    gevrSettingSet(lsword, 1);
    assert(VrLaserswordNinjaMode == 1 && gevrSettingIndex(lsword) == 1 && gevrSettingIndex(ninja) == 0);
    assert(VrRagdolls == 0 && VrDismemberment == 0 && VrBodiesStay == 0);

    gevrSettingSet(ninja, 1);
    assert(VrLaserswordNinjaMode == 2 && gevrSettingIndex(lsword) == 0 && gevrSettingIndex(ninja) == 1);
    assert(VrRagdolls == 0 && VrDismemberment == 0 && VrBodiesStay == 0);

    gevrSettingSet(lasers, 0);
    gevrSettingSet(jPickup, 1);
    gevrSettingResetAll();
    assert(VrLaserswordNinjaMode == 0 && gevrSettingIndex(lsword) == 0 && gevrSettingIndex(ninja) == 0);
    assert(VrLaserswordLasers == 1 && VrLaserswordPickupGuns == 0 && VrNinjaPickupGuns == 0);
    puts("lasersword & ninja settings: defaults, mutual exclusivity and reset passed");
    return 0;
}
''')
    mutex_exe = stub_dir / "lasersword_ninja_settings_test.exe"
    subprocess.run([
        CC, "-std=gnu11", "-O1", "-Wall", "-Werror=implicit-function-declaration",
        "-I" + str(stub_dir),
        "-I" + str(ROOT / "port/include"),
        "-I" + str(ROOT / "port/tests"),
        str(table), str(mutex_c), "-lm", "-o", str(mutex_exe),
    ], check=True)
    subprocess.run([str(mutex_exe)], check=True)

    # Verify goldeneye-vr.ini round-trip & clamping from vr_settings.cpp.
    vr_settings = (ROOT / "port/vr/vr_settings.cpp").read_text()
    save_start = vr_settings.index('    iniPrintf(f, "LaserswordNinjaMode=%d')
    save_block = vr_settings[save_start:vr_settings.index('    iniPrintf(f, "Ragdolls=%d', save_start)].replace("iniPrintf(", "fprintf(")
    load_start = vr_settings.index('            else if (strcmp(key, "LaserswordNinjaMode")')
    load_block = vr_settings[load_start:vr_settings.index('            else if (strcmp(key, "Ragdolls")', load_start)].replace("else if", "if", 1)
    ini_cpp = temp_dir / "lasersword_ninja_ini_test.cpp"
    ini_cpp.write_text(f'''
#include <cassert>
#include <cstdio>
#include <cstring>

int VrLaserswordNinjaMode, VrLaserswordHand, VrLaserswordColor, VrLaserswordStyle, VrNinjaSword, VrNinjaBoomerang;
int VrLaserswordForcePowers = 1, VrLaserswordCatchProjectiles = 1, VrLnPerfectParry = 1, VrLnBladeTrails = 1, VrLnAudio = 1;
int VrNinjaShield = 1, VrNinjaRicochet = 1, VrNinjaShadowStep = 1, VrNinjaBulletSlice = 1;
int VrRivalNpc, VrRivalInNormalMode, VrRivalScale, VrNinjaNpc, VrNinjaInNormalMode;
int VrAllyLasersword;
int VrLaserswordLasers = 1, VrLaserswordPickupGuns, VrNinjaPickupGuns;
int VrLasersword, VrLaserswordNpcLasers;

static void saveLn(FILE *f) {{
{save_block}
}}

static void loadLn(const char *line) {{
    char key[64];
    int ival;
    if (sscanf(line, "%63[^=]=%d", key, &ival) == 2) {{
{load_block}
    }}
}}

int main() {{
    VrLaserswordNinjaMode = 2; VrLaserswordHand = 1; VrLaserswordColor = 6; VrLaserswordStyle = 2;
    VrNinjaSword = 5; VrNinjaBoomerang = 3;
    VrLaserswordForcePowers = 0; VrLaserswordCatchProjectiles = 0; VrLnPerfectParry = 0;
    VrLnBladeTrails = 0; VrLnAudio = 0;
    VrNinjaShield = 0; VrNinjaRicochet = 0; VrNinjaShadowStep = 0; VrNinjaBulletSlice = 0;
    VrRivalNpc = 2; VrRivalInNormalMode = 1; VrRivalScale = 3;
    VrNinjaNpc = 2; VrNinjaInNormalMode = 1;
    VrAllyLasersword = 1;
    VrLaserswordLasers = 0; VrLaserswordPickupGuns = 1; VrNinjaPickupGuns = 1;

    FILE *f = tmpfile();
    assert(f);
    saveLn(f);
    rewind(f);

    VrLaserswordNinjaMode = VrLaserswordHand = VrLaserswordColor = VrLaserswordStyle = VrNinjaSword = VrNinjaBoomerang = 0;
    VrLaserswordForcePowers = VrLaserswordCatchProjectiles = VrLnPerfectParry = VrLnBladeTrails = VrLnAudio = 1;
    VrNinjaShield = VrNinjaRicochet = VrNinjaShadowStep = VrNinjaBulletSlice = 1;
    VrRivalNpc = VrRivalInNormalMode = VrRivalScale = VrNinjaNpc = VrNinjaInNormalMode = 0;
    VrAllyLasersword = VrLaserswordPickupGuns = VrNinjaPickupGuns = 0;
    VrLaserswordLasers = 1;

    char line[256];
    while (fgets(line, sizeof(line), f)) loadLn(line);
    fclose(f);

    assert(VrLaserswordNinjaMode == 2 && VrLaserswordHand == 1 && VrLaserswordColor == 6 && VrLaserswordStyle == 2);
    assert(VrNinjaSword == 5 && VrNinjaBoomerang == 3);
    assert(!VrLaserswordForcePowers && !VrLaserswordCatchProjectiles && !VrLnPerfectParry && !VrLnBladeTrails && !VrLnAudio);
    assert(!VrNinjaShield && !VrNinjaRicochet && !VrNinjaShadowStep && !VrNinjaBulletSlice);
    assert(VrRivalNpc == 2 && VrRivalInNormalMode == 1 && VrRivalScale == 3);
    assert(VrNinjaNpc == 2 && VrNinjaInNormalMode == 1);
    assert(VrAllyLasersword == 1);
    assert(VrLaserswordLasers == 0 && VrLaserswordPickupGuns == 1 && VrNinjaPickupGuns == 1);

    loadLn("LaserswordNinjaMode=99"); loadLn("LaserswordColor=-5"); loadLn("LaserswordStyle=9");
    loadLn("NinjaSword=42"); loadLn("NinjaBoomerang=-1"); loadLn("RivalScale=9");
    assert(VrLaserswordNinjaMode == 2 && VrLaserswordColor == 0 && VrLaserswordStyle == 2);
    assert(VrNinjaSword == 5 && VrNinjaBoomerang == 0 && VrRivalScale == 3);
    puts("lasersword & ninja ini: round-trip and clamping passed");
    return 0;
}}
''')
    ini_exe = temp_dir / "lasersword_ninja_ini_test.exe"
    subprocess.run([CXX, "-std=c++17", "-O2", str(ini_cpp), "-o", str(ini_exe)], check=True)
    subprocess.run([str(ini_exe)], check=True)
