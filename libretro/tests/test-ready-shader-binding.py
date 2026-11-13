# SPDX-License-Identifier: GPL-2.0-or-later
"""Native-LRU publication from resident module owners, without compilation."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
    m=re.search(r'^(?:static )?(?:inline )?(?:int|bool|void|ShaderBinding \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','PipelineKey','PipelineBinding'))
types+='\n'+re.search(r'typedef struct ReadyBindingInput \{.*?\} ReadyBindingInput;',s,re.S)[0]
names=('pgraph_vk_ready_shader_binding','ready_binding_compare','ready_binding_init','pgraph_vk_materialize_ready_shader_binding')
parts='\n'.join(fn(n) for n in names)
body=fn('pgraph_vk_materialize_ready_shader_binding')
for forbidden in ('get_and_ref_shader_module_for_key','pgraph_vk_ref_state_modules','vkCreate','pump_drain','get_shader_binding_for_state'):
    assert forbidden not in body,forbidden
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',parts)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ready-shader-binding-') as d:
    p=Path(d);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
    if os.name != 'nt':cmd += ['-fsanitize=address','-fno-omit-frame-pointer']
    def run(text,negative=False):
        src.write_text(text);subprocess.run(cmd,env=env,check=True)
        result=subprocess.run([str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        if negative:assert result.returncode,'negative control escaped'
        else:
            assert result.returncode==0,result.stdout.decode(errors='replace')
            print(result.stdout.decode(),end='')
    run(code)
    controls=[
        ('full pipeline hash collision',code.replace('!memcmp(&candidate->key, key, sizeof(*key))','true',1)),
        ('incomplete pipeline',code.replace('candidate->pipeline &&','true &&',1)),
        ('clear pipeline',code.replace('!pipeline || key->clear ||','!pipeline || false ||',1)),
        ('missing vertex owner reference',code.replace('pgraph_vk_ref_shader_module(input.vsh);','(void)input.vsh;',1)),
        ('missing fragment owner reference',code.replace('pgraph_vk_ref_shader_module(input.psh);','(void)input.psh;',1)),
        ('missing geometry owner reference',code.replace('pgraph_vk_ref_shader_module(input.geom);','(void)input.geom;',1)),
        ('wrong vertex owner',code.replace('.vsh = pipeline->shader_modules[0]','.vsh = r->shader_binding->vsh.module_info',1)),
        ('incomplete exact cache entry reused',code.replace('if (node->hash == hash &&\n            !memcmp', 'if (false && node->hash == hash &&\n            !memcmp',1)),
        ('cache state not owned',code.replace('memcpy(&binding->state, input->state, sizeof(binding->state));','memset(&binding->state, 0, sizeof(binding->state));',1)),
        ('uniform locations not prepared',code.replace('update_shader_uniform_locs(binding);','(void)update_shader_uniform_locs;',1)),
        ('current binding evicted under pressure',code.replace('if (!r->shader_cache.num_free)', 'if (false)',1)),
    ]
    for name,mutated in controls:
        assert mutated!=code,name
        run(mutated,True);print('Required negative control rejected:',name)
