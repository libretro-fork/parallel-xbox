#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Required exact ownership, bitmap, watchdog and child-retirement controls."""
from pathlib import Path
import ast,contextlib,io,os,re,subprocess,sys,tempfile,time,types

r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
# This is current test machinery, including when it checks an older renderer.
source=Path(__file__).with_name('test-spv-vsh-gpu.c').read_text(encoding='utf-8')
start=source.index('static bool fixture_case_owned(');end=source.index('\n}',start)+2
schedule=source[start:end]
driver=Path(__file__).with_name('test-spv-vsh-gpu.py').read_text(encoding='utf-8')
tree=ast.parse(driver)
functions=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in ['assigned_cases','run_balanced']]
assert len(functions)==2
namespace=dict(os=os,re=re,time=time,subprocess=subprocess)
exec(compile(ast.Module(body=functions,type_ignores=[]),'<actual vertex runner>','exec'),namespace)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
template=Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='vsh-schedule-') as tmp:
 d=Path(tmp);c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 deadline=time.monotonic()+300
 def remaining():
  value=deadline-time.monotonic();assert value>0,'shared scheduling control deadline';return value
 def build(text):
  c.write_text(template.replace('/* PRODUCTION_SCHEDULE */',text),encoding='utf-8')
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(c),'-o',str(exe)],env=env,check=True,timeout=remaining())
 build(schedule)
 subprocess.run([str(exe)],env=env,check=True,timeout=remaining())
 for before,after in [('(block/slots)%rounds==round','true'),('block/(slots*rounds)','block/slots'),('%slots==slot','%slots==0'),('unsigned block=(test-first)/16','unsigned block=test/16')]:
  assert schedule.count(before)==1
  build(schedule.replace(before,after));result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=remaining());assert result.returncode!=0,(before,result)
  print('Required vertex ownership fault rejected:',before,flush=True)
 build(schedule)
 original_popen=subprocess.Popen
 def run(fault):
  children=[];killed=set();waited=set()
  def launch(*args,**kwargs):
   if fault=='spawn' and children:raise OSError('required injected spawn failure')
   child=original_popen(*args,**kwargs);children.append(child)
   kill=child.kill;wait=child.wait
   def tracked_kill():killed.add(child.pid);return kill()
   def tracked_wait(*a,**k):
    if not a and not k:waited.add(child.pid)
    return wait(*a,**k)
   child.kill=tracked_kill;child.wait=tracked_wait
   return child
  namespace['subprocess']=types.SimpleNamespace(Popen=launch,CalledProcessError=subprocess.CalledProcessError,STDOUT=subprocess.STDOUT)
  namespace['time']=types.SimpleNamespace(monotonic=iter([0,301,301,301,301]).__next__) if fault=='timeout' else time
  old=os.environ.get('VSH_SCHEDULE_FAULT');os.environ['VSH_SCHEDULE_FAULT']=fault
  failed=False
  try:
   with contextlib.redirect_stdout(io.StringIO()):namespace['run_balanced'](exe,d,3,100,2)
  except (AssertionError,OSError,subprocess.TimeoutExpired,subprocess.CalledProcessError):failed=True
  finally:
   if old is None:os.environ.pop('VSH_SCHEDULE_FAULT',None)
   else:os.environ['VSH_SCHEDULE_FAULT']=old
  assert failed==bool(fault),(fault,failed)
  assert children and all(child.poll() is not None for child in children),(fault,'unretired child')
  assert waited=={child.pid for child in children},(fault,'missing terminal wait')
  if fault in ['spawn','timeout']:assert killed=={child.pid for child in children},(fault,'missing live-child kill')
 run('')
 for fault in ['bitmap','count','checks','missing','duplicate','exit','timeout','spawn']:
  run(fault);print('Required vertex runner fault rejected:',fault,flush=True)
print('Vertex runner: exact bitmaps/counts, shared watchdog and all child retirement controls PASS',flush=True)
