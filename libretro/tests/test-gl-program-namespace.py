# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual memo generations must not outlive their final program/pipeline owner."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text();u=(r/'hw/xbox/nv2a/pgraph/glsl/uniform-cache.c').read_text()
def fn(source,name):
 m=re.search(r'^(?:static )?(?:void|int|bool) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 end=source.index('{',m.start())+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]
names=['delete_program','shader_module_cache_entry_post_evict','shader_cache_entry_post_evict']
if 'static void forget_binding_program_uniforms(' in s:names.insert(1,'forget_binding_program_uniforms')
parts='\n'.join(fn(u,n) for n in ['uniform_cache_slot','uniform_cache_forget','uniform_cache_invalidate','uniform_cache_apply'])+'\n'+'\n'.join(fn(s,n) for n in names)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',parts)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-program-namespace-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),str(src),'-o',str(exe)]
 if os.name!='nt':flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 src.write_text(code);subprocess.run(flags,env=env,check=True);result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60);assert not result.returncode,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
 if 'forget_binding_program_uniforms' in names:
  helper=fn(s,'forget_binding_program_uniforms')
  for forbidden in ['glFinish','glGetProgram','glLinkProgram','glDeleteProgram','malloc','calloc']:assert forbidden not in helper,forbidden
  body=fn(s,'shader_cache_entry_post_evict');call='    forget_binding_program_uniforms(r, binding);';assert body.count(call)==1
  mutant=code.replace(call,'    if (false) forget_binding_program_uniforms(r, binding);',1);src.write_text(mutant);subprocess.run(flags,env=env,check=True);bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60);assert bad.returncode,'missing final-owner invalidation escaped';print('Required control rejected: memo repopulated after early deletion survives final owner')

  for name,before,after in [
   ('vertex memo left behind','binding->gl_program, binding->gl_prog_vs,','binding->gl_program, 0,'),
   ('fragment memo left behind','binding->gl_prog_fs, binding->gl_prog_gs,','0, binding->gl_prog_gs,'),
   ('geometry memo left behind','binding->gl_prog_fs, binding->gl_prog_gs,','binding->gl_prog_fs, 0,'),
   ('unrelated live memo invalidated',call,'    uniform_cache_invalidate(&r->uni_cache); if(false) forget_binding_program_uniforms(r, binding);'),
  ]:
   assert code.count(before)==1;src.write_text(code.replace(before,after,1));subprocess.run(flags,env=env,check=True);bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60);assert bad.returncode,name;print('Required control rejected:',name)
