# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GL surface upload transfer code, real EGL/WGL native and scaled pixels."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();s=(r/'hw/xbox/nv2a/pgraph/gl/surface.c').read_text();parts=[]
for name in ('surface_copy_expand_row','surface_copy_expand','surface_unpack_layout','surface_grow_on_gpu'):
 m=re.search(r'^static (?:void|bool) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
a=s.index('    uint8_t *data = d->vram_ptr;',s.index('static void upload_surface_data_impl'))
# Bound the upload body itself; following diagnostic helpers may be deleted.
start=s.index('static void upload_surface_data_impl')
b=s.index('{',start)+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
parts.append('static void upload_pixels(NV2AState *d,SurfaceBinding *surface){ PGRAPHState *pg=&d->pgraph;PGRAPHGLState *r=pg->gl_renderer_state;GLint last_texture_binding;glGetIntegerv(GL_TEXTURE_BINDING_2D,&last_texture_binding);glBindFramebuffer(GL_FRAMEBUFFER,pg->gl_renderer_state->gl_fb_util);\n'+s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='gl-pitched-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('PITCHED_GL_BINARY',str(p/'test'))
 cc=sys.argv[2] if len(sys.argv)>2 else 'gcc';windows='mingw' in cc or os.name=='nt'
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-cast-function-type','-I'+str(r),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen'),'-I'+str(r/'hw/xbox/nv2a/pgraph'),*shlex.split(os.environ.get('PITCHED_GL_CFLAGS','-fsanitize=address,undefined' if not windows else '')),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/swizzle.c')]
 if windows:args += [str(r/n) for n in ('ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c')]+['-lopengl32','-lgdi32','-luser32']
 else:args+=['-lEGL','-lGL']
 subprocess.run(args+['-o',binary],check=True)
 if not os.environ.get('PITCHED_GL_SKIP_RUN'):subprocess.run([binary],env=dict(os.environ,EGL_PLATFORM='surfaceless',ASAN_OPTIONS='detect_leaks=0'),check=True,timeout=60)
