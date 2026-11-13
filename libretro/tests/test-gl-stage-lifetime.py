# SPDX-License-Identifier: GPL-2.0-or-later
"""Real GL program lifetime under the production module-cache eviction path."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
def fn(name):
 m=re.search(r'^static void '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
a=fn('shader_module_cache_entry_post_evict');body=fn('delete_program')+'\n'+a
for wait in ('glGetProgramiv','GL_LINK_STATUS','GL_COMPLETION_STATUS','glFinish','glDeleteProgramPipelines'):
 assert wait not in body,wait
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
with tempfile.TemporaryDirectory(prefix='gl-stage-lifetime-') as tmp:
 p=Path(tmp);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 def run(text,negative=False):
  src.write_text(text)
  flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
  extra=[]
  if os.name=='nt':
   flags+=['-Wno-cast-function-type','-Wno-unused-parameter','-I'+str(r),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen')]
   extra=[str(r/x) for x in ('ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c')]+['-lopengl32','-lgdi32','-luser32']
  else:extra=['-lEGL','-lGL']
  subprocess.run(flags+[str(src)]+extra+['-o',str(exe)],check=True,env=env)
  result=subprocess.run([str(exe)],env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,timeout=60)
  if negative:assert result.returncode,'mutation escaped'
  else:assert result.returncode==0,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 for label,before,after in [
  ('leaked separable program','delete_program(r, module->gl_stage_program);','(void)delete_program; (void)r;'),
  ('stale uniform memo','uniform_cache_forget(&r->uni_cache, prog);','(void)uniform_cache_forget; (void)r;'),
  ('leaked pending shader','glDeleteShader(module->gl_shader);\n            module->gl_shader = 0;','module->gl_shader = 0;'),
 ]:
  assert code.count(before)==1,label
  run(code.replace(before,after,1),True);print('Required negative control rejected:',label)
