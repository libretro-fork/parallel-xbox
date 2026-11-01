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
             "decode_compressed_volume_gpu", "upload_compressed_volume"):
    position = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[start:end])
code = Path(__file__).with_suffix(".c").read_text().replace(
    "/* PRODUCTION */", "\n".join(functions))
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
                 str(repo / "deps/libepoxy.a"), "-lopengl32", "-lgdi32", "-static"]
    else:
        args += ["-lEGL", "-lGL"]
    compiler_env = os.environ.copy()
    compiler_env["PATH"] = str(Path(args[0]).resolve().parent) + os.pathsep + compiler_env.get("PATH", "")
    subprocess.run(args, check=True, env=compiler_env)
    if os.environ.get("GL_VOLUME_BINARY"):
        import shutil
        shutil.copy2(exe, os.environ["GL_VOLUME_BINARY"])
    if not os.environ.get("GL_VOLUME_SKIP_RUN"):
        subprocess.run([str(exe)], env=dict(os.environ, EGL_PLATFORM="surfaceless"),
                       check=True)
