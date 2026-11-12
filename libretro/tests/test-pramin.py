# SPDX-License-Identifier: GPL-2.0-or-later
"""Production PRAMIN spans/MMIO/mode handling against an independent byte oracle."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile,time
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/nv2a.c').read_text();a=s.index('static void nv2a_pramin_set_mode(');b=s.index('static const MemoryRegionOps nv2a_pramin_ops',a)
code=s[a:b]
a=s.index('static void nv2a_migration_release(');b=s.index('static void nv2a_reset(',a);code+=s[a:b]
fixture=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',(r/'hw/xbox/nv2a/ramin.h').read_text()+code)
assert 'VMSTATE_BOOL_V(pramin_unified, NV2AState, 6)' in s
assert '.version_id = 6,' in s
assert 'VMSTATE_BOOL(migration_parked' not in s and 'VMSTATE_BOOL_V(migration_parked' not in s
assert 'd->migration_parked = true;' in s
for path in ['hw/xbox/nv2a/pfifo.c','hw/xbox/nv2a/pgraph/pgraph.c']:
 assert 'ramin_ptr' not in (r/path).read_text()
with tempfile.TemporaryDirectory(prefix='pramin-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 binary=os.environ.get('PRAMIN_BINARY',str(p/'test'));flags=shlex.split(os.environ.get('PRAMIN_CFLAGS','-static' if 'mingw' in cc else '-fsanitize=address,undefined'))
 subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('PRAMIN_SKIP_RUN'):
  # These storage modes have separate RAM owners and no shared state. Keep
  # all 9,437,172 address/width cases while using native ARM64 runner cores.
  # One shared deadline includes launch and all three children, not 60s each.
  children=[];deadline=time.monotonic()+60
  try:
   for mode in ('unified','legacy64','legacy128'):
    children.append((mode,subprocess.Popen([binary,mode])))
   for mode,child in children:
    result=child.wait(timeout=max(0,deadline-time.monotonic()))
    if result:raise subprocess.CalledProcessError(result,[binary,mode])
  except subprocess.TimeoutExpired:
   for mode,child in children:
    if child.poll() is not None:continue
    for name in ('stat','wchan'):
     sample=Path('/proc')/str(child.pid)/name
     if sample.is_file():print('PRAMIN timeout '+mode+' '+name+': '+sample.read_text()[:1024],flush=True)
   raise
  finally:
   for mode,child in children:
    if child.poll() is None:child.kill()
   for mode,child in children:child.wait()
