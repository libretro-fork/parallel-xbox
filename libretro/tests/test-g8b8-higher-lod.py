#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production shader generation for all extended minimum/address codes."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from glib_fixture import compat_glib_flags

repo = Path(sys.argv[1]).resolve()
header = (repo / "hw/xbox/nv2a/pgraph/glsl/psh.h").read_text()
state = re.search(r"typedef struct PshState \{.*?\} PshState;", header, re.S).group()
source = (repo / "hw/xbox/nv2a/pgraph/glsl/psh.c").read_text()
helper = re.search(r"^static void psh_append_signed_g8b8_mips\(.*?^\}", source, re.S | re.M).group()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
enum ConvolutionFilter { CONV_DISABLED };
enum PshDepthFormat { DEPTH_DISABLED };
enum PshShadowDepthFunc { SHADOW_DISABLED };
/* STATE */
#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"
typedef GString MString;
#define mstring_append_fmt g_string_append_printf
#define mstring_append g_string_append
struct PixelShader { const PshState *state; struct { bool vulkan; } opts; };
/* HELPER */
int main(void) {
    for (unsigned byte = 0; byte < 256; byte++) {
        assert(!!g_ascii_iscntrl((gchar)(guchar)byte) ==
               (byte < 32 || byte == 127));
    }
    unsigned cases = 0;
    for (unsigned api = 0; api < 2; api++)
    for (unsigned stage = 0; stage < 4; stage++)
    for (unsigned level = 4; level <= 15; level++)
    for (unsigned u = 1; u <= 3; u++)
    for (unsigned v = 1; v <= 3; v++)
    for (unsigned filter = 4; filter <= 6; filter++) {
        PshState state = {0};
        state.g8b8_signed[stage] = 9 | 128 | (filter != 5 ? 16 : 0) |
                                 (filter >= 5 ? 64 : 0);
        psh_g8b8_set_extended_lod(&state, stage, level, u, v);
        struct PixelShader ps = {.state = &state, .opts = {.vulkan = api}};
        MString *text = g_string_new("");
        psh_append_signed_g8b8_mips(&ps, text, stage, state.g8b8_signed[stage]);
        char expected[160];
        snprintf(expected, sizeof(expected),
                 "max(textureQueryLod(texSamp%u, gbCoord%u).x, %u.0)",
                 stage, stage, level);
        assert(strstr(text->str, expected));
        assert(!strstr(text->str, "gbLinear"));
        assert(!!strstr(text->str, "ceil(gbLod") == (filter >= 5));
        assert(!!strstr(text->str, "abs(mod(") == (u == 2 || v == 2));
        assert(!!strstr(text->str, "clamp(gbBase") == (u == 3 || v == 3));
        g_string_free(text, true);
        cases++;
    }
    printf("PASS: %u extended minimum/address shader-generation cases\n", cases);
}
'''.replace("/* STATE */", state).replace("/* HELPER */", helper)
cc = sys.argv[2] if len(sys.argv) > 2 else "cc"
flags = compat_glib_flags(repo, cc)
with tempfile.TemporaryDirectory(prefix="g8b8-higher-lod-") as folder:
    c = Path(folder) / "test.c"
    binary = Path(folder) / "test"
    c.write_text(code)
    for char_mode in ("-fsigned-char", "-funsigned-char"):
        subprocess.run([cc, "-std=gnu11", "-O2", char_mode,
                        "-Wall", "-Wextra", "-Werror", "-I", str(repo), str(c),
                        "-o", str(binary), *flags], check=True)
        subprocess.run([str(binary)], check=True)
