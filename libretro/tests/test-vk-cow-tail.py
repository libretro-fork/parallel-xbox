# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production COW snapshots with immutable-reader and tail oracles."""
from pathlib import Path
import re,os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc';s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();v=(r/'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def function(source,name):
 m=re.search(r'^(?:static )?(?:bool|void|VertexCowCacheEntry \*)\s*'+name+r'\(',source,re.M);assert m,name
 a=source.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[m.start():b]
structs=re.search(r'typedef struct VertexCowCacheEntry \{.*?\} VertexCowCacheEntry;',h,re.S)[0]+'\n'+re.search(r'typedef struct VertexCowDispatch \{.*?\} VertexCowDispatch;',s,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',structs).replace('/* PRODUCTION */',function(v,'pgraph_vk_vertex_cow_cache_invalidate')+'\n'+'\n'.join(function(s,n) for n in ['vertex_cow_remember','vertex_cow_contains','vertex_cow_lookup','vertex_cow_store','vertex_cow_extendable','vertex_cow_snapshot']))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-cow-tail-',dir=r.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
