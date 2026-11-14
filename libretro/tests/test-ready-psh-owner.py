#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute actual native fragment owner/alias orchestration with native API mocks."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(source,signature):
 start=source.index(signature);i=source.index('{',start)+1;depth=1
 while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[start:i]
source=(r/'hw/xbox/nv2a/pgraph/gl/ready-psh.inc').read_text(encoding='utf-8')
texture=(r/'hw/xbox/nv2a/pgraph/gl/texture.c').read_text(encoding='utf-8')
retire='\n'.join(function(texture,sig) for sig in ['static void ready_psh_alias_retire(', 'void pgraph_gl_tex_shadow_reset(', 'void pgraph_gl_tex_shadow_invalidate(', 'static void texture_binding_destroy(gpointer data)\n'])
selector=function((r/'hw/xbox/nv2a/pgraph/glsl/psh-ready.inc').read_text(encoding='utf-8'),'bool pgraph_glsl_psh_ready_controls(')
shader=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text(encoding='utf-8')
final=function(shader,'void pgraph_gl_finalize_shaders(')
assert final.index('lru_flush(&r->shader_cache)')<final.index('ready_psh_finalize(r)')<final.index('g_free(r->shader_module_cache_entries)')
evict=function(shader,'static void shader_cache_entry_post_evict(')
assert evict.index('ready_psh_invalidate(r, binding)')<evict.index('memset(&binding->state')
assert 'reserved_geometry += 2;' in shader and 'r->shader_module_cache_entries + reserved_geometry' in shader
for signature in ['void pgraph_gl_tex_shadow_reset(', 'void pgraph_gl_tex_shadow_invalidate(']:assert 'aliases_valid' not in function(texture,signature)
surface=(r/'hw/xbox/nv2a/pgraph/gl/surface.c').read_text(encoding='utf-8')
for signature in ['static void render_surface_to(', 'void render_surface_to_texture(', 'void pgraph_gl_render_cube_faces_to_texture(']:assert 'assert(texture_unit >= 0 && texture_unit < NV2A_MAX_TEXTURES);' in function(surface,signature)
template=Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* SELECTOR */',selector)
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ready-psh-owner-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 def run(owner,retirement):
  c=d/'test.c';c.write_text(template.replace('/* PRODUCTION */',owner).replace('/* RETIREMENT */',retirement),encoding='utf-8');exe=d/('test.exe' if os.name=='nt' else 'test')
  subprocess.run([cc,'-O2','-DNV2A_MAX_TEXTURES=4','-DXEMU_EXPERIMENTAL_READY_VSH','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),str(c),'-o',str(exe)],env=env,check=True)
  return subprocess.run([str(exe)],capture_output=True,timeout=30,env=env)
 result=run(source,retire);assert result.returncode==0,(result.stdout,result.stderr);print(result.stdout.decode(),end='',flush=True)
 mutations=[('lost binding witness invalidation','r->ready_psh.uploaded_binding = NULL;','(void)r;',False),('lost native deletion','delete_program(r, stage->gl_stage_program);','(void)delete_program;',False),('lost slot return','lru_add_free(&r->shader_module_cache, &reserved[i].node);','(void)reserved;',False),('wrong interpolation slot','&r->ready_psh.stages[!!key->psh.state.smooth_shading]','&r->ready_psh.stages[!key->psh.state.smooth_shading]',False),('lost active-unit restoration','glActiveTexture(active);','(void)active;',False),('stale disabled texture','pgraph_is_texture_enabled(pg, unit)','true',False),('retained compiler object','glDetachShader(stage->gl_stage_program, stage->gl_shader);','(void)glDetachShader;',False),('lost alias teardown','glBindTexture(i < 4 ? GL_TEXTURE_3D : GL_TEXTURE_CUBE_MAP, 0);','(void)glBindTexture;',False),('premature publication','for (unsigned i = 0; i < 2; i++) {\n        PshState state', 'r->ready_psh.ready=true;\n    for (unsigned i = 0; i < 2; i++) {\n        PshState state',False),('primary reset clobbers reserved aliases','r->tex_shadow_valid_mask = 0;','r->tex_shadow_valid_mask = 0;r->ready_psh.aliases_valid=0;',True),('lost retirement invalidation','ready_psh_alias_retire(g_nv2a->pgraph.gl_renderer_state, binding->gl_texture);','(void)ready_psh_alias_retire;',True)]
 for name,before,after,is_retire in mutations:
  target=retire if is_retire else source;assert before in target,name
  result=run(source if is_retire else target.replace(before,after),target.replace(before,after) if is_retire else retire);assert result.returncode==86 and b'Assertion' in result.stderr,(name,result.stdout,result.stderr)
  print('Required fragment owner control rejected:',name,flush=True)
