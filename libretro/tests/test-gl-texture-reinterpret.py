# SPDX-License-Identifier: GPL-2.0-or-later
"""GPU byte oracle using the production GL transfer and shrink functions.

Requires Linux EGL/OpenGL development packages and a surfaceless EGL driver.
Usage: python3 SCRIPT CHECKOUT
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/gl/surface.c").read_text()
functions = []
for name in ("surface_shrink_on_gpu", "pgraph_gl_reinterpret_surface_to_texture"):
    match = re.search(r"^(?:static )?(?:bool|void) " + name + r"\(", source, re.M)
    assert match
    start = source.index("{", match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[match.start():end])
code = Path(__file__).with_suffix(".c").read_text().replace(
    "/* PRODUCTION */", "\n".join(functions))
with tempfile.TemporaryDirectory(prefix="gl-alias-") as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    subprocess.run([os.environ.get("CC", "cc"), "-O2", "-Wall", "-Wextra",
                    "-Werror", str(folder / "test.c"), "-o", str(folder / "test"),
                    "-lEGL", "-lGL"], check=True)
    env = dict(os.environ, EGL_PLATFORM="surfaceless")
    subprocess.run([str(folder / "test")], env=env, check=True)
