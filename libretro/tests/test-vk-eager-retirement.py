# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production eager submission and command-slot retirement."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile,ctypes,shutil
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
env=dict(os.environ);tool=shlex.split(cc)[0];env['PATH']=str(Path(shutil.which(tool)or tool).resolve().parent)+os.pathsep+env.get('PATH','')
source=Path(os.environ.get('VK_EAGER_SOURCE',root/'hw/xbox/nv2a/pgraph/vk/draw.c')).read_text()
def function(name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in ['poll_completed_draw_submissions','pgraph_vk_reclaim_retired_images','pgraph_vk_slot_retired','pgraph_vk_settle_poll','pgraph_vk_reclaim_command_buffer','pgraph_vk_wait_all_slots','eager_submit_after_draw']))
has_objects='pgraph_vk_shader_objects_retire(r);' in source
if (root/'hw/xbox/nv2a/pgraph/vk/shader-object.inc').exists():assert has_objects,'current SO owners lost their retirement consumer'
code=code.replace('/* CAPABILITIES */',f'#define HAVE_SHADER_OBJECT_RETIRE {int(has_objects)}')
assert 'eager_submit_after_draw(pg);' in function('pgraph_vk_draw_end')
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='vk-eager-retirement-',dir=root.parent) as tmp:
 t=Path(tmp);(t/'test.c').write_text(code);binary=os.environ.get('VK_EAGER_BINARY',str(t/'test'))
 flags=shlex.split(os.environ.get('VK_EAGER_CFLAGS','' if win else '-fsanitize=address,undefined'))
 command=shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(t/'test.c'),'-o',binary]+(['-static'] if win else [])
 def run(text,negative=False):
  (t/'test.c').write_text(text);subprocess.run(command,check=True,env=env)
  if os.environ.get('VK_EAGER_SKIP_RUN'):return
  result=subprocess.run([binary],capture_output=True,text=True,timeout=30,env=env)
  if negative:assert result.returncode and 'assert' in (result.stdout+result.stderr).lower(),'required retirement fault escaped without assertion'
  else:assert result.returncode==0,result.stdout+result.stderr;print(result.stdout,end='')
 run(code)
 if has_objects and not os.environ.get('VK_EAGER_SKIP_RUN'):
  omitted=code.replace('    pgraph_vk_shader_objects_retire(r);','    /* omitted SO retirement */')
  moved=code.replace('    pgraph_vk_shader_objects_retire(r);','    /* moved SO retirement */',1).replace('    lr_gpu_ram_drop((uintptr_t)r->slot_main[slot]);','    pgraph_vk_shader_objects_retire(r);\n    lr_gpu_ram_drop((uintptr_t)r->slot_main[slot]);',1)
  for name,mutant in [('SO retirement omitted',omitted),('SO retirement before command drops',moved)]:
   assert mutant!=code;run(mutant,True);print('Required retirement fault rejected:',name)
