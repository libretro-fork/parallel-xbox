#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production per-unit binding knowledge without a GL driver."""
from pathlib import Path
import re,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(root/'hw/xbox/nv2a/pgraph/gl/texture.c').read_text()
functions='\n'.join(re.search(r'^void '+name+r'\(.*?^\}',s,re.M|re.S).group() for name in ('pgraph_gl_tex_shadow_invalidate','pgraph_gl_tex_shadow_reset'))
start=s.index('        if (!enabled) {')
disabled=s[start:s.index('        glActiveTexture(GL_TEXTURE0 + i);',s.index('            continue;\n        }',start))]
start=s.index('                TextureBinding *tb = r->texture_binding[i];')
reused=s[start:s.index('                apply_texture_parameters(',start)]
start=s.index('            if (r->texture_binding[i]->gl_target != binding->gl_target)')
switch=s[start:s.index('            texture_binding_destroy(',start)]
surface=(root/'hw/xbox/nv2a/pgraph/gl/surface.c').read_text()
scale=re.search(r'^void pgraph_gl_set_surface_scale_factor\(.*?^\}',surface,re.M|re.S).group()
a=surface.index('void pgraph_gl_surface_flush(')
flush=surface[a:surface.index('    bool update_surface',a)]+'}\n'
flush=flush.replace('void pgraph_gl_surface_flush(', 'static void owner_flush(',1)
fixture=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define NV2A_MAX_TEXTURES 4
#define GL_TEXTURE0 100
#define GL_TEXTURE_CUBE_MAP 0
#define GL_TEXTURE_1D 1
#define GL_TEXTURE_2D 2
#define GL_TEXTURE_3D 3
typedef unsigned GLuint;
typedef unsigned GLenum;
typedef struct {GLuint gl_texture;GLenum gl_target;} TextureBinding;
typedef struct {GLuint tex_shadow_name[4];GLenum tex_shadow_target[4];uint8_t tex_shadow_valid_mask;TextureBinding *texture_binding[4];int download_dirty_surfaces_pending,dirty_surfaces_download_complete;} PGRAPHGLState;
static GLuint driver[4][4];static unsigned active,binds;
static void glActiveTexture(unsigned unit){CHECK(unit>=100 && unit<104);active=unit-100;}
static void glBindTexture(GLenum target,GLuint name){CHECK(target<4);driver[active][target]=name;binds++;}
static bool owner_strict,on_render;
#define pgraph_gl_tex_shadow_invalidate production_invalidate
FUNCTIONS
#undef pgraph_gl_tex_shadow_invalidate
static void pgraph_gl_tex_shadow_invalidate(PGRAPHGLState *r){CHECK(!owner_strict || on_render);production_invalidate(r);}
typedef struct {PGRAPHGLState *gl_renderer_state;int flush_pending,flush_complete;} PGRAPHState;
typedef struct {PGRAPHState pgraph;struct {int halt;} pfifo;} NV2AState;
static struct {struct {struct {unsigned surface_scale;} quality;} display;} g_config;
static NV2AState *owner_device;static unsigned owner_kicks;
OWNER_FLUSH
static void retro_atomic_store_relaxed_int(int *p,int value){*p=value;}
static void pfifo_kick(NV2AState *d){CHECK(d==owner_device && !on_render);owner_kicks++;}
static void pgraph_wait_until_clear(int *completion,int *pending){
 CHECK(owner_device && !on_render && owner_device->pfifo.halt && *pending==1);
 PGRAPHGLState *r=owner_device->pgraph.gl_renderer_state;CHECK(r->tex_shadow_valid_mask==15);
 if(pending==&owner_device->pgraph.flush_pending){
  CHECK(completion==&owner_device->pgraph.flush_complete);on_render=true;owner_flush(owner_device);on_render=false;CHECK(!r->tex_shadow_valid_mask);
 }else CHECK(pending==&r->download_dirty_surfaces_pending && completion==&r->dirty_surfaces_download_complete);
 *pending=0;
}
SCALE
static unsigned count_bind(unsigned mask,unsigned unknown){
 unsigned n=0;for(unsigned i=0;i<4;i++)if((unknown>>i)&1)n+=((mask>>i)&1)?1:4;return n;
}
static void draw_bind(PGRAPHGLState *r,unsigned mask,int helper){
 for(unsigned i=0;i<4;i++){
  bool enabled=(mask>>i)&1;
  DISABLED
  glActiveTexture(GL_TEXTURE0+i);
  REUSED
  /* A helper can conservatively invalidate all units mid-pass even when
   * it restores the GL bindings. Later units alone become known again. */
  if((int)i==helper)pgraph_gl_tex_shadow_invalidate(r);
 }
}
static void check_driver(PGRAPHGLState *r,unsigned mask){
 for(unsigned i=0;i<4;i++){
  TextureBinding *b=r->texture_binding[i];
  if((mask>>i)&1)CHECK(driver[i][b->gl_target]==b->gl_texture);
  else for(unsigned t=0;t<4;t++)CHECK(!driver[i][t]);
 }
}
int main(void){
 for(unsigned serial=0;serial<65536;serial++){
  PGRAPHGLState r={0};TextureBinding bindings[4];unsigned mask=(serial>>8)&15,foreign=(serial>>12)&15;
  for(unsigned i=0;i<4;i++){
   bindings[i]=(TextureBinding){1000+i,(serial>>(i*2))&3};r.texture_binding[i]=&bindings[i];
   r.tex_shadow_name[i]=900+i;r.tex_shadow_target[i]=3;
   for(unsigned t=0;t<4;t++)driver[i][t]=700+i*4+t;
  }
  binds=0;draw_bind(&r,mask,-1);CHECK(r.tex_shadow_valid_mask==15);
  CHECK(binds==count_bind(mask,15));check_driver(&r,mask);
  unsigned stable=binds;
  for(unsigned repeat=0;repeat<64;repeat++){
   draw_bind(&r,mask,-1);check_driver(&r,mask);CHECK(binds==stable);
  }
  for(unsigned i=0;i<4;i++)if((foreign>>i)&1){glActiveTexture(GL_TEXTURE0+i);glBindTexture(bindings[i].gl_target,8000+i);}
  pgraph_gl_tex_shadow_invalidate(&r);CHECK(!r.tex_shadow_valid_mask);
  unsigned before=binds;draw_bind(&r,mask,-1);CHECK(binds==before+count_bind(mask,15));check_driver(&r,mask);
  int helper=serial&3;draw_bind(&r,mask,helper);
  unsigned unknown=(mask>>helper)&1 ? ((1u<<(helper+1))-1) : 0;
  CHECK(r.tex_shadow_valid_mask==(15&~unknown));before=binds;
  draw_bind(&r,mask,-1);CHECK(binds==before+count_bind(mask,unknown));check_driver(&r,mask);
  for(unsigned i=0;i<4;i++){
   CHECK(r.texture_binding[i]==&bindings[i]);
   draw_bind(&r,mask,-1);
   TextureBinding next={2000+i,(bindings[i].gl_target+1)&3};TextureBinding *binding=&next;
   glActiveTexture(GL_TEXTURE0+i);glBindTexture(next.gl_target,next.gl_texture);
   r.tex_shadow_name[i]=next.gl_texture;r.tex_shadow_target[i]=next.gl_target;r.tex_shadow_valid_mask|=1u<<i;
   PGRAPHGLState *saved=&r;
   {PGRAPHGLState *r=saved;SWITCH}
   CHECK(r.tex_shadow_valid_mask==(15&~(1u<<i)));
   r.texture_binding[i]=&next;draw_bind(&r,0,-1);check_driver(&r,0);r.texture_binding[i]=&bindings[i];
  }
  before=binds;pgraph_gl_tex_shadow_reset(&r);CHECK(binds==before+16 && r.tex_shadow_valid_mask==15);check_driver(&r,0);
 }
 for(unsigned scale_value=0;scale_value<4;scale_value++){
  PGRAPHGLState r={.tex_shadow_valid_mask=15};NV2AState d={.pgraph={.gl_renderer_state=&r}};
  owner_device=&d;owner_kicks=0;owner_strict=true;on_render=false;
  pgraph_gl_set_surface_scale_factor(&d,scale_value);
  CHECK(!r.tex_shadow_valid_mask && !d.pfifo.halt && owner_kicks==3);
  CHECK(g_config.display.quality.surface_scale==(scale_value?scale_value:1));owner_strict=false;owner_device=NULL;
 }
 puts("Texture shadow: 65536 target/enable/external-bind/mid-pass/target-switch cases, 64 stable passes each; correct selected bindings, bounded recovery and unchanged owners PASS");
}
'''.replace('FUNCTIONS',functions).replace('DISABLED',disabled).replace('REUSED',reused).replace('SWITCH',switch).replace('OWNER_FLUSH',flush).replace('SCALE',scale)
with tempfile.TemporaryDirectory(prefix='gl-texture-shadow-') as tmp:
 d=Path(tmp);path=d/'fixture.c';binary=d/'fixture.exe'
 def run(body):
  path.write_text(body);subprocess.run(cc+['-std=c11','-O2','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],check=True)
  return subprocess.run([str(binary)],capture_output=True,timeout=30)
 result=run(fixture)
 if result.returncode:raise SystemExit(result.stderr.decode())
 print(result.stdout.decode().strip())
 mutants=[
  ('frontend cache write',fixture.replace(scale,scale.replace('{', '{\n    pgraph_gl_tex_shadow_invalidate(d->pgraph.gl_renderer_state);',1),1)),
  ('missing render-owner invalidation',fixture.replace(flush,flush.replace('pgraph_gl_tex_shadow_invalidate(r);','(void)r;'),1)),
  ('missing per-unit publication',fixture.replace('r->tex_shadow_valid_mask |= 1u << i;','/* omit publication */')),
  ('blind external trust',fixture.replace('r->tex_shadow_valid_mask = 0;','r->tex_shadow_valid_mask = 15;',1)),
  ('target-switch false empty',fixture.replace(switch,switch.replace('r->tex_shadow_valid_mask &= ~(1u << i);','/* omit invalidation */'),1)),
  ('global validity instead of per-unit knowledge',fixture.replace('(r->tex_shadow_valid_mask & (1u << i))','r->tex_shadow_valid_mask')),
  ('unconditional all-unit clearing',fixture.replace('static void draw_bind(PGRAPHGLState *r,unsigned mask,int helper){','static void draw_bind(PGRAPHGLState *r,unsigned mask,int helper){pgraph_gl_tex_shadow_reset(r);'))]
 for label,body in mutants:
  assert body!=fixture,label
  result=run(body);assert result.returncode and b'FAIL line' in result.stderr,label
  print('Required negative control rejected:',label)
