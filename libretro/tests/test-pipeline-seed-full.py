# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare seeded full-PSO keys to production draw keys and verify owned jobs."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
    m=re.search(r'^(?:static )?(?:inline )?(?:void|int) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
fill=fn('pipeline_job_fill')
def audit_fill(body):assert '.topology = topology_for_state(st),' in body and 'get_primitive_topology(pg)' not in body
audit_fill(fill)
try:audit_fill(fill.replace('topology_for_state(st)','get_primitive_topology(pg)'))
except AssertionError:print('Required negative control rejected: topology from unrelated live binding')
else:raise AssertionError('live topology escaped')
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','PipelineKey'))
registers=re.search(r'^static const unsigned pipeline_registers\[\] = \{.*?^\};',s,re.M|re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* REGISTERS */',registers).replace('/* FUNCTIONS */','\n'.join(fn(n) for n in ('pipeline_reg_values','init_pipeline_key','pump_state')))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='seed-full-') as d:
    p=Path(d);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),str(r/'util/fast-hash.c'),str(r/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)]
    src.write_text(code);subprocess.run(cmd,env=env,check=True);subprocess.run([str(exe)],env=env,check=True)
    pump=fn('pump_state')
    mutants=[
        ('uninitialized key suffix',code.replace(pump,pump.replace('memset((char *)key + after_shader, 0, sizeof(*key) - after_shader);','(void)after_shader;',1),1)),
        ('wrong render pass',code.replace('key->render_pass_state = *rp;','(void)rp;',1)),
        ('missing fragment owner',code.replace('job->mods[job->nmods++] = psh;','(void)psh;',1)),
        ('queued duplicate PSO',code.replace('pipeline_ready(r, hash, key) || pump_full_in_flight(hash, key)','pipeline_ready(r, hash, key) && pump_full_in_flight(hash, key)',1)),
    ]
    for name,mutant in mutants:
        assert mutant!=code,name;src.write_text(mutant);subprocess.run(cmd,env=env,check=True)
        bad=subprocess.run([str(exe)],env=env,capture_output=True);assert bad.returncode!=0 and b'FAIL seed-full' in bad.stderr,name
        print('Required negative control rejected:',name)
