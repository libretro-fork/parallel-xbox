# SPDX-License-Identifier: GPL-2.0-or-later
"""Production vertex upload copies and in-place sync against byte/page oracles."""
from pathlib import Path
import re,os,sys,subprocess,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
def function(name):
 m=re.search(r'^static (?:int|void) '+name+r'\(',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
structs='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',s,re.S)[0] for n in ['VertexCowDispatch','VertexBufferRemap'])
v=(r/'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text();m=re.search(r'^static void update_memory_buffer\(.*?^}',v,re.M|re.S);assert m
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',structs).replace('/* PRODUCTION */','\n'.join(function(n) for n in ['compare_memory_sync_requirement_by_addr','merge_vertex_sync_ranges','sync_vertex_ram_buffer','copy_remapped_attributes_to_inline_buffer'])+'\n'+m[0])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-vertex-copy-',dir=r.parent) as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
