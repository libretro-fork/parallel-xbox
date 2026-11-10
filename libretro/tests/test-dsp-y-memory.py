# SPDX-License-Identifier: GPL-2.0-or-later
"""Build real interpreter/JIT memory tests and require read/write fault controls."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
opts=['CC='+cc]
if '__aarch64__' in macros:opts+=['ARCH=aarch64']
if '_WIN32' in macros:opts+=['platform=win']
out=subprocess.check_output(['make','-Bn',*opts,'dspymemory'],cwd=r,text=True)
commands=[shlex.split(line) for line in out.splitlines() if ' -o ' in line and 'test-dsp-y-memory.c' in line]
assert len(commands)==1
args=commands[0]+shlex.split(os.environ.get('DSP_Y_CFLAGS','-fsanitize=address,undefined'))
source=r/'hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.c'
original=source.read_text()
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='dsp-y-memory-') as d:
 p=Path(d);binary=os.environ.get('DSP_Y_BINARY',str(p/'test'));args[args.index('-o')+1]=binary
 subprocess.run(args,cwd=r,check=True)
 if not os.environ.get('DSP_Y_SKIP_RUN'):
  runner=shlex.split(os.environ.get('DSP_Y_RUNNER',''))
  subprocess.run(runner+[binary],check=True,timeout=120)
  index=next(i for i,arg in enumerate(args) if arg.endswith('/interp/dsp_cpu.c'))
  args[index]=str(p/'dsp_cpu.c')
  for name,mutant in [
   ('unmapped read floats high',original.replace('return address < DSP_YRAM_SIZE ? dsp->yram[address] : 0;', 'return address < DSP_YRAM_SIZE ? dsp->yram[address] : 0xffffff;')),
   ('unmapped write corrupts program RAM',original.replace('dsp->yram[address] = value;\n        }','dsp->yram[address] = value;\n        } else { dsp->pram[address % DSP_PRAM_SIZE] = value; }'))]:
   assert mutant!=original,name
   (p/'dsp_cpu.c').write_text(mutant);subprocess.run(args,cwd=r,check=True)
   result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
   assert result.returncode and b'assertion' in result.stdout.lower(),(name,result.stdout)
   print('Required negative control rejected:',name,flush=True)
