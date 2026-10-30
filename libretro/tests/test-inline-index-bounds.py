# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual index packet handlers, bounds maintenance and post-load hook."""
from pathlib import Path
import re,os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
def function(s,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:void|int) '+name+r'\(',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
def method(s,name):
 a=s.index('DEF_METHOD(NV097, '+name+')');b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b].replace('DEF_METHOD(NV097, '+name+')','static void method_'+name+'(NV2AState *d, PGRAPHState *pg, unsigned subchannel, unsigned method, uint32_t parameter, uint32_t *parameters, size_t num_words_available, size_t *num_words_consumed, bool inc)')
v=(r/'hw/xbox/nv2a/pgraph/vertex.c').read_text();p=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/pgraph.h').read_text();n=(r/'hw/xbox/nv2a/nv2a.c').read_text()
parts=[function(h,'pgraph_update_inline_elements_bounds')]+[function(v,x) for x in ['pgraph_reset_draw_arrays','pgraph_reset_inline_buffers','pgraph_append_inline_elements_range','pgraph_rebuild_inline_elements_bounds']]+[function(p,'pgraph_expand_draw_arrays')]+[method(p,x) for x in ['ARRAY_ELEMENT16','ARRAY_ELEMENT32','DRAW_ARRAYS']]+[function(n,'nv2a_post_load')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='index-bounds-',dir=r.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
