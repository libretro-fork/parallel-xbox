# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production VP traversal with bounded queues and an ordered oracle."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
current=(r/'hw/xbox/mcpx/apu/vp/vp.c').read_text();source=Path(os.environ.get('VP_CAPACITY_SOURCE',r/'hw/xbox/mcpx/apu/vp/vp.c')).read_text()
def function(s,name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
# Keep the current serial helper available when comparing older traversals.
code=Path(__file__).with_suffix('.c').read_text().replace('/* HELPERS */',function(current,'voice_work_run_serial')+'\n'+function(source,'voice_work_enqueue')).replace('/* FRAME */',function(source,'mcpx_apu_vp_frame'))
with tempfile.TemporaryDirectory(prefix='vp-capacity-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=Path(os.environ.get('VP_CAPACITY_BINARY',str(p/'test')))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare',*shlex.split(os.environ.get('VP_CAPACITY_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error')),str(p/'test.c'),'-o',str(exe)],check=True)
 if not os.environ.get('VP_CAPACITY_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=30)
