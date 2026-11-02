# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production Vulkan clear-upload gate and state changes."""
from pathlib import Path
import os, subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve()
source = (root / 'hw/xbox/nv2a/pgraph/vk/surface.c').read_text()
a = source.index('    /* A complete attachment clear')
b = source.index('    nv2a_profile_inc_counter(NV2A_PROF_SURF_UPLOAD);', a)
code = r'''
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/surface-clear.h"
#define VK_IMAGE_ASPECT_STENCIL_BIT 4
#define ctz32(v) __builtin_ctz(v)
#define NV2A_PROF_SURF_UPLOAD_SKIPPED_CLEAR 1
typedef struct {bool color,upload_pending,initialized; unsigned width,height,draw_time;struct {unsigned aspect;}host_fmt;} Surface;
typedef struct {Surface *color_binding,*zeta_binding;unsigned clear_parameter;} Renderer;
typedef struct {bool clearing;unsigned draw_time;struct {unsigned width,height;}surface_binding_dim;struct {unsigned clip_x,clip_y,anti_aliasing;}surface_shape;} Graph;
static unsigned count,rectx,recty;
static unsigned pgraph_reg_r(Graph *pg,unsigned reg){(void)pg;return reg==NV_PGRAPH_CLEARRECTX?rectx:recty;}
static void nv2a_profile_inc_counter(unsigned c){assert(c==1);count++;}
static void gate(Graph *pg,Renderer *r,Surface *surface,bool force){
/* GATE */
}
int main(void){
 unsigned cases=0;
 for(unsigned bits=0;bits<1024;bits++)for(unsigned mask=0;mask<4;mask++){
  bool color=bits&1,stencil=bits&2;
  Surface s={.color=color,.upload_pending=true,.width=64,.height=32,.host_fmt={stencil?4:0}};
  Surface other={.width=(bits&4)?63:64,.height=32};
  Renderer r={.color_binding=color?&s:((bits&8)?&other:NULL),.zeta_binding=color?NULL:&s,
    .clear_parameter=color?NV097_CLEAR_SURFACE_COLOR:((mask&1?NV097_CLEAR_SURFACE_Z:0)|(mask&2?NV097_CLEAR_SURFACE_STENCIL:0))};
  Graph pg={.clearing=!(bits&16),.draw_time=77,.surface_binding_dim={64,32},.surface_shape={bits&32?1:0,bits&64?1:0,bits&128?(NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1+1):NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1}};
  bool force=bits&256,partial=bits&512;
  rectx=partial?62u<<16:63u<<16;recty=31u<<16;count=0;
  bool expected=!force&&pg.clearing&&!pg.surface_shape.clip_x&&!pg.surface_shape.clip_y&&!(bits&128)&&!partial&&
    (color||!(bits&8)||!(bits&4))&&(color||((mask&1)&&(!stencil||(mask&2))));
  gate(&pg,&r,&s,force);
  assert(count==(unsigned)expected&&s.initialized==expected&&s.upload_pending==!expected&&s.draw_time==(expected?77u:0u));cases++;
 }
 printf("Vulkan clear upload: %u production gate/state schedules passed\n",cases);
}
'''.replace('/* GATE */', source[a:b])
cc = sys.argv[2]
with tempfile.TemporaryDirectory(prefix='vk-clear-upload-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/'test.exe'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(root),str(d/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
