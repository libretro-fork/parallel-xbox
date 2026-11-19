# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute production staging copies and memory-property refresh with visibility spies."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
table=(r/'hw/xbox/nv2a/pgraph/vk/buffer.c').read_text()
parts=[re.search(r'const StagedBuffer pgraph_vk_staged_buffers\[3\] = \{.*?\};',table,re.S)[0]]
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
header=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
gs='if (r->has_gs_ubo_binding)' in draw
vertex='if (b_dst->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)' in draw
if 'VK_READY_GEOMETRY_STAGES' in header:
 assert gs,'current immutable GS ABI lost its staging consumer'
if 'VK_READY_VERTEX_STAGES' in header:
 assert vertex,'current constant vertex ABI lost its staging consumer'
for path,names in [('buffer.c',['create_buffer','destroy_buffer','recreate_buffer']),('draw.c',['sync_staging_buffers']),('command.c',['pgraph_vk_staging_flush','pgraph_vk_upload_source_barrier']),('surface.c',['invalidate_readback'])]:
 s=(r/'hw/xbox/nv2a/pgraph/vk'/path).read_text()
 for name in names:
  m=re.search(r'^(?:static )?void '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name;a=m.start();b=s.index('{',a)+1;depth=1
  while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
  parts.append(s[a:b])
unswizzle=(r/'hw/xbox/nv2a/pgraph/vk/unswizzle.c').read_text()
a=unswizzle.index('    /* Host and earlier GPU writes')
b=unswizzle.index('    vkCmdBindPipeline',a)
parts.append('static void unswizzle_source_barriers(PGRAPHVkState *r,VkCommandBuffer cmd,VkDeviceSize dst,size_t out_size,VkDeviceSize src_offset,size_t texels,unsigned bpp,VkDeviceSize palette_offset){'+unswizzle[a:b]+'}')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts));q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',q[a:b])
code=code.replace('/* CAPABILITIES */',f'#define HAVE_GS_ABI {int(gs)}\n#define HAVE_UNIFORM_VERTEX {int(vertex)}')
with tempfile.TemporaryDirectory(prefix='vk-staging-visibility-') as tmp:
 d=Path(tmp);exe=d/('test.exe' if os.name=='nt' else 'test')
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-o',str(exe)]
 def run(text,negative=False):
  (d/'test.c').write_text(text);subprocess.run(command,check=True,env=env)
  result=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30,env=env)
  if negative:assert result.returncode,'required staging control escaped'
  else:assert result.returncode==0,result.stdout.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 controls=[]
 if gs:controls += [('missing GS visibility','dst_stage_mask |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;','dst_stage_mask |= 0;'),('mutable GS route replaces immutable ABI','if (r->has_gs_ubo_binding)','if (r->shader_objects.enabled)')]
 if vertex:controls += [('missing constant vertex visibility','dst_access_mask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;','dst_access_mask |= 0;'),('mutable route replaces recorded vertex usage','if (b_dst->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)','if (r->shader_objects.enabled)')]
 for name,before,after in controls:
  assert code.count(before)==1,(name,code.count(before));run(code.replace(before,after),True);print('Required negative control rejected:',name)
