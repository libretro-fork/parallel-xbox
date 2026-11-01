#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production linear shapes and Force LOD0 cube-chain invariants."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/texture.c").read_text()
header = (repo / "hw/xbox/nv2a/pgraph/texture.h").read_text()
shape = re.search(r"typedef struct TextureShape \{.*?\} TextureShape;", header, re.S).group()
info = re.search(r"typedef struct BasicColorFormatInfo \{.*?\} BasicColorFormatInfo;", header, re.S).group()
formats = re.search(r"^const BasicColorFormatInfo kelvin_color_format_info_map.*?^\};",
                    source, re.S | re.M).group()
function = re.search(r"^TextureShape pgraph_get_texture_shape\(.*?^\}", source, re.S | re.M).group()
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define NV2A_DPRINTF(...) ((void)0)
static unsigned ctz32(uint32_t x) { return __builtin_ctz(x); }
#include "hw/xbox/nv2a/nv2a_regs.h"
/* TYPES */
/* FORMATS */
typedef struct { uint32_t ctl[4], pitch[4], fmt[4], rect[4], program; } PGRAPHState;
static uint32_t pgraph_reg_r(PGRAPHState *pg, unsigned reg) {
    for (unsigned i=0; i<4; i++) {
        if (reg == NV_PGRAPH_TEXCTL0_0+i*4) return pg->ctl[i];
        if (reg == NV_PGRAPH_TEXCTL1_0+i*4) return pg->pitch[i];
        if (reg == NV_PGRAPH_TEXFMT0+i*4) return pg->fmt[i];
        if (reg == NV_PGRAPH_TEXIMAGERECT0+i*4) return pg->rect[i];
    }
    assert(reg == NV_PGRAPH_SHADERPROG);
    return pg->program;
}
static bool pgraph_is_texture_enabled(PGRAPHState *pg, int stage) {
    (void)pg; (void)stage; return true;
}
static bool force_texture_lod0;
/* FUNCTION */
int main(void) {
    const unsigned clamps[] = {0, 1, 15, 255, 4095};
    const unsigned dimensions[][3] = {{1,1,64}, {4,4,8}, {13,7,128}};
    PGRAPHState pg = {0};
    unsigned cases=0, legacy=0;
    for (unsigned format=0; format<ARRAY_SIZE(kelvin_color_format_info_map); format++) {
        BasicColorFormatInfo f=kelvin_color_format_info_map[format];
        if (!f.bytes_per_pixel || !f.linear) continue;
        for (unsigned stage=0; stage<4; stage++)
        for (unsigned raw=0; raw<16; raw++)
        for (unsigned low=0; low<ARRAY_SIZE(clamps); low++)
        for (unsigned high=0; high<ARRAY_SIZE(clamps); high++)
        for (unsigned force=0; force<2; force++)
        for (unsigned border=0; border<2; border++)
        for (unsigned size=0; size<ARRAY_SIZE(dimensions); size++) {
            unsigned w=dimensions[size][0], h=dimensions[size][1], pitch=dimensions[size][2];
            pg.ctl[stage]=clamps[low]<<18 | clamps[high]<<6;
            pg.pitch[stage]=pitch<<16;
            pg.fmt[stage]=2u<<6 | format<<8 | raw<<16 | 15u<<20 | 13u<<24 |
                          (border ? 0 : NV_PGRAPH_TEXFMT0_BORDER_SOURCE);
            pg.rect[stage]=w<<16 | h;
            pg.program=2u<<(stage*5);
            force_texture_lod0=force;
            TextureShape actual=pgraph_get_texture_shape(&pg,stage), expected;
            memset(&expected,0,sizeof(expected));
            expected.dimensionality=2; expected.color_format=format;
            expected.width=w; expected.height=h; expected.depth=1;
            expected.levels=1; expected.pitch=pitch; expected.border=border;
            assert(!memcmp(&actual,&expected,sizeof(actual)));
            cases++;
        }
    }
    /* Swizzled mip chains must still retain their available levels and clamps. */
    for (unsigned raw=1; raw<16; raw++)
    for (unsigned low=0; low<16; low++)
    for (unsigned high=0; high<16; high++)
    for (unsigned dim=2; dim<=3; dim++)
    for (unsigned force=0; force<2; force++) {
        pg.ctl[0]=low<<18 | high<<6;
        pg.fmt[0]=dim<<6 | NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8<<8 |
                  raw<<16 | 4u<<20 | 3u<<24 | 2u<<28 | NV_PGRAPH_TEXFMT0_BORDER_SOURCE;
        pg.program=1;
        force_texture_lod0=force;
        TextureShape s=pgraph_get_texture_shape(&pg,0);
        unsigned levels=MIN(raw,MIN(high+1,dim==3 ? 2u : 5u));
        assert(s.levels==levels && s.width==16 && s.height==8 && s.depth==4 && !s.pitch);
        assert(s.min_mipmap_level==(force ? 0 : MIN(low,levels-1)));
        assert(s.max_mipmap_level==(force ? 0 : MIN(high,levels-1)));
        legacy++;
    }
    unsigned cubes=0;
    for (unsigned raw=1; raw<16; raw++)
    for (unsigned low=0; low<16; low++)
    for (unsigned high=0; high<16; high++) {
        pg.ctl[0]=low<<18 | high<<6;
        pg.fmt[0]=2u<<6 | NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8<<8 |
                  raw<<16 | 4u<<20 | 4u<<24 | NV_PGRAPH_TEXFMT0_CUBEMAPENABLE |
                  NV_PGRAPH_TEXFMT0_BORDER_SOURCE;
        pg.program=3;
        force_texture_lod0=false;
        TextureShape expected=pgraph_get_texture_shape(&pg,0);
        expected.min_mipmap_level=expected.max_mipmap_level=0;
        force_texture_lod0=true;
        TextureShape actual=pgraph_get_texture_shape(&pg,0);
        assert(actual.cubemap && actual.width==16 && actual.height==16);
        assert(!memcmp(&actual,&expected,sizeof(actual)));
        cubes++;
    }
    printf("PASS: %u Force LOD0 cube-chain metadata invariants\n",cubes);
    printf("PASS: %u linear shapes and %u unchanged swizzled shapes\n",cases,legacy);
}
""".replace("/* TYPES */", shape+"\n"+info).replace("/* FORMATS */", formats).replace("/* FUNCTION */", function)
with tempfile.TemporaryDirectory(prefix="linear-shape-") as folder:
    c=Path(folder)/"test.c"
    binary=Path(folder)/"test"
    c.write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else "cc", "-std=gnu11", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
                    "-I", str(repo), str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
