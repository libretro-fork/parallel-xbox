#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production Vulkan base-level color sampler policy and exclusions."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()
source=(repo/"hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
header=(repo/"hw/xbox/nv2a/pgraph/texture.h").read_text()
common=(repo/"hw/xbox/nv2a/pgraph/texture.c").read_text()
constants=(repo/"hw/xbox/nv2a/pgraph/vk/constants.h").read_text()
types="\n".join(re.search(r"typedef struct "+name+r" \{.*?\} "+name+r";",header,re.S).group()
                for name in ("TextureShape","BasicColorFormatInfo"))
formats=re.search(r"^const BasicColorFormatInfo kelvin_color_format_info_map.*?^\};",common,re.S|re.M).group()
filters=re.search(r"static const VkFilter pgraph_texture_min_filter_vk_map\[\].*?\};",constants,re.S).group()
mipmap=re.search(r"    bool mipmap_en =.*?;",source,re.S).group()
nearest=re.search(r"    bool mipmap_nearest =.*?;",source,re.S).group()
predicate=re.search(r"    bool mixed_color_lod0 =.*?;",source,re.S).group()
override=re.search(r"    if \(mixed_g8b8_clamped \|\| mixed_color_lod0\) \{.*?^    \}",source,re.S|re.M).group()
maximum=re.search(r"\.maxLod = (.*?),\n\s+\.mipLodBias",source,re.S).group(1)
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#include "hw/xbox/nv2a/nv2a_regs.h"
/* TYPES */
/* FORMATS */
/* FILTERS */
static bool policy(TextureShape state,unsigned min_filter,unsigned mag_filter,
                   bool supported,bool *out_nearest,float *out_maximum) {
    BasicColorFormatInfo f_basic=kelvin_color_format_info_map[state.color_format];
    unsigned mip_levels=f_basic.linear?1:state.levels;
    VkFilter vk_min_filter=supported?pgraph_texture_min_filter_vk_map[min_filter]:VK_FILTER_NEAREST;
    VkFilter vk_mag_filter=supported?pgraph_texture_min_filter_vk_map[mag_filter]:VK_FILTER_NEAREST;
    bool mixed_g8b8_lod0=false,mixed_g8b8_clamped=false;
    /* MIPMAP */
    /* NEAREST */
    /* PREDICATE */
    /* OVERRIDE */
    *out_nearest=mipmap_nearest;
    *out_maximum=/* MAXIMUM */;
    return mixed_color_lod0;
}
int main(void) {
    unsigned cases=0;
    for(unsigned format=0;format<66;format++) {
        BasicColorFormatInfo f=kelvin_color_format_info_map[format];
        if(!f.bytes_per_pixel)continue;
        for(unsigned min=1;min<=7;min++)
        for(unsigned mag=1;mag<=2;mag++)
        for(unsigned dim=1;dim<=3;dim++)
        for(unsigned cube=0;cube<2;cube++)
        for(unsigned border=0;border<2;border++)
        for(unsigned supported=0;supported<2;supported++)
        for(unsigned level=1;level<=3;level++)
        for(unsigned low=0;low<3;low++)
        for(unsigned high=0;high<3;high++) {
            TextureShape state={0};
            state.color_format=format;state.levels=f.linear?1:level;
            state.dimensionality=dim;state.cubemap=cube;state.border=border;
            state.min_mipmap_level=low;state.max_mipmap_level=high;
            bool nonmip=(min==1||min==2||min==7||f.linear);
            bool fixed_depth=dim==2&&!cube&&
                (format==NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED||
                 format==NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED);
            bool colors=(!f.depth||fixed_depth)&&format!=NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8&&
                        format!=NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8;
            bool different=supported&&pgraph_texture_min_filter_vk_map[min]!=
                                      pgraph_texture_min_filter_vk_map[mag];
            bool base_only=nonmip||(MIN(low,state.levels-1)==0&&MIN(high,state.levels-1)==0);
            bool expected=base_only&&colors&&different&&min<=6&&
                (dim==2||(dim==3&&!f.linear&&!cube))&&(!cube||!f.linear)&&!border;
            bool nearest;float maximum;
            assert(policy(state,min,mag,supported,&nearest,&maximum)==expected);
            assert(nearest==(expected||f.linear||state.levels==1||min==3||min==4));
            assert(maximum==(expected?0.25f:nonmip?0.0f:MIN(high,state.levels-1)));
            cases++;
        }
    }
    printf("PASS: %u base-only color sampler policies, nearest base reads and exclusions\n",cases);
}
""".replace("/* TYPES */",types).replace("/* FORMATS */",formats).replace("/* FILTERS */",filters).replace(
    "/* MIPMAP */",mipmap).replace("/* NEAREST */",nearest).replace("/* PREDICATE */",predicate).replace(
    "/* OVERRIDE */",override).replace("/* MAXIMUM */",maximum)
with tempfile.TemporaryDirectory(prefix="native-lod0-") as folder:
    c=Path(folder)/"test.c";binary=Path(folder)/"test"
    c.write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else "cc","-std=gnu11","-O2",
                    "-Wall","-Wextra","-Werror","-Wno-missing-field-initializers",
                    "-I",str(repo),str(c),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
