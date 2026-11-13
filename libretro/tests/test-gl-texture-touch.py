#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the production texture-touch utility using a real OpenGL context."""
from pathlib import Path
import os, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
source = (r / 'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

init = function('static void init_texture_touch(')
touch = function('static void touch_fresh_textures(')
setup = function('void pgraph_gl_init_shaders(')
shutdown = function('void pgraph_gl_finalize_shaders(')
assert shutdown.count('finalize_texture_touch(r);') == 1
assert setup.count('init_texture_touch(r);') == 1
for forbidden in ('pgraph_gl_compile_shader(', 'glCompileShader(',
                  'glLinkProgram(', 'glGenVertexArrays('):
    assert forbidden not in touch, forbidden
production = '\n'.join([function('GLuint pgraph_gl_compile_shader('), init, function('static void finalize_texture_touch('), touch])
template = Path(__file__).with_suffix('.c').read_text()
env = dict(os.environ, EGL_PLATFORM='surfaceless', LIBGL_ALWAYS_SOFTWARE='1')
env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-texture-touch-') as tmp:
    d = Path(tmp)
    c, exe = d / 'test.c', d / 'test'
    def build(text):
        c.write_text(template.replace('/* PRODUCTION */', text))
        flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
               '-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
        if os.name=='nt':
            flags+=['-Wno-cast-function-type','-Wno-unused-parameter','-I'+str(r),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen')]
            extra=[str(r/x) for x in ('ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c')]+['-lopengl32','-lgdi32','-luser32']
        else:extra=['-lEGL','-lGL']
        subprocess.run(flags+[str(c)]+extra+['-o',str(exe)],check=True,env=env)
    build(production)
    subprocess.run([str(exe)], env=env, check=True, timeout=30)
    for label, before, after in [
        ('wrong sampler', 'glUniform1i(glGetUniformLocation(r->touch_prog, "s"), 0);',
         'glUniform1i(glGetUniformLocation(r->touch_prog, "s"), 1);'),
        ('VAO deferred to flip', '    glBindVertexArray(r->touch_vao);',
         '    /* no startup VAO instantiation */'),
        ('unmasked utility writes', '    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);',
         '    /* missing write mask */'),
    ]:
        assert before in production, label
        build(production.replace(before, after, 1))
        result = subprocess.run([str(exe)], env=env, capture_output=True, timeout=30)
        assert result.returncode, label
        print('Required texture-touch control rejected:', label)
