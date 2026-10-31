# SPDX-License-Identifier: GPL-2.0-or-later
"""Production stop-request publication/consumption with real common threads."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'system/runstate.c').read_text();types=(r/'libretro/gen/qapi/qapi-types-run-state.h').read_text()
enum=types[types.index('typedef enum RunState {'):types.index('} RunState;')+len('} RunState;')]
def function(name):
 m=re.search(r'^(?:bool|void) '+name+r'\(',s,re.M);assert m,name
 a=s.index('{',m.start());z=a+1;depth=1
 while depth:
  depth+=(s[z]=='{')-(s[z]=='}');z+=1
 return s[m.start():z]
production=re.search(r'^static retro_atomic_int_t vmstop_requested.*;',s,re.M).group()+'\n'+'\n'.join(function(n) for n in ('qemu_vmstop_requested','qemu_system_vmstop_request'))
code=Path(__file__).with_suffix('.c').read_text().replace('/* ENUM */',enum).replace('/* PRODUCTION */',production)
with tempfile.TemporaryDirectory(prefix='vmstop-mailbox-') as t:
 p=Path(t);(p/'test.c').write_text(code);binary=Path(os.environ.get('VMSTOP_BINARY',str(p/'test')))
 flags=shlex.split(os.environ.get('VMSTOP_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
 cmd=[cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DHAVE_THREADS',*flags,'-I'+str(r/'libretro-common/include'),str(p/'test.c'),str(r/'libretro-common/rthreads/rthreads.c'),str(r/'libretro-common/features/features_cpu.c'),str(r/'libretro-common/time/rtime.c'),'-pthread','-o',str(binary)]
 if 'mingw' in cc:cmd.insert(1,'-Wno-cast-function-type')
 subprocess.run(cmd,check=True)
 if not os.environ.get('VMSTOP_SKIP_RUN'):
  subprocess.run([str(binary)],check=True,timeout=90)
