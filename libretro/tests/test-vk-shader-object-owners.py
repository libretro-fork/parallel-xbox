#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run actual shader-object native/source/GPU-owner/admission/fence bodies.

The native calls and tiny cache are checked mocks; this does not qualify a
driver or production cache throughput. No corpus or timeout is reduced.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
glsl=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
shaders=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
header=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
renderer=(r/'hw/xbox/nv2a/pgraph/vk/renderer.c').read_text()

def function(text,name):
    match=re.search(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_]\w*(?:\s*\*)?\s*\b'+name+r'\([^;{]*\)\s*\{',text,re.M)
    assert match,name
    end=text.index('{',match.start())+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[match.start():end]

names=[('init_uniform_layout',glsl),('alloc_shader_module_info',glsl),
       ('pgraph_vk_shader_layouts_unref',glsl),('pgraph_vk_shader_source_unref',glsl),
       ('pgraph_vk_ref_shader_module',glsl),('pgraph_vk_destroy_shader_module',glsl),
       ('pgraph_vk_unref_shader_module',glsl),('pgraph_vk_create_shader_object_from_spirv',glsl),
       ('pgraph_vk_shader_object_use',glsl),('pgraph_vk_shader_objects_retire',glsl),
       ('shader_module_cache_entry_pre_evict',shaders),('get_and_ref_shader_module_for_key',shaders),
       ('pgraph_vk_slot_retired',draw),('pgraph_vk_wait_all_slots',draw),('finish_is_async',draw)]
parts={name:function(text,name) for name,text in names}
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',header,re.S)[0]
                for name in ['ShaderModuleInfo','ShaderModuleCacheKey','ShaderModuleCacheEntry'])
types+='\n'+re.search(r'typedef enum FinishReason \{.*?\} FinishReason;',header,re.S)[0]
template=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types)

# Source-bound retirement/admission cannot be an unused helper-only proof.
assert 'r->shader_module_cache.pre_node_evict = shader_module_cache_entry_pre_evict;' in shaders
assert parts['pgraph_vk_slot_retired'].index('r->settled_submit =')<parts['pgraph_vk_slot_retired'].index('pgraph_vk_shader_objects_retire(r);')
wait=parts['pgraph_vk_wait_all_slots']
assert wait.index('vkWaitForFences(')<wait.index('r->settled_submit = r->submit_count;')<wait.index('pgraph_vk_shader_objects_retire(r);')
finalize=function(renderer,'pgraph_vk_finalize')
assert finalize.index('pgraph_vk_finish(')<finalize.index('pgraph_vk_shader_objects_retire(')<finalize.index('pgraph_vk_finalize_shaders(')
assert 'assert(!pg->vk_renderer_state->shader_objects.gpu_owners);' in finalize
start=finalize.index('    if (pg->vk_renderer_state->shader_objects.gpu_owners) {')
end=finalize.index('{',start)+1;depth=1
while depth:depth+=(finalize[end]=='{')-(finalize[end]=='}');end+=1
parts['shutdown_owner_boundary']='static void shutdown_owner_boundary(PGRAPHState *pg){\n'+finalize[start:end]+'\n}'
names.append(('shutdown_owner_boundary',renderer))
finish=function(draw,'pgraph_vk_finish')
assert 'bool async_ok = finish_is_async(finish_reason);' in finish
assert 'pgraph_vk_wait_all_slots(r);' in finish and '} else if (!finish_is_async(finish_reason)) {' in finish

def code(overrides=None):
    actual=dict(parts)
    if overrides:actual.update(overrides)
    return template.replace('/* PRODUCTION */','\n\n'.join(actual[name] for name,text in names))

controls=[
 ('native source pointer replaced','pgraph_vk_create_shader_object_from_spirv','.pCode = source->words,','.pCode = source->words + 1,'),
 ('VS next-stage geometry capability lost','pgraph_vk_create_shader_object_from_spirv','r->shader_objects.features.geometryShader ? VK_SHADER_STAGE_GEOMETRY_BIT : 0','false ? VK_SHADER_STAGE_GEOMETRY_BIT : 0'),
 ('push-range compatibility lost','pgraph_vk_create_shader_object_from_spirv','.pushConstantRangeCount = r->use_push_constants_for_uniform_attrs ? 1 : 0,','.pushConstantRangeCount = 0,'),
 ('partial native owner leaked','pgraph_vk_create_shader_object_from_spirv','if (object) vkDestroyShaderEXT(r->device, object, NULL);','(void)object;'),
 ('last CPU source lend never returned','pgraph_vk_create_shader_object_from_spirv','pgraph_vk_shader_source_unref(source);\n    r->shader_objects.live_objects++;','/* source return omitted */\n    r->shader_objects.live_objects++;'),
 ('immutable layouts not retained','pgraph_vk_create_shader_object_from_spirv','if (info->layouts) retro_atomic_fetch_add_int(&info->layouts->refs, 1);','/* layout reference omitted */'),
 ('queued target depends on mutable route','pgraph_vk_create_shader_object_from_spirv','assert(source);','assert(source && r->shader_objects.enabled);'),
 ('GPU owner not referenced','pgraph_vk_shader_object_use','pgraph_vk_ref_shader_module(module);','/* GPU reference omitted */'),
 ('same recording duplicates GPU pin','pgraph_vk_shader_object_use','if (!module->object_gpu_ref) {','if (true) {'),
 ('open recording serial off by one','pgraph_vk_shader_object_use','module->object_retire_after = r->submit_count + 1;','module->object_retire_after = r->submit_count;'),
 ('later recording lifetime not extended','pgraph_vk_shader_object_use','module->object_retire_after = r->submit_count + 1;','if (!module->object_gpu_ref) module->object_retire_after = r->submit_count + 1;'),
 ('serial wrap compared unsigned','pgraph_vk_shader_objects_retire','(int32_t)(module->object_retire_after - r->settled_submit) <= 0','module->object_retire_after <= r->settled_submit'),
 ('pending head hides completed owner','pgraph_vk_shader_objects_retire','else link = &module->object_gpu_next;','else break;'),
 ('GPU marker survives last unref','pgraph_vk_shader_objects_retire','module->object_gpu_next = NULL; module->object_gpu_ref = false;','module->object_gpu_next = NULL;'),
 ('binding references can be evicted','shader_module_cache_entry_pre_evict','(module->refcnt == 1 && !module->object_gpu_ref)','true'),
 ('GPU pins can be evicted','shader_module_cache_entry_pre_evict','(module->refcnt == 1 && !module->object_gpu_ref)','(module->refcnt == 1)'),
 ('only one reserve slot recovered','get_and_ref_shader_module_for_key','while (!found && r->shader_module_cache.num_free <= 3) {','if (!found && r->shader_module_cache.num_free <= 3) {'),
 ('ordinary three-stage reserve shrunk','get_and_ref_shader_module_for_key','r->shader_module_cache.num_free <= 3','r->shader_module_cache.num_free <= 2'),
 ('exact hit subjected to admission','get_and_ref_shader_module_for_key','while (!found && r->shader_module_cache.num_free <= 3)','while ((found || !found) && r->shader_module_cache.num_free <= 3)'),
 ('hash collision treated as exact hit','get_and_ref_shader_module_for_key','iter->hash == hash && !memcmp(&entry->key, key, sizeof(*key))','iter->hash == hash'),
 ('slot completion omits retirement','pgraph_vk_slot_retired','pgraph_vk_shader_objects_retire(r);','/* retirement omitted */'),
 ('all-slot fence omits retirement','pgraph_vk_wait_all_slots','pgraph_vk_shader_objects_retire(r);','/* retirement omitted */'),
 ('shutdown selects asynchronous finish','shutdown_owner_boundary','VK_FINISH_REASON_FLUSH','VK_FINISH_REASON_STALLED'),
 ('flush policy becomes asynchronous','finish_is_async','return why == VK_FINISH_REASON_STALLED ||','return why == VK_FINISH_REASON_FLUSH || why == VK_FINISH_REASON_STALLED ||'),
]

env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-shader-object-owners-') as tmp:
    p=Path(tmp);(p/'qemu').mkdir()
    (p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
    (p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    source=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare',
             '-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),
             '-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(source),'-o',str(exe)]
    def run(text,negative=False):
        source.write_text(text);subprocess.run(command,env=env,check=True)
        result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=60)
        if negative:
            assert result.returncode!=0 and re.search('assertion|Assertion',result.stderr),(result.returncode,result.stdout,result.stderr)
        else:
            assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
            print(result.stdout,end='',flush=True)
    run(code())
    for label,name,before,after in controls:
        assert parts[name].count(before)==1,(label,parts[name].count(before))
        run(code({name:parts[name].replace(before,after)}),True)
        print('Required shader-object owner/admission fault rejected:',label,flush=True)
