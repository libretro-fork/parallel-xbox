# SPDX-License-Identifier: GPL-2.0-or-later
"""Real GL pixel/lifetime oracle for production texture generation and recycling."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve()
s=Path(os.environ.get('GL_TEXTURE_RECYCLE_SOURCE',repo/'hw/xbox/nv2a/pgraph/gl/texture.c')).read_text()
functions=[]
for name in ('generate_texture','texture_binding_destroy'):
 m=re.search(r'^static (?:TextureBinding\*|void) '+name+r'\([^;{]*\)\s*\{',s,re.M)
 assert m
 end=s.index('{',m.start())+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 functions.append(s[m.start():end])
surface=(repo/'hw/xbox/nv2a/pgraph/gl/surface.c').read_text()
start=surface.index('    unsigned int width = texture_shape->width, height = texture_shape->height;',surface.index('void render_surface_to(NV2AState'))
end=surface.index('    glBindTexture(texture->gl_target, 0);',start)
allocation=surface[start:end].replace('glTexImage2D(', 'observed_snapshot_alloc(')
functions.append('static void snapshot_allocate(TextureBinding *texture,TextureShape *texture_shape) { PGRAPHState *pg=&g_nv2a->pgraph; int texture_unit=0; const ColorFormatInfo *f=&kelvin_color_format_gl_map[texture_shape->color_format];\n'+allocation+'\n}')
start=s.index('            sampled_texture_key_store(binding->recycle_key,',s.index('/* Retain only exact single-level RGBA8'))
end=s.index(';',start)+1
functions.append('static void update_snapshot_key(TextureBinding *binding,TextureShape state,bool surf_to_tex_bytes) {\n'+s[start:end]+'\n}')
warm=(repo/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
start=warm.index('    while (r->fresh_tex_r != r->fresh_tex_w)',warm.index('static void touch_fresh_textures'))
end=warm.index('{',start)+1;depth=1
while depth:
 depth+=(warm[end]=='{')-(warm[end]=='}');end+=1
functions.append('static void touch_tickets(PGRAPHGLState *r,unsigned max_items) { unsigned items=0;\n'+warm[start:end].replace('glDrawArrays(', 'test_touch(')+'\n}')
header=(repo/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text()
binding=re.search(r'typedef struct TextureBinding \{.*?\} TextureBinding;',header,re.S).group()
code=Path(__file__).with_suffix('.c').read_text().replace('/* BINDING */',binding).replace('/* PRODUCTION */','\n'.join(functions))
if os.name=='nt':
 code=code.replace('#define GL_GLEXT_PROTOTYPES\n#include <EGL/egl.h>\n#include <GL/gl.h>','#include <windows.h>\n#include <epoxy/gl.h>')
 context=(repo/'libretro/tests/test-point-fog-gl.c').read_text()
 context=context[context.index('#ifdef _WIN32',context.index('int main(')):context.index('    printf("Point/fog GL GPU:')]
 start=code.index(' EGLDisplay display=',code.index('int main('));end=code.index(' printf("GL recycle oracle:',start)
 code=code[:start]+context+code[end:]
 code=code.replace('eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,ctx);eglTerminate(display);','wglMakeCurrent(NULL,NULL);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);')
 epoxy=Path(os.environ['GL_TEXTURE_RECYCLE_EPOXY_ROOT']).resolve()
 libraries=['-I'+str(epoxy/'libepoxy/include'),str(epoxy/'libepoxy.a'),'-lopengl32','-lgdi32','-static']
 no_core_args={}
else:
 import resource
 def no_core(): resource.setrlimit(resource.RLIMIT_CORE,(0,0))
 no_core_args={'preexec_fn':no_core}
 libraries=['-lEGL','-lGL']
with tempfile.TemporaryDirectory(prefix='gl-texture-recycle-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run([os.environ.get('CC','cc'),'-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*shlex.split(os.environ.get('GL_TEXTURE_RECYCLE_CFLAGS','')), '-I'+str(repo),str(p/'test.c'),'-o',str(p/'test'),*libraries],check=True)
 subprocess.run([str(p/'test')],env=dict(os.environ,EGL_PLATFORM='surfaceless'),check=True)
 # Independently prove that dropping the no-upload branch fails the oracle.
 mutant=code.replace('if (surface_snapshot) {','if (false) {',1)
 assert mutant!=code
 (p/'negative.c').write_text(mutant)
 subprocess.run([os.environ.get('CC','cc'),'-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*shlex.split(os.environ.get('GL_TEXTURE_RECYCLE_CFLAGS','')), '-I'+str(repo),str(p/'negative.c'),'-o',str(p/'negative'),*libraries],check=True)
 failed=subprocess.run([str(p/'negative')],env=dict(os.environ,EGL_PLATFORM='surfaceless'),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,**no_core_args)
 assert failed.returncode!=0, 'redundant snapshot upload escaped the oracle'
 print('PASS: 2D/cube snapshots defer storage and guest uploads; upload-branch negative rejected')
 header=(repo/'hw/xbox/nv2a/pgraph/gl/texture-recycle.h').read_text()
 bad_header=header.replace('pool->entries[i].serial <= pool->completed','true')
 assert bad_header!=header
 (p/'early.h').write_text(bad_header)
 early=code.replace('"hw/xbox/nv2a/pgraph/gl/texture-recycle.h"','"early.h"')
 (p/'negative.c').write_text(early)
 subprocess.run([os.environ.get('CC','cc'),'-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*shlex.split(os.environ.get('GL_TEXTURE_RECYCLE_CFLAGS','')), '-I'+str(repo),str(p/'negative.c'),'-o',str(p/'negative'),*libraries],check=True)
 failed=subprocess.run([str(p/'negative')],env=dict(os.environ,EGL_PLATFORM='surfaceless'),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,**no_core_args)
 assert failed.returncode!=0,'early reuse escaped the lifetime oracle'
 print('PASS: 64 GPU snapshot lifetimes, scaled allocation reuse, RAM exchange; premature-reuse negative rejected')
