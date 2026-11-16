#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Production zero-stride decoder/binder, exact GL transport and key domain.

DMA banks and dirty uploads are bounded models. Actual decoder, displacement,
attribute-shadow and binding functions run. Test-only GPU feedback verifies
floating constants and packed array payloads, not NV2A lighting precision.
"""
from pathlib import Path
import importlib.util,os,re,shutil,subprocess,sys,tempfile,time
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
module=Path(__file__).with_name('test-vsh-interpreter-dynamic.py')
spec=importlib.util.spec_from_file_location('fixture',module);fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
vertex=(root/'hw/xbox/nv2a/pgraph/gl/vertex.c').read_text(encoding='utf-8')
common=(root/'hw/xbox/nv2a/pgraph/vertex.c').read_text(encoding='utf-8')
header=(root/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text(encoding='utf-8')
def function(source,name):
 m=re.search(r'^(?:static )?(?:void|bool|unsigned int) '+name+r'\([^;]*?\)\s*\{',source,re.M);assert m,name
 end=source.index('{',m.start())+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]
production=function(common,'pgraph_update_inline_value')+'\n'+'\n'.join(function(vertex,n) for n in ['pgraph_gl_attr_pointer','pgraph_gl_attr_enable','pgraph_gl_attr_value','attr_gl_format','attr_displacement','pgraph_gl_bind_vertex_attributes'])
shadow=re.search(r'    struct AttrShadow \{.*?\} attr_shadow\[NV2A_VERTEXSHADER_ATTRIBUTES\];',header,re.S)[0]
with tempfile.TemporaryDirectory(prefix='gl-constant-cmp-') as tmp:
 d=Path(tmp);fixture.prepare(root,d,'gl');code=(d/'driver.c').read_text(encoding='utf-8');code=code[:code.index('int main(')]
 for label in ['varyings','prefixed_varyings']:
  start=code.index('const char *'+label+'[]={');end=code.index(';',start)+1
  code=code[:start]+'const char *'+label+'[]={"capture_value","stamp"};'+code[end:]
 code+=Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* FIELDS */',shadow).replace('/* PRODUCTION */',production)
 script=(d/'driver.py').read_text(encoding='utf-8');script=script[:script.index(' # Keep the full growing corpus')]+'\n subprocess.run([str(exe)],env=os.environ,check=True,timeout=180)\n'
 script=script.replace("'-O2','-std=gnu11'","'-O2','-std=gnu11','-fno-strict-aliasing'");(d/'driver.py').write_text(script,encoding='utf-8')
 env=dict(os.environ,EGL_PLATFORM='surfaceless');env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
 deadline=time.monotonic()+300
 faults=[('slow-packed-key','        if (!stride) {\n','        if (needs_conversion) pg->compressed_attrs |= (1 << i);\n        if (!stride) {\n'),('fast-packed-key','if (attr->stride && attr->format ==','if (attr->format =='),('array-key-lost','        if (needs_conversion) {\n            pg->compressed_attrs |= (1 << i);\n        }','        if (false) {\n            pg->compressed_attrs |= (1 << i);\n        }'),('CPU-clamp-lost','MAX(-1.0f, (float)x / 1023.0f)','((float)x / 1023.0f)')]
 for fault in [None,*faults]:
  text=code
  if fault:
   name,before,after=fault;assert text.count(before)==1,(name,text.count(before));text=text.replace(before,after,1)
  (d/'driver.c').write_text(text,encoding='utf-8')
  result=subprocess.run([sys.executable,str(d/'driver.py'),str(root),cc],env=env,capture_output=True,timeout=max(0,deadline-time.monotonic()))
  if fault:
   assert result.returncode!=0 and b'CONSTANT_CMP_MISMATCH' in result.stderr,(name,result.stderr.decode(errors='replace'))
   print('Required constant-CMP fault rejected:',name,flush=True)
  else:
   assert result.returncode==0,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='',flush=True)
