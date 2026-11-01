# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GL report lifetime and real object pool with deterministic query results."""
from pathlib import Path
import argparse,os,shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');a=p.parse_args();r=a.checkout.resolve()
h=(r/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text(encoding='utf-8');lo=h.index('typedef struct QueryReport {');hi=h.index('} QueryReport;',lo)+len('} QueryReport;')
s=(r/'hw/xbox/nv2a/pgraph/gl/reports.c').read_text(encoding='utf-8');s=s[s.index('/* The active interval'):]
pool=(r/'hw/xbox/nv2a/pgraph/pool.h').read_text(encoding='utf-8');pool=pool[pool.index('typedef struct ObjPool {'):pool.index('typedef struct Scratch {')]
draw=(r/'hw/xbox/nv2a/pgraph/gl/draw.c').read_text(encoding='utf-8');assert 'pgraph_gl_alloc_zpass_query(d)' in draw;assert 'gl_zpass_pixel_count_queries' not in draw
code=Path(__file__).with_suffix('.c').read_text().replace('/* REPORT TYPE */',h[lo:hi]).replace('/* POOL */',pool).replace('/* PRODUCTION */',s)
with tempfile.TemporaryDirectory(prefix='gl-report-storage-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' or 'mingw' in a.cc else 'test')
 flags=shlex.split(os.environ.get('GL_REPORT_STORAGE_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-misleading-indentation','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),*flags,str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 if os.environ.get('GL_REPORT_STORAGE_BINARY'):shutil.copy2(exe,os.environ['GL_REPORT_STORAGE_BINARY'])
 if not os.environ.get('GL_REPORT_STORAGE_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env)
