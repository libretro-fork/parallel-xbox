#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check active convolution keys and canonicalize unused kernel settings."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("checkout",type=Path)
p.add_argument("--cc",default="gcc")
a=p.parse_args();r=a.checkout.resolve()
source=(r/"hw/xbox/nv2a/pgraph/glsl/psh.c").read_text()
selection=re.search(r"        unsigned mode =.*?        state->conv_tex\[i\] = kernel;",source,re.S).group()
state=re.search(r"typedef struct PshState \{.*?\} PshState;",(r/"hw/xbox/nv2a/pgraph/glsl/psh.h").read_text(),re.S).group()
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/psh_regs.h"
enum PshDepthFormat { DEPTH_DISABLED };
/* STATE */
static unsigned ctz32(uint32_t x) {return __builtin_ctz(x);}
static PshState key(unsigned min,unsigned mag,unsigned k,unsigned i,unsigned texture_mode,bool shadow) {
 PshState value={0},*state=&value;
 state->shader_stage_program=texture_mode<<(i*5);state->shadow_map[i]=shadow;
 unsigned filter=(min<<16)|(mag<<24)|(k<<13),min_filter=min;
 /* SELECTION */
 return value;
}
int main(void) {
 assert(CONVOLUTION_FILTER_DISABLED==0);
 assert(CONVOLUTION_FILTER_QUINCUNX==5&&CONVOLUTION_FILTER_GAUSSIAN==6);
 assert(sizeof(enum ConvolutionFilter)==sizeof(int));
 unsigned cases=0;
 for(unsigned mode=0;mode<32;mode++)for(unsigned shadow=0;shadow<2;shadow++)
 for(unsigned i=0;i<4;i++)for(unsigned min=1;min<=7;min++)
 for(unsigned mag=1;mag<=4;mag++)if(mag!=3)for(unsigned k=1;k<=2;k++) {
  PshState actual=key(min,mag,k,i,mode,shadow),baseline={0};
  baseline.shader_stage_program=mode<<(i*5);baseline.shadow_map[i]=shadow;
  if(min==7&&mode==PS_TEXTUREMODES_PROJECT2D&&!shadow){assert(actual.conv_tex[i]==(k==1?5:6));actual.conv_tex[i]=0;}
  assert(!memcmp(&actual,&baseline,sizeof(actual)));cases++;
 }
 printf("PASS: %u kernel/filter/stage keys; unused convolution keys collapse; active convolution and other stage bytes unchanged\n",cases);
}
""".replace("/* STATE */",state).replace("/* SELECTION */",selection)
with tempfile.TemporaryDirectory(prefix="convolution-state-") as folder:
 c=Path(folder)/"test.c";exe=Path(folder)/"test";c.write_text(code)
 subprocess.run([a.cc,"-std=gnu11","-O2","-Wall","-Wextra","-Werror","-I",str(r),str(c),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
