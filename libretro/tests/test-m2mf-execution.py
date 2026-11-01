# SPDX-License-Identifier: GPL-2.0-or-later
"""Production M2MF coordinator with controlled DMA maps and RAM authority."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/m2mf-execution.c').read_text()
owner=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/pgraph.h').read_text()
def function(source,name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
types=h[h.index('#define PGRAPH_CPU_PAGE_BITS'):h.index('} PGRAPHCpuAccess;')+len('} PGRAPHCpuAccess;')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* CPU TYPES */',types)
code=code.replace('/* OWNER ACCESS */','\n'.join(function(owner,n) for n in ['pgraph_cpu_pages','pgraph_cpu_mask','pgraph_cpu_set_atomic','pgraph_cpu_transfer_access']))
code=code.replace('/* COORDINATOR */','\n'.join(line for line in s.splitlines() if not line.startswith('#include')))
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='m2mf-execution-',dir=r.parent) as d:
 p=Path(d);(p/'test.c').write_text(code);binary=os.environ.get('M2MF_EXECUTION_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('M2MF_EXECUTION_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-DHAVE_THREADS','-Wall','-Wextra','-Werror',*flags,'-I'+str(r),'-I'+str(r/'libretro-common/include'),str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('M2MF_EXECUTION_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
