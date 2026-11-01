# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GL flip continuation and FIFO backpressure with a controlled GPU."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
g=(r/'hw/xbox/nv2a/pgraph/gl/renderer.c').read_text();p=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text();f=(r/'hw/xbox/nv2a/pfifo.c').read_text()
def function(source,name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
code=Path(__file__).with_suffix('.c').read_text()
functions=[function(g,n) for n in ['pgraph_gl_discard_flip','pgraph_gl_flip_complete','pgraph_gl_flip_stall_ready','pgraph_gl_flip_stall','pgraph_gl_process_pending']]
functions += [function(p,'pgraph_process_pending_flip'),function(f,'pfifo_puller_should_stall')]
a=p.index('DEF_METHOD(NV097, FLIP_STALL)');b=p.index('{',a)+1;depth=1
while depth:depth+=(p[b]=='{')-(p[b]=='}');b+=1
functions.append(p[a:b])
a=p.index('    PGRAPHState *pg',p.index('void pgraph_process_pending(NV2AState *d)'))
b=p.index('    if (g_config.display.renderer',a)
functions.append('static void owner_dispatch(NV2AState *d) {\n'+p[a:b]+'\n}')
a=f.index('    const PGRAPHRenderer *renderer',f.index('static inline void pfifo_idle_wait'))
b=f.index('    /* The wait is prepared',a)
functions.append('static void idle_report_prefix(NV2AState *d) {\n'+f[a:b]+'\n(void)report_pending;\n}')
code=code.replace('/* PRODUCTION */','\n'.join(functions))
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='gl-flip-retirement-',dir=r.parent) as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('GL_FLIP_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('GL_FLIP_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(r/'libretro-common/include'),str(p/'test.c'),'-o',binary]+(['-static'] if win else []),check=True)
 if not os.environ.get('GL_FLIP_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
