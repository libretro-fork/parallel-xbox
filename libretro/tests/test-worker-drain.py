#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare real-queue drains, ready ownership and optional controlled CPU timing.
--benchmark adds timings; --copy-control compares identical copying drains.
WORKER_BENCH_PRODUCER_CPU / WORKER_BENCH_CONSUMER_CPU optionally pin Windows CPUs.
WORKER_DRAIN_SOURCE optionally extracts a local candidate renderer source file.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
source=Path(os.environ.get('WORKER_DRAIN_SOURCE',repo/'hw/xbox/nv2a/pgraph/vk/shaders.c'))
s=source.read_text(encoding='utf-8')
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8')
g=(repo/'hw/xbox/nv2a/pgraph/vk/glsl.h').read_text(encoding='utf-8')
reference=Path(__file__).with_name('worker-drain-reference.h').read_text(encoding='utf-8')
def function(src,name):
 m=re.search(r'^static [^\n]*\b'+name+r'\([^;]*?\)\s*\{',src,re.M);assert m,name
 b=src.index('{',m.start())+1;d=1
 while d:d+=(src[b]=='{')-(src[b]=='}');b+=1
 return src[m.start():b]
types='\n'.join([re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0], 'typedef struct ShaderLayouts ShaderLayouts;',re.search(r'typedef struct ShaderSource \{.*?\} ShaderSource;',g,re.S)[0],re.search(r'typedef struct VkSpecDone \{.*?\} VkSpecDone;',s,re.S)[0]])
globals=s[s.index('static VkSpecDone vk_spec_ready['):s.index('static void vk_spec_ready_remove_order(')]
shared=['vk_spec_find','vk_spec_ready_remove_order','vk_spec_ready_clear','vk_spec_ready_take_at','vk_spec_take_ready']
functions='\n'.join(function(s,n) for n in shared)+'\n'+function(reference,'vk_spec_ready_push').replace('vk_spec_ready_push','reference_push')+'\n'+function(reference,'vk_spec_drain').replace('vk_spec_ready_push','reference_push').replace('vk_spec_drain','reference_drain')
current=function(s,'vk_spec_drain')
view_drain='static VkSpecDone *vk_spec_ready_slot(' in s and '--copy-control' not in sys.argv[3:]
slot=None
if '--copy-control' in sys.argv[3:]:
 current=function(reference,'vk_spec_drain').replace('vk_spec_ready_push','reference_push')
elif view_drain:
 slot=function(s,'vk_spec_ready_slot');functions+='\n'+slot
else:
 slot=function(s,'vk_spec_ready_push').replace('vk_spec_ready_push','current_push')
 functions+='\n'+slot
 current=current.replace('vk_spec_ready_push','current_push')
functions+='\n'+current
# Reuse the already-tested placement/clock helpers, without its queue-specific gate.
helpers=Path(__file__).with_name('test-worker-direct.c').read_text(encoding='utf-8')
helpers=helpers[helpers.index('static void pin_thread('):helpers.index('static retro_atomic_int_t started,start_gate;')]
code=Path(__file__).with_suffix('.c').read_text(encoding='utf-8-sig').replace('/* TYPES */',types).replace('/* GLOBALS */',globals).replace('/* FUNCTIONS */',functions).replace('/* BENCH HELPERS */',helpers)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='worker-drain-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_bytes(b'#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_bytes(b'typedef struct MString MString;\n');(p/'test.c').write_bytes(code.encode());exe=p/('test.exe' if os.name=='nt' else 'test')
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),str(repo/'libretro-common/queues/retro_spsc.c'),'-o',str(exe)]
 if view_drain: command.append('-DVIEW_DRAIN')
 for checked in (True,False):
  flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else (['-DBENCHMARK'] if '--benchmark' in sys.argv[3:] else [])
  subprocess.run(command+flags,env=env,check=True);print('UBSan' if checked else 'optimized',flush=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
 if '--copy-control' not in sys.argv[3:]:
  if view_drain:
   slot_call=re.search(r'VkSpecDone \*d = vk_spec_ready_slot\([^;]*\);',current)[0]
   free='pgraph_vk_shader_source_unref(evicted);'
   free_target=current
   if free not in current:
    free='pgraph_vk_shader_source_unref(vk_spec_ready[slot].spirv);';free_target=slot
   controls=[
    ('missing wrapped copy',current,'memcpy((char *)d + first_bytes, second, bytes - first_bytes);','/* missing wrapped copy */'),
    ('early queue release',current,slot_call,slot_call+' retro_spsc_read_end(&vk_spec_out, bytes);'),
    ('missing state marker',current,'if (d->state_hash)', 'if (false)'),
    ('leaked eviction',free_target,free,'/* leaked eviction */')]
  else:
   controls=[
    ('missing ready copy',slot,'vk_spec_ready[slot] = *done;','(void)done;'),
    ('missing state marker',current,'if (d.state_hash)', 'if (false)'),
    ('leaked eviction',slot,'pgraph_vk_shader_source_unref(vk_spec_ready[slot].spirv);','/* leaked eviction */')]
  for label,target,old,new in controls:
   assert old in target,label
   mutant=code.replace(target,target.replace(old,new,1),1);assert mutant!=code,label
   (p/'test.c').write_bytes(mutant.encode());subprocess.run(command+['-DCHECKED'],env=env,check=True)
   result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=120)
   assert result.returncode and b'FAIL line' in result.stderr,(label,result.returncode,result.stdout,result.stderr)
   print('Required negative control rejected:',label,flush=True)
