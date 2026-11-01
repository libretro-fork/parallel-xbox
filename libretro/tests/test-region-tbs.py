# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the production TB index of a code region against a reference.
Usage: python test-region-tbs.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'tcg/region.c').read_text()
def block(m,semi=False):
 at=s.index('{',m.start());end=at+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+(';' if semi else '')
def function(name):
 m=re.search(r'^(?:static )?[\w ]+?\*?\s*'+name+r'\(',s,re.M);assert m,name
 return block(m)
def struct(name):
 m=re.search(r'^struct '+name+r' \{',s,re.M);assert m,name
 return block(m,True)
# These are ownership restrictions, not replacement synchronization.
assert 'QemuMutex' not in s and 'qemu_mutex_' not in s
assert 'g_assert(max_threads == 1);' in s and 'region.n = 1;' in s
machine=(repo/'hw/xbox/xbox.c').read_text();assert re.search(r'm->max_cpus\s*=\s*1;',machine)
accel=(repo/'accel/tcg/tcg-all.c').read_text();assert 'g_assert(ms->smp.max_cpus == 1);' in accel
assert 'tcg_init(s->tb_size * MiB, s->splitwx_enabled, 1);' in accel
assert 'mttcg_enabled' not in accel and 'tcg_set_thread' not in accel
allowed={'tcg_tb_lookup':{'accel/tcg/tb-maint.c','accel/tcg/translate-all.c'},'tcg_tb_insert':{'accel/tcg/translate-all.c'},'tcg_tb_remove':{'accel/tcg/translate-all.c'},'tcg_region_reset_all':{'accel/tcg/tb-maint.c'}}
for tree in ['accel','tcg','system','target','hw','ui']:
 for path in (repo/tree).rglob('*.c'):
  name=path.relative_to(repo).as_posix()
  if name=='tcg/region.c':continue
  text=path.read_text(encoding='utf-8',errors='replace')
  for fn,callers in allowed.items():
   if re.search(r'\b'+fn+r'\s*\(',text):assert name in callers,(fn,name,'audit region ownership before adding a caller')
parts=[struct('tcg_region_tb'),struct('tcg_region_tree')]+[function(n) for n in
 ['tb_destroy','region_tb_upper','tcg_tb_insert','tcg_tb_remove','tcg_tb_lookup',
  'tcg_region_tree_reset_all','tcg_region_bounds','tcg_region_assign',
  'tcg_region_assign_next','tcg_region_alloc','tcg_region_initial_alloc','tcg_region_reset_all']]
fixture=Path(__file__).with_suffix('.c').read_text()
code=fixture.replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='region-tbs-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' or 'mingw' in cc else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-missing-field-initializers','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(repo/'libretro-common/include'),str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 if os.environ.get('REGION_TBS_BINARY'):
  import shutil
  shutil.copy2(exe,os.environ['REGION_TBS_BINARY'])
 if not os.environ.get('REGION_TBS_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env)
