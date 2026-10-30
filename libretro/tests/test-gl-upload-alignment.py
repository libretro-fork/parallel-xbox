# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production GL upload row/slice addressing on independent linear data.

Usage: python SCRIPT CHECKOUT [CC]. Windows needs the built libepoxy.a;
Linux needs EGL/OpenGL development libraries and a surfaceless driver.
Unswizzle/conversion outputs are supplied by the fixture; their arithmetic
and guest memory layout are outside this host pixel-transfer test.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/gl/texture.c").read_text()
functions = []
for name in ("tex_scratch", "gl_internal_format_to_s3tc_enum", "upload_gl_texture"):
    # upload_gl_texture has no forward declaration; extract its definition.
    position = source.index(name + "(")
    position = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[position:end])
decoder = (repo / "hw/xbox/nv2a/pgraph/s3tc.c").read_text().replace(
    '#include "qemu/osdep.h"',
    '#include <stdbool.h>\n#include <stdlib.h>\n#include <assert.h>\n'
    '#define MIN(a,b) ((a)<(b)?(a):(b))\n#define g_malloc malloc')
code = Path(__file__).with_suffix(".c").read_text().replace(
    "/* PRODUCTION */", "\n".join(functions))
with tempfile.TemporaryDirectory(prefix="gl-upload-") as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    (folder / "decoder.c").write_text(decoder)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    args = [sys.argv[2] if len(sys.argv) > 2 else "gcc", "-O2", "-Wall",
            "-Wextra", "-Werror", "-Wno-sign-compare", "-Wno-return-type",
            "-I", str(repo / "hw/xbox/nv2a/pgraph"), "-I", str(repo / "libretro/tests"),
            str(folder / "test.c"), str(folder / "decoder.c"), "-o", str(exe), "-lm"]
    if os.name == "nt":
        args += ["-I", str(repo / "deps/libepoxy/include"),
                 str(repo / "deps/libepoxy.a"), "-lopengl32", "-lgdi32", "-static"]
    else:
        args += ["-lEGL", "-lGL"]
    subprocess.run(args, check=True)
    subprocess.run([str(exe)], env=dict(os.environ, EGL_PLATFORM="surfaceless"),
                   check=True)
