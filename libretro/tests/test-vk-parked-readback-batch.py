# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compare production texture-producer batching with the previous per-image waits."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
s=(r/'hw/xbox/nv2a/pgraph/vk/surface.c').read_text();parts=[]
for name in ['check_surface_overlaps_range','queue_surface_readback_range','pgraph_vk_download_surface_ranges_if_dirty','pgraph_vk_download_surfaces_in_range_if_dirty','download_settled','readback_batch_cmd','readback_batch_flush','readback_batch_add','readback_batch_begin','readback_batch_end','parked_owed_settled','parked_materialize','parked_materialize_range','pgraph_vk_surface_download_if_dirty']:
 match=re.search(r'^(?:static )?(?:bool|void|VkCommandBuffer) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert match,name;a=match.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();batch=re.search(r'struct \{\s*bool open;\s*bool recording;.*?\} readback_batch;',h,re.S).group(0)
range_type=re.search(r'typedef struct MemorySyncRequirement \{.*?\} MemorySyncRequirement;',h,re.S).group(0)
code=Path(__file__).with_suffix('.c').read_text().replace('/* RANGE TYPE */',range_type).replace('/* BATCH TYPE */',batch).replace('/* PRODUCTION */','\n'.join(parts))
q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',q[a:b])
with tempfile.TemporaryDirectory(prefix='vk-parked-batch-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,timeout=30,env=env)
