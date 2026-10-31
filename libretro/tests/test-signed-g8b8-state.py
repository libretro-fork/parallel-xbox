#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production signed-G8B8 shader invalidation without an emulator."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/glsl/shaders.c").read_text()
function = re.search(r"^static bool signed_g8b8_address_dirty\(.*?^\}", source, re.S | re.M).group()
header = (repo / "hw/xbox/nv2a/pgraph/glsl/shaders.h").read_text()
masks = "\n".join(re.search(r"#define " + name + r" .*?(?<!\\)\n", header, re.S).group()
                  for name in ("NV_PGRAPH_TEXFMT0_SHADER_FIELDS", "NV_PGRAPH_TEXFILTER0_SHADER_FIELDS"))
state_header = (repo / "hw/xbox/nv2a/pgraph/glsl/psh.h").read_text()
state = re.search(r"typedef struct PshState \{.*?\} PshState;", state_header, re.S).group()
legacy = state.replace("uint8_t g8b8_mip_mixed;", "bool universal;").replace("PshState", "LegacyPshState")
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
enum ConvolutionFilter { CONV_DISABLED };
enum PshDepthFormat { DEPTH_DISABLED };
enum PshShadowDepthFunc { SHADOW_DISABLED };
/* STATE */
_Static_assert(sizeof(PshState) == sizeof(LegacyPshState), "seed state size changed");
_Static_assert(offsetof(PshState, g8b8_mip_mixed) == offsetof(LegacyPshState, universal), "reserved byte moved");
_Static_assert(offsetof(PshState, g8b8_signed) == offsetof(LegacyPshState, g8b8_signed), "signed byte layout changed");
_Static_assert(offsetof(PshState, depth_format) == offsetof(LegacyPshState, depth_format), "seed tail moved");
static unsigned ctz32(uint32_t x) { return __builtin_ctz(x); }
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef struct { uint32_t fmt[4], filter[4]; bool dirty[4]; } PGRAPHState;
static bool pgraph_is_reg_dirty(PGRAPHState *pg, unsigned reg) {
    assert(reg >= NV_PGRAPH_TEXADDRESS0 && reg < NV_PGRAPH_TEXADDRESS0 + 16);
    return pg->dirty[(reg - NV_PGRAPH_TEXADDRESS0) / 4];
}
static uint32_t pgraph_reg_r(PGRAPHState *pg, unsigned reg) {
    if (reg >= NV_PGRAPH_TEXFMT0 && reg < NV_PGRAPH_TEXFMT0 + 16)
        return pg->fmt[(reg - NV_PGRAPH_TEXFMT0) / 4];
    assert(reg >= NV_PGRAPH_TEXFILTER0 && reg < NV_PGRAPH_TEXFILTER0 + 16);
    return pg->filter[(reg - NV_PGRAPH_TEXFILTER0) / 4];
}
/* MASKS */
/* PRODUCTION */
int main(void) {
    unsigned cases = 0;
    for (unsigned slot = 0; slot < 4; slot++)
    for (unsigned color = 0; color < 128; color++)
    for (unsigned levels = 0; levels < 16; levels++)
    for (unsigned signs = 0; signs < 16; signs++)
    for (unsigned dirty = 0; dirty < 2; dirty++) {
        PGRAPHState pg = {0};
        pg.fmt[slot] = color << 8 | levels << 16;
        pg.filter[slot] = signs << 28;
        pg.dirty[slot] = dirty;
        bool expected = color == 0x28 && levels > 1 && signs && dirty;
        assert(signed_g8b8_address_dirty(&pg, slot) == expected);
        assert(!signed_g8b8_address_dirty(&pg, (slot + 1) % 4));
        cases++;
    }
    const uint32_t filter_fields[] = {
        NV_PGRAPH_TEXFILTER0_MAG, NV_PGRAPH_TEXFILTER0_MIN,
        NV_PGRAPH_TEXFILTER0_ASIGNED, NV_PGRAPH_TEXFILTER0_RSIGNED,
        NV_PGRAPH_TEXFILTER0_GSIGNED, NV_PGRAPH_TEXFILTER0_BSIGNED,
    };
    const uint32_t format_fields[] = {
        NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS, NV_PGRAPH_TEXFMT0_BASE_SIZE_U,
        NV_PGRAPH_TEXFMT0_BASE_SIZE_V,
    };
    for (unsigned i = 0; i < sizeof(filter_fields) / sizeof(*filter_fields); i++)
        assert((filter_fields[i] & NV_PGRAPH_TEXFILTER0_SHADER_FIELDS) == filter_fields[i]);
    for (unsigned i = 0; i < sizeof(format_fields) / sizeof(*format_fields); i++)
        assert((format_fields[i] & NV_PGRAPH_TEXFMT0_SHADER_FIELDS) == format_fields[i]);
    assert(!(NV_PGRAPH_TEXFILTER0_SHADER_FIELDS & NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS));
    printf("PASS: %u signed-G8B8 address invalidation cases and shader field coverage\n", cases);
}
'''.replace("/* PRODUCTION */", function).replace("/* MASKS */", masks).replace("/* STATE */", state + "\n" + legacy)
with tempfile.TemporaryDirectory(prefix="g8b8-state-") as folder:
    source = Path(folder) / "test.c"
    binary = Path(folder) / "test"
    source.write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "cc", "-std=gnu11", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-I", str(repo), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
