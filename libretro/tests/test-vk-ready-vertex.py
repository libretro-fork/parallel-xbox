# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual prepared Vulkan owner/payload/input bodies; no core or GPU build."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();source=r/'hw/xbox/nv2a/pgraph/vk/ready-vertex.inc'
if len(sys.argv)>3: source=Path(sys.argv[3]).resolve() # Historical implementation replay; checkout/types remain pinned.
assert source.is_file(),'Prepared Vulkan support requires its owner implementation'
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S)[0] for name in ['ShaderModuleCacheKey','ShaderModuleCacheEntry'])
namespace=re.search(r'enum VkReadyVertexKind \{.*?\};\n#define VK_READY_VERTEX_VERSION[^\n]+\n#define VK_READY_VERTEX_STAGES[^\n]+',h,re.S)[0]
bank=re.search(r'    struct \{\n        bool ready, route;.*?    \} ready_vertex;',h,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',namespace+'\n'+types).replace('/* BANK */',bank).replace('/* PRODUCTION */',source.read_text())
aliases='entry->module_info = r->ready_vertex.stages[i - 1].module_info;' in source.read_text()
if 'alias slot owns one reference' in source.read_text() or '60 native owners' in source.read_text(): assert aliases
code='#define TEST_PREFIX_ALIASES '+str(int(aliases))+'\n'+code
shader=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();buffer=(r/'hw/xbox/nv2a/pgraph/vk/buffer.c').read_text()
# Public wiring is required: an isolated unused implementation is not coverage.
assert shader.count('vk_ready_vertex_init(r);')==1
assert shader.index('vk_spec_quiesce(pg->vk_renderer_state);')<shader.index('shader_cache_finalize(pg);',shader.index('void pgraph_vk_finalize_shaders'))
assert 'i = reserved; i < shader_module_cache_size' in shader and 'VK_READY_VERTEX_STAGES : 0' in shader
start=shader.index('get_and_ref_shader_module_for_key(');end=shader.index('\n}',start)
assert 'vk_ready_vertex_find(' not in shader[start:end],'Ordinary worker/adoption module refs must stay exact'
assert 'ShaderModuleInfo *prepared = binding->ready_vertex_route ?' in shader
assert 'binding->ready_vertex_route == r->ready_vertex.route' in shader
assert 'full->ready_vertex_route != r->ready_vertex.route' in shader
assert 'snode->ready_vertex_route != r->ready_vertex.route' in shader
assert 'binding->ready_vertex_route != r->ready_vertex.route' in shader
assert 'r->shader_binding->ready_vertex_route != r->ready_vertex.route' in shader
assert 'pgraph_vk_ready_vertex_upload_extent(r, mod) <= end - mod->uniform_staged_offset' in shader
assert 'pgraph_vk_reserve_buffer(' in shader and 'pgraph_vk_ready_vertex_upload(pg, modules[i], destination);' in shader
assert 'key->prepared_vertex = 0;' in draw and 'key->prepared_vertex = pgraph_vk_ready_vertex_identity(' in draw
assert 'if (!job->key.prepared_vertex) pgraph_vk_spec_pipeline_' in draw
assert 'pgraph_vk_ready_vertex_input(pg);' in draw
assert 'module_info->ready_vertex_index ||' in draw
assert 'dst_access_mask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;' in draw and 'dst_stage_mask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;' in draw
assert 'pgraph_vk_prepared_vertex_supported(r) ?' in buffer and '.buffer_size = 16 * 1024 * 1024' in buffer
assert 'index == BUFFER_UNIFORM_STAGING' in buffer
assert 'entry->key.native_kind = 1;' in source.read_text()
assert 'pgraph_vk_create_shader_object_from_spirv(r, VK_SHADER_STAGE_VERTEX_BIT,' in (r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
def fn(text,name):
 m=re.search(r'^(?:static )?(?:bool|VkDeviceSize|void) '+name+r'\([^;{]*\)\s*\{',text,re.M);assert m,name
 end=text.index('{',m.start())+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]
code=code.replace('/* SNAPSHOT AND RESERVATION */',fn(shader,'uniform_snapshot_valid')+'\n'+fn(buffer,'pgraph_vk_reserve_buffer'))
glsl=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
code=code.replace('/* GPU OWNER FUNCTIONS */',fn(glsl,'pgraph_vk_shader_object_use')+'\n'+fn(glsl,'pgraph_vk_shader_objects_retire'))


env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-ready-vertex-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 def run(text,negative=False):
  src.write_text(text);subprocess.run(cmd,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60)
  if negative:assert result.returncode,'Required control escaped'
  else:assert result.returncode==0,result.stderr.decode();print(result.stdout.decode(),end='')
 run(code)
 controls=[
 ('nonfinite inputs admitted',code.replace('(bits & 0x7f800000u) == 0x7f800000u','false && (bits & 0x7f800000u) == 0x7f800000u',1)),
 ('mask-only change ignored',code.replace('!payload->valid || payload->mask != mask','!payload->valid',1)),
 ('constant raw payload comparison omitted',code.replace('memcmp(payload->values[count], value, 16)','false',1)),
 ('code update omitted',code.replace('binding->state.vsh.programmable.program_data, VSH_INTERPRETER_FLAGS_OFFSET, false','binding->state.vsh.programmable.program_data, 16, false',1)),
 ('upload extent drops suffix',code.replace('payload->suffix_offset + count * 16','payload->suffix_offset',1)),
 ('wrong final constant slice',code.replace('r->uniform_buffer_offsets[0] + payload->suffix_offset','payload->suffix_offset',1)),
 ('constant stride is not zero',code.replace('.stride = 0, .divisor = 1','.stride = 16, .divisor = 1',1)),
 ('partial startup publishes ready',code.replace('if (!entry->module_info) goto unavailable;','if (!entry->module_info) { r->ready_vertex.ready = true; return; }',1)),
 ('program bound not checked',code.replace('s->programmable.program_length <= 0','(false && s->programmable.program_length <= 0)',1)),
 ('malformed code array length accepted',code.replace('code->dim_a != NV2A_MAX_TRANSFORM_PROGRAM_LENGTH','false',1)),
 ('malformed flags bounds accepted',code.replace('sizeof(uint32_t) > module->uniforms.total_size - flags->offset','false',1)),
 ('malformed code extent accepted',code.replace('VSH_INTERPRETER_FLAGS_OFFSET > module->uniforms.total_size - code->offset','false',1)),
 ('wrong programmable FP execution target',code.replace('(s->point_params_enable ? 4u : 0u)','0u',1)),
 ('stale upload epoch reused',code.replace('mod->uniform_staged_epoch == r->uniform_upload_epoch','true',1)),
 ('truncated suffix snapshot accepted',code.replace('pgraph_vk_ready_vertex_upload_extent(r, mod) <= end - mod->uniform_staged_offset','mod->uniforms.total_size <= end - mod->uniform_staged_offset',1)),
 ('reservation alignment omitted',code.replace('ROUND_UP(b->buffer_offset, alignment)','b->buffer_offset',1)),
 ('unsupported swizzle accepted',code.replace('s->compressed_attrs || s->swizzle_attrs || s->const_attrs_dynamic','s->compressed_attrs || s->const_attrs_dynamic',1)),
 ('prepared bank aliases native module target',code.replace('entry->key.native_kind = 1;','entry->key.native_kind = 0;',1)),
 ('shader-object capability gate ignored',code.replace('r->shader_objects.enabled && pgraph_vk_prepared_vertex_enabled()','true && pgraph_vk_prepared_vertex_enabled()',1)),
 ]
 if aliases:
  controls += [
   ('alias slot reference omitted',code.replace('pgraph_vk_ref_shader_module(entry->module_info); /* alias slot owns one reference */','/* alias reference omitted */',1)),
   ('alias targets a different FP owner',code.replace('r->ready_vertex.stages[i - 1].module_info','r->ready_vertex.stages[0].module_info',1)),
   ('alias payload identity is rewritten',code.replace('continue;\n        }\n        MString *source','entry->module_info->ready_vertex_index = i + 1; continue;\n        }\n        MString *source',1)),
   ('GPU last-consumer retained reference omitted',code.replace('pgraph_vk_ref_shader_module(module);','/* GPU reference omitted */',1)),
   ('GPU last-use serial retired too early',code.replace('module->object_retire_after = r->submit_count + 1;','module->object_retire_after = r->submit_count;',1)),
  ]
 for name,mutant in controls:
  assert mutant!=code,name;run(mutant,True);print('Required negative control rejected:',name)
