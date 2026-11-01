# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production bounded scanout-mode eviction and lifetime rules."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(root/'hw/xbox/nv2a/pgraph/vk/surface.c').read_text()
def function(name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in ['scanout_mode_bytes','plan_scanout_mode','release_scanout_mode','reserve_scanout_mode']))
compose=function('pgraph_vk_compose_scanout');a=compose.index('            uint64_t required');b=compose.index('            struct PGRAPHVkScanoutMode *p',a);pressure=compose[a:b]
assert 'pgraph_vk_finish' not in pressure and 'single_time_commands' not in pressure
reserve=function('reserve_scanout_mode')
assert reserve.count('pgraph_vk_poll_aux_retired(r)')==1 and reserve.count('pgraph_vk_poll_display_retired(r)')==1
assert 'reserve_scanout_mode(r, required, &next)' in pressure
assert 'r->display.retry_pending = true' in pressure and 'return NULL' in pressure
assert 'm->aux_after = r->aux_submitted' in compose
present=(root/'hw/xbox/nv2a/pgraph/vk/display.c').read_text()
a=present.index('submit_frame(r, sync);');b=present.index('retro_atomic_store_release_64',a)
assert 'if (direct) mode->display_after = sync->serial' in present[a:b]
assert present.count('if (direct) mode->aux_after = r->aux_submitted;')==2
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='vk-scanout-mode-retirement-',dir=root.parent) as tmp:
 t=Path(tmp);(t/'test.c').write_text(code);binary=os.environ.get('VK_MODE_BINARY',str(t/'test'))
 flags=shlex.split(os.environ.get('VK_MODE_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(t/'test.c'),'-o',binary]+(['-static'] if win else []),check=True)
 if not os.environ.get('VK_MODE_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
