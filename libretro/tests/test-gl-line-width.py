# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay production dynamic GL width updates and invalidation against a driver trace."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile
repo=Path(sys.argv[1]).resolve()
source=Path(os.environ.get('GL_LINE_WIDTH_SOURCE',repo/'hw/xbox/nv2a/pgraph/gl/draw.c')).read_text()
invalidate=re.search(r'void pgraph_gl_draw_state_invalidate\([^)]*\)\n\{.*?\n\}',source,re.S)[0]
start=source.index('    GLfloat line_width = (pg->line_width / 8.0f)',source.index('gl_state_done:;'))
end=source.index('    if (r->shader_binding',start)
update='static void update(PGRAPHState *pg,PGRAPHGLState *r) {\n'+source[start:end]+'}\n'
fixture=(repo/'libretro/tests/test-gl-line-width.c').read_text().replace('/* PRODUCTION */',invalidate+'\n'+update)
with tempfile.TemporaryDirectory(prefix='gl-width-') as tmp:
    c=Path(tmp)/'test.c';c.write_text(fixture)
    exe=Path(os.environ.get('GL_LINE_WIDTH_BINARY',Path(tmp)/'test'))
    subprocess.run([sys.argv[2] if len(sys.argv)>2 and sys.argv[2]!='--gl' else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('GL_LINE_WIDTH_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error')),str(c),'-o',str(exe)],check=True)
    if not os.environ.get('GL_LINE_WIDTH_SKIP_RUN'):subprocess.run([str(exe)],check=True)
    if '--gl' in sys.argv:
        subprocess.run(['gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-DREAL_GL',str(c),'-lEGL','-lGL','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,env=dict(os.environ,EGL_PLATFORM='surfaceless'))
