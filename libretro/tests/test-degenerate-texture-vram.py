# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify direct VRAM selection against the production Morton-order oracle."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
parts = [re.search(r"typedef struct TextureLevel \{.*?\} TextureLevel;", source, re.S)[0]]
for name in ("stage_level", "level_from_vram", "stage_swizzled"):
    position = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    parts.append(source[start:end])
code = Path(__file__).with_suffix(".c").read_text().replace(
    "/* PRODUCTION */", "\n".join(parts))
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
with tempfile.TemporaryDirectory(prefix="degenerate-texture-vram-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-sign-compare", "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo), "-I", str(repo / "deps/vulkan-headers"),
                    str(folder / "test.c"),
                    str(repo / "hw/xbox/nv2a/pgraph/swizzle.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
