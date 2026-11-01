#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify F16 shader-key ABI and the actual GPU decoder for every code."""
import argparse
import ast
from contextlib import nullcontext
from pathlib import Path
import re
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument("checkout",type=Path)
p.add_argument("--cc",default="cc")
p.add_argument("--gpu",action="store_true")
p.add_argument("--output",type=Path)
a=p.parse_args();r=a.checkout.resolve()
header=(r/"hw/xbox/nv2a/pgraph/glsl/psh.h").read_text()
state=re.search(r"typedef struct PshState \{.*?\} PshState;",header,re.S).group()
legacy=state.replace("uint8_t shadow_map[4]","bool shadow_map[4]").replace("PshState","LegacyPshState")
source=(r/"hw/xbox/nv2a/pgraph/glsl/psh.c").read_text()
selection=re.search(r"        if \(color_format == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT &&.*?^        \}",source,re.M|re.S).group()
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
enum ConvolutionFilter { CONV_DISABLED };
enum PshDepthFormat { DEPTH_DISABLED };
enum PshShadowDepthFunc { SHADOW_DISABLED };
/* STATE */
_Static_assert(sizeof(PshState)==sizeof(LegacyPshState),"shader seed size changed");
_Static_assert(offsetof(PshState,shadow_map)==offsetof(LegacyPshState,shadow_map),"shadow offset changed");
_Static_assert(offsetof(PshState,shadow_depth_func)==offsetof(LegacyPshState,shadow_depth_func),"following field moved");
_Static_assert(offsetof(PshState,depth_format)==offsetof(LegacyPshState,depth_format),"shader tail moved");
static unsigned key(unsigned color_format,unsigned min_filter,unsigned mag_filter,unsigned dim,bool cubemap,unsigned i,bool enabled) {
 PshState value={0},*state=&value;
 state->dim_tex[i]=dim;state->shadow_map[i]=1;
#define pgraph_is_texture_enabled(pg,i) enabled
 /* SELECTION */
 return state->shadow_map[i];
}
int main(void) {
 unsigned cases=0;
 for(unsigned format=0;format<66;format++)
 for(unsigned min=1;min<=7;min++)
 for(unsigned mag=1;mag<=4;mag++) {
  if(mag==3)continue;
  for(unsigned dim=1;dim<=3;dim++)
  for(unsigned cube=0;cube<2;cube++)
  for(unsigned i=0;i<4;i++) {
   unsigned k=key(format,min,mag,dim,cube,i,true);
   if(format!=0x31||dim!=2||cube)assert(k==1);
   else {
    assert((k&3)==3);
    assert(!!(k&4)==(min==2||min==4||min==6||min==7));
    assert(!!(k&8)==(mag==2||mag==4));
   }
   cases++;
  }
 }
 for(unsigned min=1;min<=7;min++)
 for(unsigned mag=1;mag<=4;mag++)if(mag!=3)
 for(unsigned i=0;i<4;i++)assert(key(0x31,min,mag,2,false,i,false)==1);
 for(unsigned mask=0;mask<16;mask++) {
  PshState current={0};LegacyPshState old={0};
  for(unsigned i=0;i<4;i++)current.shadow_map[i]=old.shadow_map[i]=!!(mask&(1u<<i));
  assert(!memcmp(&current,&old,sizeof(current)));
 }
 printf("PASS: %u F16 filter/source-independent keys; 84 disabled F16 keys, fixed-depth bytes and shader-seed ABI unchanged\n",cases);
}
""".replace("/* STATE */",state+"\n"+legacy).replace("/* SELECTION */",selection)
if a.output:a.output.mkdir(parents=True,exist_ok=True)
with nullcontext(a.output) if a.output else tempfile.TemporaryDirectory(prefix="f16-codec-") as folder:
 folder=Path(folder);c=folder/"keys.c";exe=folder/"keys";c.write_text(code)
 subprocess.run([a.cc,"-std=gnu11","-O2","-Wall","-Wextra","-Werror","-I",str(r),str(c),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 if a.gpu:
  helper=re.search(r"^static void psh_append_f16_shadow\(.*?^\}",source,re.M|re.S).group()
  text="".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',helper))
  decoder=text[text.index(" uvec4 fdBits"):text.index("} else { fdValue")]
  shader=folder/"decode.comp";spv=folder/"decode.spv"
  shader.write_text("#version 450\nlayout(local_size_x=64) in;\nlayout(set=0,binding=0,std430) buffer Out {float values[];};\nvoid main(){\nuvec4 codes=gl_GlobalInvocationID.x*4u+uvec4(0u,1u,2u,3u);\nvec4 fdRaw=vec4(codes)/65535.0;vec4 fdValue;\n"+decoder+"\nfor(int i=0;i<4;i++)values[codes[i]]=fdValue[i];\n}\n")
  subprocess.run(["glslc",str(shader),"-o",str(spv)],check=True)
  exe=folder/"gpu"
  subprocess.run([a.cc,"-std=gnu11","-O2","-Wall","-Wextra","-Werror",str(Path(__file__).with_name("test-float-depth-codec-vulkan.c")),"-lvulkan","-lm","-o",str(exe)],check=True)
  subprocess.run([str(exe),str(spv)],check=True,timeout=90)
