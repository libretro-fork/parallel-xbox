# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the production dirty-page fast path with bounded owner probes."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(repo/'system/physmem.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:bool|uint8_t) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
functions='\n'.join(function(n) for n in ('physical_memory_get_dirty','physical_memory_get_dirty_flag','physical_memory_is_clean'))
ram=(repo/'include/exec/ramlist.h').read_text();defines='\n'.join(x for x in ram.splitlines() if x.startswith('#define DIRTY_MEMORY_'))
code=Path(__file__).with_suffix('.c').read_text().replace('/* CLIENTS */',defines).replace('/* FUNCTIONS */',functions)
with tempfile.TemporaryDirectory(prefix='dirty-page-fast-') as d:
 p=Path(d);source=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(source),'-o',str(exe)]
 source.write_text(code);subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
 mutants=[('missing bitmap RCU owner',code.replace('    RCU_READ_LOCK_GUARD();','',1)),('wrong PVIDEO consumer',code.replace('DIRTY_MEMORY_NV2A_TEX, DIRTY_MEMORY_NV2A_PVIDEO,','DIRTY_MEMORY_NV2A_TEX, DIRTY_MEMORY_NV2A_TEX,',1)),('wrong bitmap block',code.replace('unsigned long idx = page / DIRTY_MEMORY_BLOCK_SIZE;','unsigned long idx = 0;',1)),('inverted dirty predicate',code.replace('if (!test_bit(offset, blocks->blocks[idx]))','if (test_bit(offset, blocks->blocks[idx]))',1))]
 for name,mutant in mutants:
  assert mutant!=code,name;source.write_text(mutant);subprocess.run(command,check=True)
  bad=subprocess.run([str(exe)],capture_output=True);assert bad.returncode!=0 and b'FAIL dirty-page' in bad.stderr,name
  print('Required negative control rejected:',name)
