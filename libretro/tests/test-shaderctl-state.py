#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production fragment-register keys against generator field decoding."""
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
selection = re.search(r"    state->other_stage_input =.*?;", source, re.S).group()
selection += "\n" + re.search(r"    state->shader_stage_program =.*?;", source, re.S).group()
selection += "\n" + re.search(r"    state->combiner_control = pgraph_reg_r.*?;", source, re.S).group()
uniform = re.search(r"        values->combCtl\[0\] =.*?;", source, re.S).group()
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
static uint32_t pgraph_reg_r(const uint32_t *pg,unsigned reg) {
 assert(reg==NV_PGRAPH_SHADERCTL||reg==NV_PGRAPH_SHADERPROG||reg==NV_PGRAPH_COMBINECTL);return *pg;
}
static void select_key(PshState *state,const uint32_t *pg) {
 /* SELECTION */
}
typedef struct {uint32_t combCtl[1];} TestUniform;
static void fill_uniform(TestUniform *values,const uint32_t *pg) {
 /* UNIFORM */
}
static void check(uint32_t raw) {
 TestUniform values={0};fill_uniform(&values,&raw);assert(values.combCtl[0]==raw);
 PshState actual,expected;memset(&actual,0,sizeof(actual));
 actual.shader_stage_program=UINT32_MAX;actual.other_stage_input=UINT32_MAX;actual.combiner_control=UINT32_MAX;actual.final_inputs_0=0xdeadbeef;
 memcpy(&expected,&actual,sizeof(expected));expected.other_stage_input=0;expected.shader_stage_program=0;expected.combiner_control=raw&255u;
 /* Independently rebuild fields decoded by the generator. */
 for(unsigned i=0;i<3;i++)expected.other_stage_input|=((raw>>(i*4))&15u)<<(i*4);
 for(unsigned i=0;i<2;i++)expected.other_stage_input|=((raw>>(16+i*4))&15u)<<(16+i*4);
 for(unsigned i=0;i<4;i++)expected.shader_stage_program|=((raw>>(i*5))&31u)<<(i*5);
 const unsigned flags[]={8,12,16};
 for(unsigned i=0;i<3;i++)expected.combiner_control|=((raw>>flags[i])&1u)<<flags[i];
 select_key(&actual,&raw);assert(!memcmp(&actual,&expected,sizeof(actual)));
}
int main(void) {
 unsigned cases=0;check(0);check(UINT32_MAX);cases+=2;
 for(unsigned bit=0;bit<32;bit++){check(1u<<bit);check(~(1u<<bit));cases+=2;}
 uint32_t random=0x91dcb175;
 for(unsigned i=0;i<65536;i++) {
  random^=random<<13;random^=random>>17;random^=random<<5;check(random);cases++;
 }
 assert(cases==65602);printf("PASS: %u SHADERCTL/SHADERPROG/COMBINECTL key triples; all decoded fields preserved; ignored bits cleared\n",cases);
}
""".replace("/* STATE */", state).replace("/* SELECTION */", selection).replace("/* UNIFORM */", uniform)
with tempfile.TemporaryDirectory(prefix="shaderctl-state-") as folder:
    c = Path(folder) / "test.c"
    exe = Path(folder) / "test"
    c.write_text(code)
    subprocess.run([a.cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(r), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
