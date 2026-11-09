#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact native graphics layout coverage, scoped lends and retirement order."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
    m=re.search(r'^(?:static )?(?:void|bool|VkPipelineLayout|uint32_t) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
assert 'graphics_pipeline_layouts[NV2A_VERTEXSHADER_ATTRIBUTES + 1]' in h
init=fn('pgraph_vk_init_pipelines');fini=fn('pgraph_vk_finalize_pipelines')
assert init.index('init_pipeline_layouts(r)')<init.index('init_clear_shaders(pg)')
order=['pump_quiesce(pg)','finalize_pipeline_cache(pg)','pgraph_vk_wait_all_slots(r)','pgraph_vk_trash_drain(r)','gpl_finalize(r)','finalize_pipeline_layouts(r)']
assert [fini.index(x) for x in order]==sorted(fini.index(x) for x in order)
renderer=(r/'hw/xbox/nv2a/pgraph/vk/renderer.c').read_text()
assert renderer.index('pgraph_vk_finalize_pipelines(pg)')<renderer.index('pgraph_vk_finalize_shaders(pg)')
assert s.count('vkCreatePipelineLayout(')==2, 'only bounded initialization creates graphics layouts'
for n in ('create_clear_pipeline','create_pipeline','pipeline_job_fill','pgraph_vk_pre_library_ready','library_land','pipeline_land','pump_drain'):
    assert 'vkCreatePipelineLayout(' not in fn(n) and 'vkDestroyPipelineLayout(' not in fn(n),n
assert '.layout = snode->layout' not in fn('pipeline_cache_entry_post_evict')
parts='\n'.join(fn(n) for n in ('init_pipeline_layouts','finalize_pipeline_layouts','create_pipeline_layout_size','push_constant_size','create_pipeline_layout'))
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',parts)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='pipeline-layout-lends-') as d:
    p=Path(d);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
    src.write_text(code);subprocess.run(cmd,env=env,check=True);subprocess.run([str(exe)],env=env,check=True)
    controls=[
        ('missing maximum range',code.replace('i < count;','i + 1 < count;',1)),
        ('wrong vertex range',code.replace('.size = i * 4 * sizeof(float),','.size = i * sizeof(float),',1)),
        ('clear borrows descriptor layout',code.replace('info.setLayoutCount = 1;','info.setLayoutCount = 0;',1)),
        ('wrong borrowed owner',code.replace('return r->graphics_pipeline_layouts[index];','return r->graphics_pipeline_layouts[0];',1)),
        ('stale layout after teardown',code.replace('r->graphics_pipeline_layouts[i] = VK_NULL_HANDLE;','(void)i;',1)),
    ]
    for name,mutant in controls:
        assert mutant!=code,name;src.write_text(mutant);subprocess.run(cmd,env=env,check=True)
        bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
        assert bad.returncode==1 and b'FAIL ' in bad.stderr,name
        print('Required negative control rejected:',name)
