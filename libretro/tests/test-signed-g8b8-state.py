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
legacy = state.replace("uint8_t g8b8_mip_flags;", "bool universal;").replace("PshState", "LegacyPshState")
legacy = legacy.replace("uint8_t g8b8_mip_clamp_u;", "").replace("uint8_t g8b8_mip_clamp_v;", "")
legacy = legacy.replace("uint8_t g8b8_mip_mirror;", "")
pixel_source = (repo / "hw/xbox/nv2a/pgraph/glsl/psh.c").read_text()
encoding = re.search(r"^            if \(signs.*?^            \}$", pixel_source, re.S | re.M).group()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
enum ConvolutionFilter { CONV_DISABLED };
enum PshDepthFormat { DEPTH_DISABLED };
enum PshShadowDepthFunc { SHADOW_DISABLED };
/* STATE */
_Static_assert(sizeof(PshState) == sizeof(LegacyPshState), "seed state size changed");
_Static_assert(offsetof(PshState, g8b8_mip_flags) == offsetof(LegacyPshState, universal), "reserved byte moved");
_Static_assert(offsetof(PshState, g8b8_signed) == offsetof(LegacyPshState, g8b8_signed), "signed byte layout changed");
_Static_assert(offsetof(PshState, depth_format) == offsetof(LegacyPshState, depth_format), "seed tail moved");
_Static_assert(offsetof(PshState, surface_zeta_format) == offsetof(LegacyPshState, surface_zeta_format), "seed surface offset moved");
_Static_assert(offsetof(PshState, g8b8_mip_mirror) < offsetof(LegacyPshState, surface_zeta_format), "address flags exceed old padding");
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
static PshState encode_signed_g8b8(unsigned signs, unsigned min_filter,
                                 unsigned mag_filter, bool mip_filter,
                                 unsigned address_u, unsigned address_v,
                                 bool mip_lod0, bool others, int i, unsigned mip_min_lod) {
    PshState result;
    memset(&result, 0, sizeof(result));
    PshState *state = &result;
    if (others) {
        state->g8b8_mip_flags = 0xffu ^ ((1u << i) | (1u << (i + 4)));
        state->g8b8_mip_clamp_u = 0x55u & ~((1u << i) | (1u << (i + 4)));
        state->g8b8_mip_clamp_v = 0xaau & ~((1u << i) | (1u << (i + 4)));
        state->g8b8_mip_mirror = 0x96u & ~((1u << i) | (1u << (i + 4)));
    }
    bool mip_clamp_u = address_u == 1, mip_clamp_v = address_v == 1;
    bool mip_mirror_u = address_u == 2, mip_mirror_v = address_v == 2;
    bool mip_clamp = mip_clamp_u && mip_clamp_v;
    bool min_linear = min_filter == 2 || min_filter == 4 || min_filter == 6;
    bool linear_filter = min_linear && mag_filter == 2;
    bool mip_min_linear = min_filter == 4 || min_filter == 6;
    bool mixed_filter = !mip_filter && ((min_filter == 1 && mag_filter == 2) ||
                                       (min_filter == 2 && mag_filter == 1));
    bool mixed_mip_filter = mip_filter && (mag_filter != (min_linear ? 2u : 1u));
    /* ENCODING */
    return result;
}
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
    unsigned canonical = 0;
    for (unsigned stage = 0; stage < 4; stage++)
    for (unsigned signs = 0; signs < 16; signs++)
    for (unsigned filter = 3; filter <= 6; filter++)
    for (unsigned address_u = 0; address_u < 3; address_u++)
    for (unsigned address_v = 0; address_v < 3; address_v++)
    for (unsigned others = 0; others < 2; others++) {
        unsigned base_filter = filter % 2 ? 1 : 2;
        PshState mip = encode_signed_g8b8(signs, filter, base_filter,
                                        true, address_u, address_v, true, others, stage, 0);
        PshState base = encode_signed_g8b8(signs, base_filter, base_filter,
                                         false, address_u, address_v, true, others, stage, 0);
        assert(!memcmp(&mip, &base, sizeof(mip)));
        canonical++;
    }
    unsigned addressed = 0;
    for (unsigned stage = 0; stage < 4; stage++)
    for (unsigned signs = 0; signs < 16; signs++)
    for (unsigned filter = 3; filter <= 6; filter++)
    for (unsigned mag = 1; mag <= 2; mag++)
    for (unsigned zero = 0; zero < 2; zero++)
    for (unsigned u = 0; u < 3; u++)
    for (unsigned v = 0; v < 3; v++) {
        PshState key = encode_signed_g8b8(signs, filter, mag, true,
                                         u, v, zero, true, stage, 0);
        bool mixed = mag != (filter % 2 ? 1u : 2u);
        bool explicit_fetch = signs && !(zero && !mixed) &&
                              (!(filter % 2) || mixed || filter == 5);
        unsigned bit = 1u << stage;
        assert(key.g8b8_mip_clamp_u == ((0x55u & ~(bit | (bit << 4))) |
               (explicit_fetch && u == 1 && v != 1 ? bit : 0)));
        assert(key.g8b8_mip_clamp_v == ((0xaau & ~(bit | (bit << 4))) |
               (explicit_fetch && v == 1 && u != 1 ? bit : 0)));
        assert(!!(key.g8b8_mip_flags & (bit << 4)) ==
               (explicit_fetch && u == 1 && v == 1));
        assert(key.g8b8_mip_mirror == ((0x96u & ~(bit | (bit << 4))) |
               (explicit_fetch && u == 2 ? bit : 0) |
               (explicit_fetch && v == 2 ? bit << 4 : 0)));
        addressed++;
    }
    unsigned lower = 0;
    for (unsigned stage = 0; stage < 4; stage++)
    for (unsigned signs = 0; signs < 16; signs++)
    for (unsigned filter = 3; filter <= 6; filter++)
    for (unsigned level = 1; level <= 2; level++)
    for (unsigned others = 0; others < 2; others++) {
        unsigned mag = filter % 2 ? 1 : 2;
        PshState key = encode_signed_g8b8(signs, filter, mag, true,
                                         0, 0, false, others, stage, level);
        PshState base = encode_signed_g8b8(signs, filter, mag, true,
                                          0, 0, false, others, stage, 0);
        unsigned bit = 1u << (stage + 4);
        bool fetch = signs && filter != 3;
        base.g8b8_mip_clamp_u |= fetch && (level & 1) ? bit : 0;
        base.g8b8_mip_clamp_v |= fetch && (level & 2) ? bit : 0;
        assert(!memcmp(&key, &base, sizeof(key)));
        lower++;
    }
    printf("PASS: %u lower-mip state encodings\n", lower);
    printf("PASS: %u independent-axis state encodings\n", addressed);
    printf("PASS: %u signed-G8B8 address invalidation cases and shader field coverage\n", cases);
    printf("PASS: %u base-only shader cache canonicalization cases\n", canonical);
}
'''.replace("/* PRODUCTION */", function).replace("/* MASKS */", masks).replace("/* STATE */", state + "\n" + legacy).replace("/* ENCODING */", encoding)
with tempfile.TemporaryDirectory(prefix="g8b8-state-") as folder:
    source = Path(folder) / "test.c"
    binary = Path(folder) / "test"
    source.write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "cc", "-std=gnu11", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-I", str(repo), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
