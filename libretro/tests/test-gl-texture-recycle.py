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
warm=(repo/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
start=warm.index('    while (r->fresh_tex_r != r->fresh_tex_w)',warm.index('static void touch_fresh_textures'))
end=warm.index('{',start)+1;depth=1
while depth:
 depth+=(warm[end]=='{')-(warm[end]=='}');end+=1
functions.append('static void touch_tickets(PGRAPHGLState *r,unsigned max_items) { unsigned items=0;\n'+warm[start:end].replace('glDrawArrays(', 'test_touch(')+'\n}')
header=(repo/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text()
binding=re.search(r'typedef struct TextureBinding \{.*?\} TextureBinding;',header,re.S).group()
code=Path(__file__).with_suffix('.c').read_text().replace('/* BINDING */',binding).replace('/* PRODUCTION */','\n'.join(functions))
with tempfile.TemporaryDirectory(prefix='gl-texture-recycle-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run([os.environ.get('CC','cc'),'-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*shlex.split(os.environ.get('GL_TEXTURE_RECYCLE_CFLAGS','')), '-I'+str(repo),str(p/'test.c'),'-o',str(p/'test'),'-lEGL','-lGL'],check=True)
 subprocess.run([str(p/'test')],env=dict(os.environ,EGL_PLATFORM='surfaceless'),check=True)
