#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production clip comparison keys, including sampler-disabled stages."""
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
selection = re.search(r"        unsigned mode =.*?(?=        uint32_t ctl_0)", source, re.S).group()
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
static uint32_t pgraph_reg_r(const uint32_t *pg,unsigned reg) {
 assert(reg==NV_PGRAPH_SHADERCLIPMODE);reads++;return *pg;
}
static void select_key(PshState *state,unsigned i,const uint32_t *pg,bool enabled) {
 /* SELECTION */
 /* The real builder's sampler-enable branch follows this block. */
 if(!enabled)return;
}
int main(void) {
 unsigned cases=0;
 for(unsigned mode=0;mode<32;mode++)for(unsigned i=0;i<4;i++)
 for(unsigned mask=0;mask<16;mask++)for(unsigned enabled=0;enabled<2;enabled++) {
  PshState actual,expected;memset(&actual,0,sizeof(actual));
  actual.shader_stage_program=mode<<(i*5);
  memset(actual.compare_mode,1,sizeof(actual.compare_mode));
  memcpy(&expected,&actual,sizeof(expected));
  for(unsigned j=0;j<4;j++)expected.compare_mode[i][j]=
   mode==PS_TEXTUREMODES_CLIPPLANE&&((mask>>j)&1);
  uint32_t reg=mask<<(i*4);reads=0;
  select_key(&actual,i,&reg,enabled);
  assert(!memcmp(&actual,&expected,sizeof(actual)));
  assert(reads==(mode==PS_TEXTUREMODES_CLIPPLANE?4u:0u));cases++;
 }
 assert(cases==4096);
 printf("PASS: %u clip keys; active comparisons retained with sampler disabled; unused bits cleared\n",cases);
}
""".replace("/* STATE */", state).replace("/* SELECTION */", selection)
with tempfile.TemporaryDirectory(prefix="clipplane-state-") as folder:
    c = Path(folder) / "test.c"
    exe = Path(folder) / "test"
    c.write_text(code)
    subprocess.run([a.cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(r), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
