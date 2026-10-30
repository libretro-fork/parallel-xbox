# SPDX-License-Identifier: GPL-2.0-or-later
"""Check exact vertex fetch spans and actual GL/VK range plumbing."""
from pathlib import Path
import re,os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
def function(s,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:uint64_t|void) '+name+r'\(',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
u=(r/'hw/xbox/nv2a/pgraph/util.h').read_text();v=(r/'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text();g=(r/'hw/xbox/nv2a/pgraph/gl/vertex.c').read_text()
def block(s):
 a=s.index('            start = attrib_data_addr + min_element * stride;');b=s.index('\n        }\n\n        uint32_t provoking_element_index',a);return s[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* HELPER */',function(u,'pgraph_vertex_fetch_span'))
code=code.replace('/* UPDATERS */',function(g,'update_memory_buffer').replace('update_memory_buffer','gl_update_memory_buffer')+'\n'+function(v,'update_memory_buffer').replace('update_memory_buffer','vk_update_memory_buffer'))
code=code.replace('/* GL BLOCK */',block(g).replace('update_memory_buffer','gl_update_memory_buffer')).replace('/* VK BLOCK */',block(v).replace('update_memory_buffer','vk_update_memory_buffer'))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vertex-span-',dir=r.parent) as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
