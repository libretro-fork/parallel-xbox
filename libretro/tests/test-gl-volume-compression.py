# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the production volume uploader against real GL, natively or via EGL.

Usage: python SCRIPT CHECKOUT [CC]. Windows requires the built libepoxy.a;
Linux requires EGL/OpenGL development libraries and a surfaceless driver.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/gl/texture.c").read_text()
functions = []
for name in ("gl_internal_format_to_s3tc_enum", "tex_scratch",
             "init_compressed_volume_decoder", "decode_compressed_volume_gpu", "upload_compressed_volume"):
    position = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[start:end])
production="\n".join(functions)
# Fixed utility creation belongs to renderer initialization, not upload.
init = source[source.index('void pgraph_gl_init_textures('):source.index('void pgraph_gl_finalize_textures(')]
hot = functions[-2]
def audit(init,hot):
    assert 'init_compressed_volume_decoder(r);' in init
    assert not any(x in hot for x in ('glCompileShader(', 'glLinkProgram(', 'glCreateShader(', 'glCreateProgram(', 'init_compressed_volume_decoder('))
audit(init,hot)
for name,bad_init,bad_hot in (
    ('missing startup preparation',init.replace('init_compressed_volume_decoder(r);',''),hot),
    ('draw retries startup helper',init,hot+'\ninit_compressed_volume_decoder(r);')):
    try:audit(bad_init,bad_hot)
    except AssertionError:print('Required negative control rejected:',name)
    else:raise AssertionError(name)
for api in ('glCompileShader','glLinkProgram','glCreateShader','glCreateProgram','glGetProgramiv'):
    production=production.replace(api+'(', 'tracked_'+api+'(')
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */",production)
code = code.replace('glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);', 'glGetShaderiv(shader, GL_COMPILE_STATUS, &ok); if (!ok) { char log[4096]; glGetShaderInfoLog(shader, sizeof(log), NULL, log); fprintf(stderr, "%s", log); }')
code = code.replace('const char *source = s3tc_volume_decode_glsl;', 'const char *source = getenv("GL_VOLUME_BAD_SHADER") ? "invalid shader" : s3tc_volume_decode_glsl;')
# Compile the standalone production decoder without include/allocation adapters.
decoder = (repo / "hw/xbox/nv2a/pgraph/s3tc.c").read_text()
with tempfile.TemporaryDirectory(prefix="gl-volume-") as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    (folder / "decoder.c").write_text(decoder)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    # The extracted mapper ends in assert; MinGW does not mark its assert
    # handler noreturn. The decoder also has existing signed loop indices.
    args = [sys.argv[2] if len(sys.argv) > 2 else "gcc", "-O2", "-Wall",
            "-Wextra", "-Werror", "-Wno-sign-compare", "-Wno-return-type", "-I",
            str(repo / "hw/xbox/nv2a/pgraph"), "-I", str(repo / "hw/xbox/nv2a/pgraph/gl"), "-I", str(repo / "libretro/tests"), str(folder / "test.c"),
            str(folder / "decoder.c"), "-o", str(exe), "-lm"]
    if os.name == "nt":
        args += ["-I", str(repo / "deps/libepoxy/include"),
                 os.environ.get("GL_VOLUME_EPOXY_LIB", str(repo / "deps/libepoxy.a")), "-lopengl32", "-lgdi32", "-static"]
    else:
        args += ["-lEGL", "-lGL"]
    compiler_env = os.environ.copy()
    compiler_env["PATH"] = str(Path(args[0]).resolve().parent) + os.pathsep + compiler_env.get("PATH", "")
    subprocess.run(args, check=True, env=compiler_env)
    if os.environ.get("GL_VOLUME_BINARY"):
        import shutil
        shutil.copy2(exe, os.environ["GL_VOLUME_BINARY"])
    if not os.environ.get("GL_VOLUME_SKIP_RUN"):
        for fault in (None, 'GL_VOLUME_BAD_SHADER', 'GL_VOLUME_FAIL_LINK'):
            env=dict(os.environ, EGL_PLATFORM="surfaceless")
            env.pop('GL_VOLUME_BAD_SHADER',None);env.pop('GL_VOLUME_FAIL_LINK',None)
            if fault:env[fault]='1'
            subprocess.run([str(exe)],env=env,check=True)

        if os.name=='nt':
            import ctypes
            ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
        env=dict(os.environ,EGL_PLATFORM="surfaceless")
        env.pop('GL_VOLUME_BAD_SHADER',None);env.pop('GL_VOLUME_FAIL_LINK',None)
        marker='    GLint program, generic, indexed, texture, unpack, base_level, max_level;'
        for name,call in (('draw creates shader','(void)tracked_glCreateShader(GL_COMPUTE_SHADER);'),('draw links program','tracked_glLinkProgram(r->s3tc_decode.program);')):
            assert marker in code
            mutant=code.replace(marker,'    '+call+'\n'+marker,1)
            (folder/'test.c').write_text(mutant)
            subprocess.run(args,check=True,env=compiler_env)
            bad=subprocess.run([str(exe)],env=env,capture_output=True)
            assert bad.returncode!=0 and b'!draw_phase' in bad.stderr,name
            print('Required negative control rejected:',name)
