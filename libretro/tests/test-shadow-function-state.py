#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check shadow-function key selection from production state-builder blocks."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("checkout", type=Path)
p.add_argument("--cc", default="gcc")
a = p.parse_args()
r = a.checkout.resolve()
source = (r / "hw/xbox/nv2a/pgraph/glsl/psh.c").read_text()
initial = re.search(r"    state->shadow_depth_func = SHADOW_DEPTH_FUNC_NEVER;", source).group()
predicate = re.search(r"        shadow_comparison \|=.*?;", source, re.S).group()
selection = re.search(r"    if \(shadow_comparison\) \{.*?\n    \}", source, re.S).group()
state = re.search(r"typedef struct PshState \{.*?\} PshState;", (r / "hw/xbox/nv2a/pgraph/glsl/psh.h").read_text(), re.S).group()
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/psh_regs.h"
enum PshDepthFormat { DEPTH_DISABLED };
/* STATE */
static unsigned reads;
static unsigned ctz32(uint32_t x) {return __builtin_ctz(x);}
static uint32_t pgraph_reg_r(const uint32_t *pg,unsigned reg) {
 assert(reg==NV_PGRAPH_SHADOWCTL);reads++;return *pg;
}
static void select_key(PshState *state,unsigned enabled,const uint32_t *pg) {
 /* INITIAL */
 bool shadow_comparison=false;
 for(unsigned i=0;i<4;i++) {
  if(!(enabled&(1u<<i)))continue;
  unsigned mode=(state->shader_stage_program>>(i*5))&31;
  /* PREDICATE */
 }
 /* SELECTION */
}
int main(void) {
 unsigned cases=0;
 for(unsigned mode=0;mode<32;mode++)for(unsigned enabled=0;enabled<16;enabled++)
 for(unsigned shadow=0;shadow<16;shadow++)for(unsigned func=0;func<8;func++) {
  PshState actual,expected;memset(&actual,0,sizeof(actual));bool needed=false;
  for(unsigned i=0;i<4;i++) {
   unsigned m=(mode+i)%32;actual.shader_stage_program|=m<<(i*5);
   actual.shadow_map[i]=((shadow>>i)&1)?1:0;
   if((enabled&(1u<<i))&&(shadow&(1u<<i))&&
      (m==PS_TEXTUREMODES_PROJECT2D||m==PS_TEXTUREMODES_PROJECT3D))needed=true;
  }
  actual.shadow_depth_func=SHADOW_DEPTH_FUNC_ALWAYS;
  memcpy(&expected,&actual,sizeof(expected));
  expected.shadow_depth_func=needed?func:SHADOW_DEPTH_FUNC_NEVER;
  uint32_t reg=func<<ctz32(NV_PGRAPH_SHADOWCTL_SHADOW_ZFUNC);reads=0;
  select_key(&actual,enabled,&reg);
  assert(!memcmp(&actual,&expected,sizeof(actual)));
  assert(reads==(needed?1u:0u));cases++;
 }
 assert(cases==65536);
 printf("PASS: %u shadow-function keys; active functions retained; unused register reads omitted\n",cases);
}
""".replace("/* STATE */", state).replace("/* INITIAL */", initial).replace("/* PREDICATE */", predicate).replace("/* SELECTION */", selection)
with tempfile.TemporaryDirectory(prefix="shadow-function-state-") as folder:
    c = Path(folder) / "test.c"
    exe = Path(folder) / "test"
    c.write_text(code)
    subprocess.run([a.cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(r), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
