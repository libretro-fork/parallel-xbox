# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production Vulkan uniform change tracking and UBO upload planning.
Upload planning and descriptor reuse are extracted from production; selected
dynamic offsets are read from bounded RAM.
Usage: python test-uniform-stage-uploads.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?void '+name+r'\(',s,re.M);assert m
 at=s.index('{',m.start());end=at+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
upload=function('pgraph_vk_update_descriptor_sets')
upload=upload[:upload.index('    assert(r->descriptor_set_index <')]+ '    r->descriptor_set_index++;\n}'
upload=upload.replace('    VkWriteDescriptorSet descriptor_writes[2 + NV2A_MAX_TEXTURES];','')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',upload+'\n'+function('update_shader_uniforms_tracked'))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='uniform-stage-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
