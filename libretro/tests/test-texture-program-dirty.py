#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the complete production shader dirty check with masked writes."""
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
source = (r / "hw/xbox/nv2a/pgraph/glsl/shaders.c").read_text()
functions = "\n".join(re.search(r"^(?:static )?bool " + name + r"\(.*?^\}", source, re.S | re.M).group()
                      for name in ("shader_reg_dirty", "texture_program_dirty", "signed_g8b8_address_dirty", "pgraph_glsl_check_shader_state_dirty"))
header = (r / "hw/xbox/nv2a/pgraph/glsl/shaders.h").read_text()
masks = "\n".join(re.search(r"#define " + name + r" .*?(?<!\\)\n", header, re.S).group()
                  for name in ("NV_PGRAPH_SHADERCTL_SHADER_FIELDS", "NV_PGRAPH_SHADERPROG_SHADER_FIELDS", "NV_PGRAPH_SETUPRASTER_SHADER_FIELDS", "NV_PGRAPH_TEXFMT0_SHADER_FIELDS", "NV_PGRAPH_TEXFILTER0_SHADER_FIELDS"))
state = re.search(r"typedef struct PshState \{.*?\} PshState;", (r / "hw/xbox/nv2a/pgraph/glsl/psh.h").read_text(), re.S).group()
other_regs = re.search(r"    unsigned int regs\[\] = \{.*?    \};", source, re.S).group()
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/psh_regs.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
enum PshDepthFormat { DEPTH_DISABLED };
/* MASKS */
/* STATE */
typedef struct {
 struct {
  bool is_fixed_function;unsigned uniform_attrs,swizzle_attrs,compressed_attrs;
  struct {bool csv0c_dynamic,lighting,two_sided,texgen_dynamic,texture_matrix_enable[4];} fixed_function;
 } vsh;
 struct {unsigned primitive_mode;} geom;
 PshState psh;
} ShaderState;
typedef struct {
 bool program_data_dirty,two_side_light_en,texture_matrix_enable[4];
 unsigned uniform_attrs,swizzle_attrs,compressed_attrs,primitive_mode;
 struct {unsigned zeta_format;} surface_shape;
 uint32_t registers[0x4000/4],shader_reg_snap[9];bool dirty[0x4000/4];
} PGRAPHState;
static unsigned reads;
static unsigned ctz32(uint32_t x) {return __builtin_ctz(x);}
static bool pgraph_is_reg_dirty(PGRAPHState *pg,unsigned reg) {
 assert(!(reg&3)&&reg<0x4000);return pg->dirty[reg/4];
}
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg) {
 assert(!(reg&3)&&reg<0x4000);
 if(reg==NV_PGRAPH_SHADERCTL||reg==NV_PGRAPH_SHADERPROG)reads++;
 return pg->registers[reg/4];
}
/* FUNCTIONS */
int main(void) {
 unsigned cases=0;uint32_t random=0x12345678;
 const unsigned target[]={NV_PGRAPH_SHADERCTL,NV_PGRAPH_SHADERPROG};
 const uint32_t live[]={0x00ff0fffu,0x000fffffu};
 for(unsigned n=0;n<256;n++)for(unsigned reg=0;reg<2;reg++)
 for(unsigned bit=0;bit<32;bit++)for(unsigned dirty=0;dirty<2;dirty++) {
  random^=random<<13;random^=random>>17;random^=random<<5;
  PGRAPHState pg={0};ShaderState state={0};
  /* Exercise dynamic siblings too: these fields must remain significant. */
  state.psh.combiner_dynamic=n&1;state.psh.alpha_dynamic=n&1;
  pg.registers[target[reg]/4]=random^(1u<<bit);pg.dirty[target[reg]/4]=dirty;
  if(reg==0)state.psh.other_stage_input=random&live[reg];
  else state.psh.shader_stage_program=random&live[reg];
  PGRAPHState saved=pg;reads=0;
  bool expected=dirty&&((1u<<bit)&live[reg]);
  assert(pgraph_glsl_check_shader_state_dirty(&pg,&state)==expected);
  assert(!memcmp(&pg,&saved,sizeof(pg)));if(!dirty)assert(!reads);
  /* A live write restored before a draw must reuse the bound state too. */
  pg.registers[target[reg]/4]=random;pg.dirty[target[reg]/4]=true;
  assert(!pgraph_glsl_check_shader_state_dirty(&pg,&state));
  pg.program_data_dirty=true;
  assert(pgraph_glsl_check_shader_state_dirty(&pg,&state));cases++;
 }
 assert(cases==32768);
 /* Every register still in the unconditional list retains invalidation. */
 /* OTHER_REGS */
 for(unsigned i=0;i<ARRAY_SIZE(regs);i++) {
  PGRAPHState pg={0};ShaderState state={0};pg.dirty[regs[i]/4]=true;
  pg.dirty[NV_PGRAPH_SHADERCTL/4]=true;
  pg.registers[NV_PGRAPH_SHADERCTL/4]=0xff00f000u;
  assert(pgraph_glsl_check_shader_state_dirty(&pg,&state));
 }
 /* An ignored write to one register must not hide a live write to the other. */
 for(unsigned reg=0;reg<2;reg++) {
  PGRAPHState pg={0};ShaderState state={0};
  for(unsigned j=0;j<2;j++){pg.dirty[target[j]/4]=true;pg.registers[target[j]/4]=~live[j];}
  assert(!pgraph_glsl_check_shader_state_dirty(&pg,&state));
  pg.registers[target[reg]/4]|=1u;
  assert(pgraph_glsl_check_shader_state_dirty(&pg,&state));
 }
 /* Noncanonical old seed states conservatively rebuild, rather than alias. */
 for(unsigned reg=0;reg<2;reg++) {
  PGRAPHState pg={0};ShaderState state={0};pg.dirty[target[reg]/4]=true;
  if(reg==0)state.psh.other_stage_input=~live[reg];
  else state.psh.shader_stage_program=~live[reg];
  assert(pgraph_glsl_check_shader_state_dirty(&pg,&state));
 }
 printf("PASS: %u masked-write schedules, restored writes, dynamic siblings, dirty-map preservation and retained invalidators\n",cases);
}
""".replace("/* MASKS */", masks).replace("/* STATE */", state).replace("/* FUNCTIONS */", functions).replace("/* OTHER_REGS */", other_regs)
with tempfile.TemporaryDirectory(prefix="texture-program-dirty-") as folder:
    c = Path(folder) / "test.c"
    exe = Path(folder) / "test"
    c.write_text(code)
    subprocess.run([a.cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-I", str(r), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
