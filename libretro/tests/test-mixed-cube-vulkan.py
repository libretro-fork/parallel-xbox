# SPDX-License-Identifier: GPL-2.0-or-later
"""Real Vulkan byte oracle for production mixed-cube provenance and face copies."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2];parts=[]
for path,names in [('image.c',['check_format_has_depth_component','check_format_has_stencil_component','pgraph_vk_transition_image_layout']),('texture.c',['preserve_cube_upload_sources','record_surface_copy','copy_cube_faces_to_texture'])]:
 s=(r/'hw/xbox/nv2a/pgraph/vk'/path).read_text()
 for name in names:
  m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
  a=m.start();b=s.index('{',a)+1;depth=1
  while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
  parts.append(s[a:b])
common=(r/'libretro/tests/test-vulkan-linear-span.c').read_text()
a=common.index('static VkDevice device;');b=common.index('int main(void)',a)
helpers=common[a:b]
a=common.index('    const char *layers[]');b=common.index('    const VkFormat formats[]',a)
init=common[a:b].replace('LINEAR_SPAN_NO_VALIDATION','CUBE_COPY_NO_VALIDATION').replace('Vulkan linear staging-span oracle','Vulkan mixed cube oracle')
code=Path(__file__).with_suffix('.c').read_text().replace('/* GPU COMMON */',helpers).replace('/* GPU INIT */',init).replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-mixed-cube-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 link=[str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-parameter','-Wno-maybe-uninitialized','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),*link,'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=120)
