# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production u16 index packing and conservative topology selection."""
from pathlib import Path

vk_loader_root = Path(__file__).resolve().parents[2]
import os,re,shlex,struct,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
def function(path,name):
 s=(r/path).read_text();m=re.search(r'^(?:static )?(?:bool|VkDeviceSize) '+name+r'\(',s,re.M);assert m
 start=s.index('{',m.start());end=start+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
body=function('hw/xbox/nv2a/pgraph/vk/vertex.c','pgraph_vk_update_index_buffer_u16')+'\n'+function('hw/xbox/nv2a/pgraph/vk/draw.c','inline_indices_use_u16')
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
assert 'use_u16 ? VK_INDEX_TYPE_UINT16 :' in draw
assert 'ROUND_UP(pg->inline_elements_length, 2) * sizeof(uint16_t)' in draw
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='vk-index-packing-') as d:
 p=Path(d);src=p/'test.c';binary=os.environ.get('INDEX_PACK_BINARY',str(p/'test'))
 args=cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r)]+shlex.split(os.environ.get('INDEX_PACK_CFLAGS','-fsanitize=address,undefined'))
 def build(text):
  src.write_text(text);subprocess.run(args+[str(src),'-o',binary],check=True)
 build(code)
 if not os.environ.get('INDEX_PACK_SKIP_RUN'):
  runner=shlex.split(os.environ.get('INDEX_PACK_RUNNER',''))
  subprocess.run(runner+[binary],check=True,timeout=60)
  for name,mutant in [
   ('restart index narrowed',code.replace('max_index >= 0xffff','max_index > 0xffff')),
   ('odd count loses alignment',code.replace('ROUND_UP(count, 2)','count')),
   ('high index byte lost',code.replace('dst[i] = indices[i];','dst[i] = indices[i] & 255;'))]:
   assert mutant!=code,name
   build(mutant)
   result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
   assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
   print('Required negative control rejected:',name,flush=True)
 if '--gpu' in sys.argv[3:]:
  shader=p/'index.vert';spv=p/'index.spv'
  shader.write_text('#version 450\nlayout(std430,binding=0) buffer Results { uint value[]; };\nvoid main(){uint i=uint(gl_VertexIndex);atomicExchange(value[i],i^0x12345678u);gl_Position=vec4(0,0,0,1);gl_PointSize=1.0;}\n')
  subprocess.run(['glslangValidator','-V',str(shader),'-o',str(spv)],check=True)
  raw=spv.read_bytes();words=struct.unpack('<'+'I'*(len(raw)//4),raw)
  helpers=(vk_loader_root/'libretro/tests/spv-clear-vulkan.h').read_text().split('static VkPipeline pipeline(')[0]
  gpu='#include <stdlib.h>\n#include <vk-test-loader.h>\n#define main cpu_index_test\n'+code+'\n#undef main\n'+helpers
  gpu+='\nstatic const uint32_t index_vertex_words[]={'+','.join(map(str,words))+'};\n'
  gpu+=(vk_loader_root/'libretro/tests/test-vk-index-packing-gpu.c').read_text();src.write_text(gpu)
  subprocess.run(cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-I'+str(r),'-I'+str(vk_loader_root/'libretro'),'-I'+str(r/'deps/vulkan-headers'),str(src),str(vk_loader_root/'libretro/vk_dispatch.c'),str(vk_loader_root/'libretro/tests/vk-test-loader.c'),'-ldl','-o',binary],check=True)
  subprocess.run([binary],check=True,timeout=60)
