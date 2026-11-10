# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production TLB dirty-range reprotection against an exhaustive scan.
Usage: python test-tlb-dirty.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'accel/tcg/cputlb.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:inline )?(?:void|bool|uint64_t) '+name+r'\(',s,re.M);assert m
 at=s.index('{',m.start());end=at+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
assert 'memset(desc->dirty_write_tags, 0' in function('tlb_mmu_flush_locked')
assert 'tlb_dirty_index_mark(desc, cpu_tlb_fast(cpu, mmu_idx), index)' in function('tlb_set_page_full')
fixture=Path(__file__).with_suffix('.c').read_text()
code=fixture.replace('/* PRODUCTION */','\n'.join(function(name) for name in ['tlb_dirty_host_tag','tlb_dirty_range_tags','tlb_dirty_index_mark','tlb_reset_dirty_range_locked','tlb_reset_dirty','tlb_set_dirty1_locked','tlb_set_dirty','copy_tlb_helper_locked','victim_tlb_hit']))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='tlb-dirty-',dir=repo.parent) as directory:
 p=Path(directory);exe=Path(os.environ.get('TLB_DIRTY_BINARY',str(p/('test.exe' if os.name=='nt' else 'test'))))
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror']+shlex.split(os.environ.get('TLB_DIRTY_CFLAGS','-fsanitize=address,undefined'))+['-I'+str(repo/'include'),str(p/'test.c'),'-o',str(exe)]
 def build(text):
  (p/'test.c').write_text(text)
  subprocess.run(args,check=True,env=env)
 build(code)
 if not os.environ.get('TLB_DIRTY_SKIP_RUN'):
  runner=shlex.split(os.environ.get('TLB_DIRTY_RUNNER',''))
  subprocess.run(runner+[str(exe)],check=True,env=env)
  if os.name!='nt':
   import resource
   resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  for name,mutant in [
    ('different physical page promoted',code.replace('full->xlat_section == xlat_section','(full->xlat_section == xlat_section || full != NULL)')),
    ('victim uses primary mapping',code.replace('&desc->vfulltlb[k],','&desc->fulltlb[k],'))]:
   assert mutant!=code,name
   build(mutant)
   result=subprocess.run(runner+[str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
   assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
   print('Required negative control rejected:',name,flush=True)
