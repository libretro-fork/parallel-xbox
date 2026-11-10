# SPDX-License-Identifier: GPL-2.0-or-later
"""Test production GPU reset and IRQ recomputation with stale interrupt state."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(r/'hw/xbox/nv2a/nv2a.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?void '+name+r'\(',s,re.M);assert m
 start=s.index('{',m.start());end=start+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',function('nv2a_update_irq')+'\n'+function('nv2a_reset'))
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='nv2a-reset-irq-') as d:
 p=Path(d);src=p/'test.c';binary=os.environ.get('RESET_IRQ_BINARY',str(p/'test'))
 args=cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r)]+shlex.split(os.environ.get('RESET_IRQ_CFLAGS','-fsanitize=address,undefined'))
 def build(text):
  src.write_text(text);subprocess.run(args+[str(src),'-o',binary],check=True)
 build(code)
 if not os.environ.get('RESET_IRQ_SKIP_RUN'):
  runner=shlex.split(os.environ.get('RESET_IRQ_RUNNER',''))
  subprocess.run(runner+[binary],check=True,timeout=60)
  for name,mutant in [
   ('stale PGRAPH pending bits',code.replace('retro_atomic_store_relaxed_int(&d->pgraph.pending_interrupts, 0);','')),
   ('stale interrupt enables',code.replace('d->pcrtc.enabled_interrupts = 0;','')),
   ('IRQ line never lowered',code.replace('    nv2a_update_irq(d);',''))]:
   assert mutant!=code,name
   build(mutant)
   result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
   assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
   print('Required negative control rejected:',name,flush=True)
