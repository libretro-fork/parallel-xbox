# SPDX-License-Identifier: GPL-2.0-or-later
"""Production cache publication preserves mapped readers and old good files."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
h=r/'hw/xbox/nv2a/pgraph/gl/cache-file.h';header=h.read_text()
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
for name in ('module_write_to_disk','shader_write_to_disk'):
 a=s.index('static void '+name+'(');b=s.index('\n}',a);body=s[a:b]
 assert 'gl_cache_file_begin(' in body and 'gl_cache_file_finish(' in body
 for forbidden in ('g_strdup','g_new','g_malloc','fopen_utf8','qemu_unlink'):
  assert forbidden not in body,(name,forbidden)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-cache-file-') as tmp:
 d=Path(tmp);override=d/'hw/xbox/nv2a/pgraph/gl/cache-file.h';override.parent.mkdir(parents=True)
 exe=d/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),str(Path(__file__).with_suffix('.c')),'-o',str(exe)]
 if os.name!='nt':flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 def run(text,negative=False):
  override.write_text(text);subprocess.run(flags,env=env,check=True)
  folder=d/('case'+str(len(list(d.glob('case*')))));folder.mkdir()
  result=subprocess.run([str(exe),str(folder)],env=env,capture_output=True,timeout=60)
  if negative:assert result.returncode,'mutation escaped'
  else:assert result.returncode==0,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
 run(header)
 for label,before,after in [
  ('truncate published file','fopen_utf8(slot->temporary_path, "wb")','fopen_utf8(slot->path, "wb")'),
  ('publish incomplete write','if (ok && g_rename','if ((ok || true) && g_rename'),
  ('ignore failed close','if (fclose(file) != 0) ok = false;','(void)fclose(file);'),
  ('remove old good file on failure','qemu_unlink(slot->temporary_path);','qemu_unlink(slot->path);'),
  ('share temporary across slots','process, (uintptr_t)slot','process, (uintptr_t)0'),
 ]:
  assert header.count(before)==1,label
  run(header.replace(before,after,1),True);print('Required negative control rejected:',label)
