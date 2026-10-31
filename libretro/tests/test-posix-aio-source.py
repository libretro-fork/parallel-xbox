# SPDX-License-Identifier: GPL-2.0-or-later
"""Production direct POSIX AIO readiness with real GLib, pipes and nested dispatch."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
macros=subprocess.check_output([cc,'-dM','-E','-'],input='',text=True)
if '#define _WIN32 ' in macros:
 print('POSIX GSource integration requires a POSIX host; skipped Windows compiler');sys.exit(0)
s=(r/'util/aio-posix.c').read_text();h=(r/'include/block/aio.h').read_text();ih=(r/'util/aio-posix.h').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:void |bool |AioHandler \*)'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
names=['aio_add_ready_handler','aio_update_source_poll','find_aio_handler','aio_remove_fd_handler','aio_set_fd_handler','aio_prepare','aio_pending','aio_free_deleted_handlers','aio_dispatch_handler','aio_collect_ready_handlers','aio_dispatch_ready_handlers','aio_dispatch','aio_context_setup','aio_context_destroy','aio_has_fd_handlers']
handler=re.search(r'struct AioHandler \{.*?\n\};',ih,re.S)[0]
helpers=h[h.index('static inline void aio_assert_handler_owner'):h.index('/**\n * aio_context_new:')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* HANDLER */',handler).replace('/* OWNER HELPERS */',helpers).replace('/* PRODUCTION */','\n'.join(function(n) for n in names))
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','glib-2.0'],text=True))
with tempfile.TemporaryDirectory(prefix='posix-aio-source-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('POSIX_AIO_BINARY',str(p/'test'))
 cmd=[cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include')]
 cmd+=shlex.split(os.environ.get('POSIX_AIO_CFLAGS','-fsanitize=address,undefined'))
 subprocess.run(cmd+[str(p/'test.c'),'-pthread',*flags,'-o',binary],check=True)
 if not os.environ.get('POSIX_AIO_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)