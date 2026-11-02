# SPDX-License-Identifier: LGPL-2.1-or-later
"""Production upscaled-UI selection, cache layout and scale invalidation checks."""
import argparse, os, re, subprocess, tempfile, shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('--cc',default='cc');a=p.parse_args();r=a.checkout.resolve()
s=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/glsl/psh.h').read_text()
def function(name):
 m=re.search(r'^static bool '+name+r'\(',s,re.M);assert m
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
geom=(r/'hw/xbox/nv2a/pgraph/glsl/geom.h').read_text()
geom_types=re.search(r'enum ShaderPolygonMode \{.*?\};',(r/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text(),re.S)[0]+'\n'+re.search(r'typedef struct \{.*?\} GeomState;',geom,re.S)[0]+'\n'+re.search(r'typedef struct GenGeomGlslOptions \{.*?\} GenGeomGlslOptions;',geom,re.S)[0]
struct=re.search(r'typedef struct PshState \{.*?\} PshState;',h,re.S)[0]
enum=re.search(r'enum PshDepthFormat \{.*?\};',h,re.S)[0]
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/psh_regs.h"
/* PRIMITIVES */

/* TYPES */
/* PRODUCTION */
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef struct {unsigned reserved_surface_scale;} PGRAPHVkState;
typedef struct {unsigned surface_scale_factor;PGRAPHVkState *vk_renderer_state;unsigned long regs_dirty[2048];} PGRAPHState;
static struct {struct {struct {int surface_scale;}quality;}display;}g_config;
static unsigned marks;
static void bitmap_set(unsigned long *map,unsigned bit,unsigned n){(void)map;assert(bit==NV_PGRAPH_CONTROL_0/sizeof(uint32_t)&&n==1);marks++;}
/* SCALE */
int main(int argc,char **argv){
 assert(argc==2);(void)argv;unsigned cases=0;
 assert(sizeof(PshState)==264&&offsetof(PshState,depth_format)==260);
 typedef struct {GeomState state;struct {bool vulkan,locations;}opts;}OldKey;
 typedef struct {GeomState state;GenGeomGlslOptions opts;}NewKey;
 assert(sizeof(GeomState)==20&&offsetof(GenGeomGlslOptions,ui_clamp_stage)==2);
 assert(sizeof(OldKey)==24&&sizeof(NewKey)==sizeof(OldKey));
 for(unsigned vulkan=0;vulkan<2;vulkan++)for(unsigned locations=0;locations<2;locations++){
  OldKey old={0};NewKey now={0};old.opts.vulkan=now.opts.vulkan=vulkan;old.opts.locations=now.opts.locations=locations;
  assert(!memcmp(&old,&now,sizeof(old)));
 }
 for(unsigned scale=1;scale<=8;scale++)for(unsigned primitive=0;primitive<=10;primitive++)
 for(unsigned depth=0;depth<2;depth++)for(unsigned mode=0;mode<32;mode++)
 for(unsigned stage=0;stage<4;stage++){
  PshState state={0};state.shader_stage_program=mode<<(5*stage);state.dim_tex[stage]=2;
  bool expected=scale>1&&primitive>=PRIM_TYPE_TRIANGLES&&primitive<=PRIM_TYPE_QUAD_STRIP&&!depth&&mode==PS_TEXTUREMODES_PROJECT2D;
  assert(psh_ui_clamp_eligible(&state,scale,primitive,depth?NV_PGRAPH_CONTROL_0_ZENABLE:0)==expected);cases++;
  assert(!pgraph_glsl_ui_clamp_stage(&state));
  if(expected){state.depth_format|=DEPTH_FORMAT_UI_CLAMP;assert(pgraph_glsl_ui_clamp_stage(&state)==stage+1);}
 }
 PshState good={.shader_stage_program=PS_TEXTUREMODES_PROJECT2D,.dim_tex={2}};
 assert(psh_ui_clamp_eligible(&good,8,PRIM_TYPE_TRIANGLE_STRIP,0));
 for(unsigned stage=0;stage<4;stage++)for(unsigned kind=0;kind<7;kind++){
  PshState x=good;x.shader_stage_program|=PS_TEXTUREMODES_PROJECT2D<<(5*stage);x.dim_tex[stage]=2;
  switch(kind){case 0:x.point_sprite=true;break;case 1:x.dim_tex[stage]=3;break;case 2:x.tex_cubemap[stage]=true;break;
  case 3:x.shadow_map[stage]=1;break;case 4:x.border_adjust[stage]=true;break;case 5:x.conv_tex[stage]=CONVOLUTION_FILTER_GAUSSIAN;break;case 6:x.shader_stage_program|=PS_TEXTUREMODES_CUBEMAP<<(5*stage);break;}
  assert(!psh_ui_clamp_eligible(&x,8,PRIM_TYPE_TRIANGLES,0));cases++;
 }
 for(unsigned depth=0;depth<4;depth++){
  PshState old={0},new={0};old.depth_format=depth;new=old;
  assert(!memcmp(&old,&new,sizeof(old)));new.depth_format|=DEPTH_FORMAT_UI_CLAMP;
  assert((new.depth_format&DEPTH_FORMAT_MASK)==depth);
  assert(!memcmp(&old,&new,offsetof(PshState,depth_format)));
 }
 printf("PASS: %u production UI selection cases; legacy key layout, depth bits and exclusions\n",cases);
 for(unsigned old=0;old<=8;old++)for(int factor=-2;factor<=10;factor++){
  PGRAPHVkState vk={.reserved_surface_scale=4};PGRAPHState pg={.surface_scale_factor=old,.vk_renderer_state=&vk};
  g_config.display.quality.surface_scale=factor;marks=0;pgraph_gl_reload_surface_scale_factor(&pg);
  unsigned expected=factor<1?1:factor;assert(pg.surface_scale_factor==expected&&marks==((old>1)!=(expected>1)));
  pg.surface_scale_factor=old;marks=0;pgraph_vk_reload_surface_scale_factor(&pg);expected=MIN(MAX(factor,1),4);
  assert(pg.surface_scale_factor==expected&&marks==((old>1)!=(expected>1)));
 }
 puts("PASS: 234 actual GL/Vulkan scale-boundary invalidation and reservation-cap cases");
 unsigned filters=0;
 for(unsigned linear=0;linear<2;linear++)for(unsigned levels=0;levels<=8;levels++)
 for(unsigned min=0;min<8;min++)for(unsigned mag=0;mag<4;mag++)for(unsigned forced=0;forced<2;forced++){
  bool matched=(mag==1&&(min==1||min==3||min==5))||(mag==2&&(min==2||min==4||min==6));
  bool expected=matched&&(linear||levels<=1||forced||min==1||min==2);
  assert(psh_ui_filter_supported(linear,levels,min,mag,forced)==expected);filters++;
 }
 printf("PASS: %u fixed-mip and matched-filter combinations\n",filters);

 return 0;
}
""".replace('/* PRIMITIVES */',re.search(r'enum ShaderPrimitiveMode \{.*?\};',(r/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text(),re.S)[0]).replace('/* TYPES */',enum+'\n'+struct+'\n'+geom_types).replace('/* PRODUCTION */',function('psh_ui_clamp_eligible')+'\n'+function('psh_ui_filter_supported')+'\n'+re.search(r'static inline unsigned pgraph_glsl_ui_clamp_stage\(.*?\n\}',h,re.S)[0])
scales=[]
for backend in ('gl','vk'):
 text=(r/f'hw/xbox/nv2a/pgraph/{backend}/surface.c').read_text();start=text.index('void pgraph_'+backend+'_reload_surface_scale_factor(');end=text.index('\n}',start)+2;scales.append(text[start:end])
code=code.replace('/* SCALE */','\n'.join(scales))
env=dict(os.environ)
if Path(a.cc).is_absolute():env['PATH']=str(Path(a.cc).parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='scaled-ui-') as temp:
 out=Path(temp);src=out/'test.c';src.write_text(code);exe=out/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-format-extra-args','-Wno-missing-field-initializers','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),str(src),'-lm','-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe),str(out)],check=True,env=env)
