# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production upload-time GPL constructor and result adoption."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
def fn(name):
    m=re.search(r'^(?:static )?(?:void|bool|LibJob \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
job=re.search(r'typedef struct \{\n    bool frag;.*?\} LibJob;',s,re.S)[0]
parts='\n'.join(fn(n) for n in ('pipeline_job_reset','lib_job_link','pump_slot','pump_submit','library_land','pump_drain','pgraph_vk_pre_library_ready'))
def audit(body):
    assert 'vkCreateGraphicsPipelines' not in body and 'gpl_get_pre(' not in body and 'pgraph_vk_ref_pre_state_modules(' not in body
audit(fn('pgraph_vk_pre_library_ready'));audit(fn('pgraph_vk_prewarm_shaders'))
try:audit(fn('pgraph_vk_pre_library_ready')+'\ngpl_get_pre(pg, NULL, NULL, 0, 0);')
except AssertionError:print('Required negative control rejected: synchronous upload library creation')
else:raise AssertionError('synchronous prewarm escaped')
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
metadata=re.search(r'typedef struct VkPrewarmState \{.*?\} VkPrewarmState;',h,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PREWARM */',metadata).replace('/* JOB */',job).replace('/* PRODUCTION */',parts)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='prewarm-library-') as d:
    p=Path(d);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
    src.write_text(code);subprocess.run(cmd,env=env,check=True);subprocess.run([str(exe)],env=env,check=True)
    controls=[
        ('missing vertex owner',code.replace('job->mods[job->nmods++] = vsh;','(void)vsh;',1)),
        ('missing geometry owner',code.replace('job->mods[job->nmods++] = geom;','(void)geom;',1)),
        ('queued duplicate',code.replace('if (!pending->full && !pending->frag && pending->hash == job->hash)', 'if (pending->full && !pending->frag && pending->hash == job->hash)',1)),
        ('duplicate worker result retained',code.replace('vkDestroyPipeline(r->device, job->result, NULL);','if (!job->result) vkDestroyPipeline(r->device, job->result, NULL);',1)),
        ('missing dynamic-state snapshot',code.replace('memcpy(job->dyn_states, pipeline->dynamic_states,\n           pipeline->num_dynamic * sizeof(job->dyn_states[0]));','(void)pipeline->dynamic_states;',1)),
        ('prepared library uses raw module handle',code.replace('gpl_pre_hash(r, vsh->identity,','gpl_pre_hash(r, (uint64_t)(uintptr_t)vsh->module,',1)),
    ]
    for name,mutant in controls:
        assert mutant!=code,name;src.write_text(mutant);subprocess.run(cmd,env=env,check=True)
        bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
        assert bad.returncode==1 and b'FAIL ' in bad.stderr,name
        print('Required negative control rejected:',name)
