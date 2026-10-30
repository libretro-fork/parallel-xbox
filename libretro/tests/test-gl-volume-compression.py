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
             "upload_compressed_volume"):
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
# Only the QEMU allocation/include adapter differs from the production decoder.
decoder = (repo / "hw/xbox/nv2a/pgraph/s3tc.c").read_text().replace(
    '#include "qemu/osdep.h"',
    '#include <stdbool.h>\n#include <stdlib.h>\n#include <assert.h>\n'
    '#define MIN(a,b) ((a)<(b)?(a):(b))\n#define g_malloc malloc')
with tempfile.TemporaryDirectory(prefix="gl-volume-") as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    (folder / "decoder.c").write_text(decoder)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    # The extracted mapper ends in assert; MinGW does not mark its assert
    # handler noreturn. The decoder also has existing signed loop indices.
    args = [sys.argv[2] if len(sys.argv) > 2 else "gcc", "-O2", "-Wall",
            "-Wextra", "-Werror", "-Wno-sign-compare", "-Wno-return-type", "-I",
            str(repo / "hw/xbox/nv2a/pgraph"), str(folder / "test.c"),
            str(folder / "decoder.c"), "-o", str(exe)]
    if os.name == "nt":
        args += ["-I", str(repo / "deps/libepoxy/include"),
                 str(repo / "deps/libepoxy.a"), "-lopengl32", "-lgdi32", "-static"]
    else:
        args += ["-lEGL", "-lGL"]
    subprocess.run(args, check=True)
    subprocess.run([str(exe)], env=dict(os.environ, EGL_PLATFORM="surfaceless"),
                   check=True)
