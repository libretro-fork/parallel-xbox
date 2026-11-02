# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compare complete prewarm/helper bodies with frozen f7c329b implementations.
Actual shader/binding/key types, hash canonicalization and stage construction;
modeled renderer services/driver capture. This is not a core or GPU build.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text(encoding='utf-8')
def fn(source,name):
 m=re.search(r'^(?:static )?(?:void|int|uint64_t|VkPipeline|VkPrimitiveTopology) '+name+r'\(',source,re.M);assert m,name
 a=source.index('{',m.start());b=a+1;d=1
 while d:d+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[m.start():b]
reference=Path(__file__).with_name('prewarm-input-reference.h').read_text(encoding='utf-8').replace('gpl_get_pre(', 'reference_get_pre(').replace('pgraph_vk_prewarm_shaders(', 'reference_prewarm(')
current=fn(s,'gpl_get_pre')+'\n'+fn(s,'pgraph_vk_prewarm_shaders')
functions='\n'.join(fn(s,n) for n in ['topology_class','gpl_pre_hash','pre_stages'])+'\n'+reference+'\n'+current
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8')
types=re.search(r'typedef struct LruNode \{.*?\} LruNode;',(repo/'include/qemu/lru.h').read_text(),re.S)[0]+'\n'
types+='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S)[0] for name in ['RenderPassState','PipelineKey','ShaderBinding'])
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='prewarm-input-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir()
 (p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
 (p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 (p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test')
  flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
