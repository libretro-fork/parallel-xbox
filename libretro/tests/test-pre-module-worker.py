# SPDX-License-Identifier: GPL-2.0-or-later
"""Production bounded pre-raster requests, immutable key lends and adoption."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
    m=re.search(r'^(?:static )?(?:inline )?(?:bool|void|int|ShaderSource \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('ShaderModuleCacheKey','ShaderModuleCacheEntry','RenderPassState','VkPrewarmState'))
globals=s[s.index('#define VK_PRE_REQUESTS'):s.index('static ShaderSource *vk_pre_take_ready(',s.index('#define VK_PRE_REQUESTS'))]
parts='\n'.join(fn(n) for n in ('vk_pre_ref_resident_modules','vk_pre_state_module_keys','vk_pre_take_ready','shader_module_cache_entry_init_pre','vk_pre_process_input','pgraph_vk_queue_pre_modules','vk_pre_service','vk_pre_clear','vk_spec_drain'))
assert 'vk_pre_' not in fn('shader_module_cache_entry_init')
assert 'vk_module_spirv' not in fn('shader_module_cache_entry_init_pre')
assert fn('vk_spec_quiesce').index('vk_pre_owner = NULL;') < fn('vk_spec_quiesce').index('retro_atomic_store_release_int(&vk_spec_cancel, 1);')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* GLOBALS */',globals).replace('/* PRODUCTION */',parts)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='pre-module-worker-') as d:
    p=Path(d);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
    src.write_text(code);subprocess.run(cmd,env=env,check=True);subprocess.run([str(exe)],env=env,check=True,timeout=60)
    controls=[
        ('resident hash collision accepted',code.replace('!memcmp(&entry->key, &request->keys[i], sizeof(entry->key))','entry != NULL',1)),
        ('resident owner not pinned',code.replace('pgraph_vk_ref_shader_module(resident[i]);','(void)resident[i];',1)),
        ('resident retry owner lost',code.replace('request->modules[i] = resident[i];','request->modules[i] = NULL;',1)),
        ('duplicate request queued',code.replace('request->count = 0;\n                return true;', 'break;',1)),
        ('colliding shader keys dropped',re.sub(r' &&\s*!memcmp\(pending->keys, request->keys,\s*request->count \* sizeof\(request->keys\[0\]\)\)', '',code,count=1)),
        ('different pipeline snapshot dropped',code.replace('!memcmp(&pending->pipeline, pipeline, sizeof(*pipeline))','pipeline != NULL',1)),
        ('render-thread compiler invocation',code.replace('request->pipeline = *pipeline;','request->pipeline = *pipeline;\n        vk_spec_compile_module(&request->keys[0], request->hashes[0]);',1)),
        ('wrong-key word adoption',code.replace('!memcmp(&request->keys[i], key, sizeof(*key))','key != NULL',1)),
        ('word owner retained after transfer',code.replace('request->words[i] = NULL;\n                return words;','return words;',1)),
        ('release while pipeline queue is full',code.replace('pgraph_vk_pre_library_ready(pg, &request->pipeline, vsh, geom))','pgraph_vk_pre_library_ready(pg, &request->pipeline, vsh, geom) || true)',1)),
        ('shutdown word references leaked',code.replace(fn('vk_pre_clear'),fn('vk_pre_clear').replace('pgraph_vk_shader_source_unref(request->words[i]);','if (!request->words[i]) pgraph_vk_shader_source_unref(request->words[i]);',1),1)),
        ('native cache initializer not restored',code.replace('r->shader_module_cache.init_node = saved_init;','(void)saved_init;',1)),
    ]
    for name,mutant in controls:
        assert mutant!=code,name;src.write_text(mutant);subprocess.run(cmd,env=env,check=True)
        bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60)
        assert bad.returncode==1 and b'FAIL ' in bad.stderr,name
        print('Required negative control rejected:',name)
