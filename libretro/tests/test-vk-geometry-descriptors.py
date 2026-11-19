#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual optional descriptor ABI, immutable snapshots and transfer visibility."""
from pathlib import Path
import os,re,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();d=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
def fn(text,name):
 m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\([^;]*?\)\s*\{',text,re.M);assert m,name
 z=text.index('{',m.start())+1;depth=1
 while depth:depth+=(text[z]=='{')-(text[z]=='}');z+=1
 return text[m.start():z]
parts='\n'.join(fn(s,n) for n in ['create_descriptor_pool','create_descriptor_set_layout','uniform_snapshot_valid','pgraph_vk_update_descriptor_sets'])+'\n'+'\n'.join(fn(d,n) for n in ['bind_descriptor_sets','sync_staging_buffers'])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',parts)
# The immutable ABI is published before any native shader and retained on disable.
init=fn(s,'pgraph_vk_init_shaders');assert init.index('r->has_gs_ubo_binding =')<init.index('create_descriptor_pool(pg)')<init.index('vk_ready_vertex_init(r)')
assert 'has_gs_ubo_binding =' not in fn(s,'pgraph_vk_shader_objects_disable')
update=fn(s,'update_shader_uniforms_tracked');assert 'vk_ready_geometry_parameters(pg, binding)' in update and 'r->uni_last_pushed[2]' in update
with tempfile.TemporaryDirectory(prefix='vk-gs-descriptors-') as tmp:
 p=Path(tmp);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test');env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 def run(text,negative=False):
  src.write_text(text);subprocess.run(command,check=True,env=env);done=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=30)
  if negative:assert done.returncode and b'FAIL GS transport' in done.stdout,done.stdout.decode(errors='replace')
  else:assert done.returncode==0,done.stdout.decode(errors='replace');print(done.stdout.decode(),end='')
 run(code)
 for label,before,after in [('mutable-offset-ABI','r->has_gs_ubo_binding ? 3 : 2, offsets','r->shader_objects.enabled ? 3 : 2, offsets'),('missing-third-write','2 + NV2A_MAX_TEXTURES + r->has_gs_ubo_binding,\n                           descriptor_writes','2 + NV2A_MAX_TEXTURES,\n                           descriptor_writes'),('GS-not-reuploaded-after-rotation','uniform_uploads = all_uniforms;','uniform_uploads = 3;'),('stale-generation','mod->uniform_staged_epoch == r->uniform_upload_epoch','true'),('missing-GS-stage','dst_stage_mask |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;','dst_stage_mask |= 0;'),('mutable-GS-barrier','if (r->has_gs_ubo_binding) {\n                /* The descriptor ABI','if (r->shader_objects.enabled) {\n                /* The descriptor ABI')]:
  assert before in parts,label;run(code.replace(parts,parts.replace(before,after,1)),True);print('Required GS descriptor fault rejected:',label)
