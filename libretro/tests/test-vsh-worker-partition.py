#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual vertex case guard and prove disjoint complete coverage."""
import ast
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
compiler = sys.argv[2] if len(sys.argv) > 2 else 'cc'
source = (root/'libretro/tests/test-spv-vsh-gpu.c').read_text()
start = source.index('  if(argc==5){')
end = source.index('\n  }', start) + len('\n  }')
guard = source[start:end]
tree = ast.parse((root/'libretro/tests/test-spv-vsh-gpu.py').read_text())
batches = next(ast.literal_eval(n.value) for n in ast.walk(tree)
               if isinstance(n, ast.Assign) and isinstance(n.value, ast.List)
               and any(isinstance(t, ast.Name) and t.id == 'batches' for t in n.targets))
assert [case for first, limit in batches for case in range(first, limit)] == list(range(42512))
code = '''#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned ranges[][2]={RANGES};
int main(void){
 unsigned char seen[42512];int argc=5;
 for(unsigned b=0;b<sizeof(ranges)/sizeof(ranges[0]);b++){
  unsigned first=ranges[b][0],limit=ranges[b][1];
  for(unsigned n=1;n<=4;n++){
   memset(seen,0,sizeof(seen));unsigned counts[4]={0};
   for(unsigned worker=0;worker<n;worker++){
    char slot[8],slots[8];snprintf(slot,sizeof(slot),"%u",worker);snprintf(slots,sizeof(slots),"%u",n);
    char *argv[]={"fixture","unused","unused",slot,slots};
    for(unsigned test=first;test<limit;test++){
GUARD
     assert(!seen[test]);seen[test]++;counts[worker]++;
     unsigned block=(test-first)/16;assert((block+block/n)%n==worker);
    }
   }
   for(unsigned test=first;test<limit;test++)assert(seen[test]==1);
   for(unsigned worker=0;worker<n;worker++)assert(counts[worker]+16>=(limit-first)/n && counts[worker]<=(limit-first+n-1)/n+16);
  }
 }
 puts("Actual VSH worker guard: 42512 cases, all original batches, slots1..4, disjoint complete balanced coverage PASS");
}
'''.replace('RANGES', ','.join('{%u,%u}' % row for row in batches))
with tempfile.TemporaryDirectory(prefix='vsh-partition-') as temporary:
    directory = Path(temporary)
    for name, body in [('positive', guard), ('lost slot', guard.replace('%slots', '%(slots+1)'))]:
        assert name == 'positive' or body != guard
        path = directory/'test.c'
        path.write_text(code.replace('GUARD', body))
        executable = directory/('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([compiler, '-O2', str(path), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if name == 'positive':
            assert result.returncode == 0, result.stderr
            print(result.stdout, end='')
        else:
            assert result.returncode != 0 and 'Assertion' in result.stderr
            print('Required VSH lost-slot control rejected')
