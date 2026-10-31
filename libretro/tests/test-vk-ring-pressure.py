# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise production ring allocation and aux retirement with byte-level ownership."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
s=(r/'hw/xbox/nv2a/pgraph/vk/command.c').read_text();parts=[]
for name in ['aux_retire_oldest','pgraph_vk_wait_for_aux','pgraph_vk_begin_single_time_commands','ring_tail','pgraph_vk_ring_alloc','pgraph_vk_end_single_time_commands_async']:
 match=re.search(r'^(?:static )?(?:bool|void|VkCommandBuffer) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert match,name;a=match.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
ring_map=re.search(r'static const int ring_buffer\[PGRAPH_VK_RINGS\] = \{.*?\};',s,re.S).group(0)
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();constants='\n'.join(re.findall(r'^#define PGRAPH_VK_(?:AUX_SLOTS|RING_STAGING|RING_UNSWIZZLE|RINGS) .+$',h,re.M));aux=re.search(r'struct PGRAPHVkAuxSlot \{.*?\} aux\[PGRAPH_VK_AUX_SLOTS\];',h,re.S).group(0)
code=Path(__file__).with_suffix('.c').read_text().replace('/* CONSTANTS */',constants).replace('/* AUX TYPE */',aux).replace('/* PRODUCTION */',ring_map+'\n'+'\n'.join(parts))
q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',q[a:b])
with tempfile.TemporaryDirectory(prefix='vk-ring-pressure-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,timeout=30,env=env)
