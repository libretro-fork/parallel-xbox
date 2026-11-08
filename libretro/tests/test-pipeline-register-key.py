#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Only the dynamic stencil identity may differ from the frozen projection."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
current=re.search(r'^static inline void pipeline_reg_values\(.*?^\}',s,re.M|re.S).group()
registers=re.search(r'^static const unsigned pipeline_registers\[\] = \{.*?^\};',s,re.M|re.S).group()
reference=Path(__file__).with_name('pipeline-registers-reference.h').read_text()
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S).group() for name in ('RenderPassState','PipelineKey'))
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/blend.h"
#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define QEMU_BUILD_BUG_ON(x) _Static_assert(!(x),"register count mismatch")
/* TYPES */
typedef struct {bool dyn_cull_front,dyn_blend;uint32_t raw[9];} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;struct {unsigned color_format;} surface_shape;};
/* REGISTERS */
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg){
 for(int i=0;i<ARRAY_SIZE(pipeline_registers);i++)if(pipeline_registers[i]==reg)return pg->vk_renderer_state->raw[i];
 abort();
}
/* FUNCTIONS */
static uint32_t rng=0x29e4881;
static uint32_t random_value(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 unsigned cases=0;
 for(unsigned flags=0;flags<4;flags++)for(unsigned trial=0;trial<50000;trial++){
  PGRAPHVkState r={.dyn_cull_front=flags&1,.dyn_blend=flags&2};
  PGRAPHState pg={.vk_renderer_state=&r,.surface_shape={trial%16}};
  for(unsigned i=0;i<9;i++)r.raw[i]=random_value();
  uint32_t before[9],after[9];reference_reg_values(&pg,before);pipeline_reg_values(&pg,after);
  if(r.dyn_cull_front)before[2]&=~NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE;
  if(memcmp(before,after,sizeof(before)))goto fail;
  r.raw[2]^=NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE;
  uint32_t toggled[9];pipeline_reg_values(&pg,toggled);
  if((after[2]^toggled[2])!=(r.dyn_cull_front?0:NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE))goto fail;
  for(unsigned i=0;i<9;i++)if(i!=2 && after[i]!=toggled[i])goto fail;
  cases++;continue;
fail:fprintf(stderr,"FAIL register-key flags=%u trial=%u\n",flags,trial);return 1;
 }
 printf("Pipeline register keys: %u frozen-reference/random/static/dynamic cases PASS\n",cases);
}
'''.replace('/* TYPES */',types).replace('/* REGISTERS */',registers).replace('/* FUNCTIONS */',reference+'\n'+current)
with tempfile.TemporaryDirectory(prefix='pipeline-register-key-') as folder:
 p=Path(folder);(p/'qemu').mkdir()
 (p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
 (p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 command=[cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
          '-I'+str(p),'-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),str(c),'-o',str(exe)]
 c.write_text(code);subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
 mutant=reference.replace('reference_reg_values(', 'pipeline_reg_values(',1)
 c.write_text(code.replace(current,mutant,1));subprocess.run(command,check=True)
 bad=subprocess.run([str(exe)],capture_output=True)
 assert bad.returncode==1 and b'FAIL register-key' in bad.stderr
 print('Required negative control rejected: frozen redundant dynamic stencil identity')
