#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production full-PSO publication and module ownership retirement."""
from pathlib import Path
import os, re, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
    m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
assert fn('create_pipeline').count('pipeline_hold_modules(')==1
assert 'pipeline_hold_modules(' not in fn('create_clear_pipeline')
assert fn('pump_drain').index('job->result == VK_NULL_HANDLE')<fn('pump_drain').index('pipeline_land(pg, job)')
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','PipelineKey','PipelineBinding'))
production='\n'.join(fn(n) for n in ('pipeline_hold_modules','pipeline_cache_entry_init','pipeline_cache_entry_post_evict','pipeline_cache_entry_compare','pipeline_land'))
template=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='pipeline-module-owners-') as tmp:
    d=Path(tmp);(d/'qemu').mkdir()
    (d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n')
    (d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
    flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(c),'-o',str(exe)]
    if os.name!='nt':flags+=['-fsanitize=address','-fno-omit-frame-pointer']
    def run(text,negative=False):
        c.write_text(template.replace('/* PRODUCTION */',text));subprocess.run(flags,check=True,env=env)
        result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
        if negative:assert result.returncode,'owner mutation escaped'
        else:assert not result.returncode,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
    run(production)
    for label,before,after in [
        ('missing publication reference','pgraph_vk_ref_shader_module(binding->shader_modules[i]);','(void)pgraph_vk_ref_shader_module;'),
        ('lost eviction release','pgraph_vk_unref_shader_module(r, snode->shader_modules[i]);','(void)pgraph_vk_unref_shader_module;'),
        ('geometry owner confused with fragment','count == 3 ? modules[1] : NULL','count == 3 ? modules[2] : NULL'),
        ('duplicate result replaces owner','if (snode->pipeline == VK_NULL_HANDLE) {','if (true) {'),
        ('stale owner after eviction','snode->shader_modules[i] = NULL;','/* stale metadata owner */'),
    ]:
        assert production.count(before)==1,label
        run(production.replace(before,after,1),True)
        print('Required PSO metadata control rejected:',label)
