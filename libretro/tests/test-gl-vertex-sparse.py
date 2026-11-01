# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production GL vertex uploads with an independently owned RAM/mirror."""
from pathlib import Path
import argparse,os,shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--real-gl',action='store_true');p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');a=p.parse_args();r=a.checkout.resolve()
s=(r/'hw/xbox/nv2a/pgraph/gl/vertex.c').read_text(encoding='utf-8');lo=s.index('/* The range a draw last took');hi=s.index('void pgraph_gl_attr_pointer',lo)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[lo:hi])
# Both foreign consumers must use the helper that preserves vertex staleness.
surface=(r/'hw/xbox/nv2a/pgraph/gl/surface.c').read_text(encoding='utf-8')
assert surface.count('pgraph_gl_surface_test_and_clear_dirty(')==2
assert 'DIRTY_MEMORY_NV2A)' not in surface
sources=[]
if a.real_gl:
 code='#include "ui/gl-dispatch.h"\n#include "gloffscreen.h"\nextern void libretro_gl_set_standalone_mode(void);\nstatic GLuint native_buffer;\n'+code
 code=code.replace('#define GL_ARRAY_BUFFER 1','').replace('target==1','target==GL_ARRAY_BUFFER')
 code=code.replace('static void glBindBuffer(', 'static void test_bind(').replace('static void glBufferSubData(', 'static void test_upload(')
 code=code.replace('assert(target==GL_ARRAY_BUFFER&&buffer==7);','assert(target==GL_ARRAY_BUFFER&&buffer==7);glBindBuffer(target,native_buffer);')
 code=code.replace('memcpy(mirror+a,src,n);','glBufferSubData(target,a,n,src);memcpy(mirror+a,src,n);')
 code=code.replace('static hwaddr last_addr, last_end;', '#undef glBindBuffer\n#undef glBufferSubData\n#define glBindBuffer test_bind\n#define glBufferSubData test_upload\nstatic hwaddr last_addr, last_end;')
 code=code.replace('unsigned long stale[', 'libretro_gl_set_standalone_mode();GloContext *context=glo_context_create();assert(context);glo_set_current(context);\n glGenBuffers(1,&native_buffer);test_bind(GL_ARRAY_BUFFER,7);glBufferData(GL_ARRAY_BUFFER,BYTES,mirror,GL_DYNAMIC_DRAW);\n unsigned long stale[')
 code=code.replace('free(ram);return 0;', 'uint8_t *actual=malloc(BYTES);assert(actual);glGetBufferSubData(GL_ARRAY_BUFFER,0,BYTES,actual);assert(!memcmp(actual,ram,BYTES));free(actual);glDeleteBuffers(1,&native_buffer);assert(glGetError()==GL_NO_ERROR);glo_context_destroy(context);puts("Real WGL buffer readback PASS");free(ram);return 0;')
 sources=[str(r/x) for x in ['ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c']]
with tempfile.TemporaryDirectory(prefix='gl-vertex-sparse-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 flags=shlex.split(os.environ.get('GL_VERTEX_SPARSE_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 extra=[]
 if a.real_gl:extra=['-Wno-cast-function-type','-I'+str(r),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen')]
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,*extra,str(d/'test.c'),*sources,*(['-lopengl32','-lgdi32','-luser32'] if a.real_gl else []),'-o',str(exe)],check=True,env=env)
 if os.environ.get('GL_VERTEX_SPARSE_BINARY'):shutil.copy2(exe,os.environ['GL_VERTEX_SPARSE_BINARY'])
 if not os.environ.get('GL_VERTEX_SPARSE_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env)
