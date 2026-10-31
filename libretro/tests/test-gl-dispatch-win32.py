# SPDX-License-Identifier: GPL-2.0-or-later
"""Build real-WGL dispatch tests; native execution needs an OpenGL 4 driver."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'x86_64-w64-mingw32-gcc'
with tempfile.TemporaryDirectory(prefix='gl-loader-') as tmp:
 out=os.environ.get('GL_DISPATCH_BINARY',str(Path(tmp)/'test.exe'))
 sources=['libretro/tests/test-gl-dispatch-win32.c','ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c']
 subprocess.run([cc,'-O2','-g','-Wall','-Wextra','-Werror','-Wno-cast-function-type','-I'+str(r),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen'),*shlex.split(os.environ.get('GL_DISPATCH_CFLAGS','')),*[str(r/s) for s in sources],'-lopengl32','-lgdi32','-luser32','-o',out],check=True)
 if not os.environ.get('GL_DISPATCH_SKIP_RUN'):
  for mode in ('frontend','native'):subprocess.run([out,mode],check=True,timeout=30)
