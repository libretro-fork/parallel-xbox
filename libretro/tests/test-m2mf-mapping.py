# SPDX-License-Identifier: GPL-2.0-or-later
"""Production checked DMA views and one-sample notifier timestamps."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/nv2a.c').read_text();timer=(r/'hw/xbox/nv2a/ptimer.c').read_text();h=(r/'hw/xbox/nv2a/nv2a_int.h').read_text()
def function(source,name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+';',h,re.S)[0] for name in ['DMAObject','NVDMAView'])
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types)
code=code.replace('/* DMA */','\n'.join(function(s,n) for n in ['nv_dma_load','nv_dma_map_checked','nv_dma_view_range']))
code=code.replace('/* CONSTANTS */',timer[timer.index('#define CLOCK_HIGH_MASK'):timer.index('static void ptimer_alarm_fired')])
code=code.replace('/* TIMER */','\n'.join(function(timer,n) for n in ['ptimer_get_absolute_clock','get_internal_clock','get_reg_time','ptimer_snapshot']))
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='m2mf-mapping-',dir=r.parent) as d:
 p=Path(d);(p/'test.c').write_text(code);binary=os.environ.get('M2MF_MAPPING_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('M2MF_MAPPING_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare',*flags,'-I'+str(r),str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('M2MF_MAPPING_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
