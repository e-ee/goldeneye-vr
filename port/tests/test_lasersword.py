"""Compile lean Lasersword weapon core, settings table rules, and INI persistence."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import settings_stub_dir

ROOT = Path(__file__).resolve().parents[2]
CC = shutil.which("gcc") or "gcc"
CXX = shutil.which("g++") or "g++"

with tempfile.TemporaryDirectory(prefix="gevr-lasersword-") as temp:
    temp_dir = Path(temp)
    exe = temp_dir / "lasersword.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-Wall", "-Werror",
        "-I" + str(ROOT / "port/include"),
        str(ROOT / "port/tests/lasersword_native.c"),
        str(ROOT / "port/src/gevr_lasersword.c"),
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)

    # Verify compatibility when GoldenEye overrides math_atan2f/asinf/acosf.
    game = [str(ROOT / "src/game" / name) for name in ("math_atan2f.c", "math_asinfacosf.c", "math_asinacos.c")]
    exe = temp_dir / "lasersword_game_math.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-D_LANGUAGE_C", "-Wno-incompatible-pointer-types",
        "-I" + str(ROOT / "port/include"),
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "src"),
        "-I" + str(ROOT / "src/game"),
        str(ROOT / "port/tests/lasersword_native.c"),
        str(ROOT / "port/src/gevr_lasersword.c"),
        *game,
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)

    # Verify settings defaults, unmutated ragdoll flags, and Reset all settings.
    stub_dir = temp_dir / "settings"
    stub_dir.mkdir()
    table = settings_stub_dir.make(stub_dir, ROOT)
    set_c = stub_dir / "lasersword_settings_test.c"
    set_c.write_text(r'''
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
    assert(VrLasersword == 0);
    assert(VrLaserswordColor == 0);
    assert(VrLaserswordNpcLasers == 0);
    assert(VrRagdolls == 0 && VrDismemberment == 0 && VrBodiesStay == 0);

    const int ls = find("Lasersword");
    const int col = find("  Blade colour") >= 0 ? find("  Blade colour") : find("Blade colour");
    const int npc = find("  NPC laser shots");
    assert(ls >= 0 && col >= 0 && npc >= 0);

    gevrSettingSet(ls, 1);
    assert(VrLasersword == 1);
    assert(VrRagdolls == 0 && VrDismemberment == 0 && VrBodiesStay == 0);

    gevrSettingSet(col, 4);
    gevrSettingSet(npc, 1);
    assert(VrLaserswordColor == 4 && VrLaserswordNpcLasers == 1);

    gevrSettingResetAll();
    assert(VrLasersword == 0 && VrLaserswordColor == 0 && VrLaserswordNpcLasers == 0);
    puts("lasersword settings: defaults, unmutated ragdolls and reset passed");
    return 0;
}
''')
    set_exe = stub_dir / "lasersword_settings_test.exe"
    subprocess.run([
        CC, "-std=gnu11", "-O1", "-Wall", "-Werror=implicit-function-declaration",
        "-I" + str(stub_dir),
        "-I" + str(ROOT / "port/include"),
        "-I" + str(ROOT / "port/tests"),
        str(table), str(set_c), "-lm", "-o", str(set_exe),
    ], check=True)
    subprocess.run([str(set_exe)], check=True)

    # Verify goldeneye-vr.ini round-trip & clamping from vr_settings.cpp.
    vr_settings = (ROOT / "port/vr/vr_settings.cpp").read_text()
    save_start = vr_settings.index('    iniPrintf(f, "Lasersword=%d')
    save_block = vr_settings[save_start:vr_settings.index('    iniPrintf(f, "Ragdolls=%d', save_start)].replace("iniPrintf(", "fprintf(")
    load_start = vr_settings.index('            else if (strcmp(key, "Lasersword")')
    load_block = vr_settings[load_start:vr_settings.index('            else if (strcmp(key, "Ragdolls")', load_start)].replace("else if", "if", 1)
    ini_cpp = temp_dir / "lasersword_ini_test.cpp"
    ini_cpp.write_text(f'''
#include <cassert>
#include <cstdio>
#include <cstring>

int VrLasersword, VrLaserswordColor, VrLaserswordNpcLasers;

static void saveLs(FILE *f) {{
{save_block}
}}

static void loadLs(const char *line) {{
    char key[64];
    int ival;
    if (sscanf(line, "%63[^=]=%d", key, &ival) == 2) {{
{load_block}
    }}
}}

int main() {{
    VrLasersword = 1; VrLaserswordColor = 6; VrLaserswordNpcLasers = 1;
    FILE *f = tmpfile();
    assert(f);
    saveLs(f);
    rewind(f);

    VrLasersword = VrLaserswordColor = VrLaserswordNpcLasers = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) loadLs(line);
    fclose(f);
    assert(VrLasersword == 1 && VrLaserswordColor == 6 && VrLaserswordNpcLasers == 1);

    loadLs("Lasersword=9"); loadLs("LaserswordColor=-5");
    assert(VrLasersword == 1 && VrLaserswordColor == 0);
    loadLs("LaserswordColor=99"); loadLs("LaserswordNpcLasers=2");
    assert(VrLaserswordColor == 7 && VrLaserswordNpcLasers == 1);
    puts("lasersword ini: round-trip and clamping passed");
    return 0;
}}
''')
    ini_exe = temp_dir / "lasersword_ini_test.exe"
    subprocess.run([CXX, "-std=c++17", "-O2", str(ini_cpp), "-o", str(ini_exe)], check=True)
    subprocess.run([str(ini_exe)], check=True)
