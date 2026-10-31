# SPDX-License-Identifier: GPL-2.0-or-later
"""Production handler reclamation during callback mutation and nested dispatch."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
h=(r/'include/block/aio.h').read_text()
helpers=h[h.index('static inline void aio_assert_handler_owner'):h.index('/**\n * aio_context_new:')]
def function(source,name):
 m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 end=source.index('{',m.start())+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]
for backend in ['win32','posix']:
 s=(r/f'util/aio-{backend}.c').read_text();t=s if backend=='win32' else (r/'util/aio-posix.h').read_text()
 handler=re.search(r'struct AioHandler \{.*?\n\};',t,re.S)[0]
 names=['aio_remove_fd_handler','aio_dispatch_handlers'] if backend=='win32' else ['aio_remove_fd_handler','aio_free_deleted_handlers','aio_dispatch_handler']
 code=Path(__file__).with_suffix('.c').read_text().replace('/* HANDLER */',handler).replace('/* OWNER HELPERS */',helpers).replace('/* PRODUCTION */','\n'.join(function(s,n) for n in names))
 with tempfile.TemporaryDirectory(prefix='aio-handler-') as tmp:
  p=Path(tmp);(p/'test.c').write_text(code)
  prefix=os.environ.get('AIO_HANDLER_BINARY_PREFIX');binary=prefix+'-'+backend+('.exe' if 'mingw' in cc else '') if prefix else str(p/'test')
  cmd=[cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-DCASE_'+backend.upper(),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include')]
  cmd+=shlex.split(os.environ.get('AIO_HANDLER_CFLAGS','-fsanitize=address,undefined'))
  subprocess.run(cmd+[str(p/'test.c'),'-o',binary],check=True)
  if not os.environ.get('AIO_HANDLER_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)