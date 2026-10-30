# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate production bordered-cube copy regions with Vulkan GPU transfers.

Usage: python3 SCRIPT CHECKOUT [CC]. Requires Linux Vulkan, BC format support
and Khronos validation layers. Checks copy addressing, not Xbox border layout.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
parts = [re.search(r"typedef struct TextureLevel \{.*?\} TextureLevel;", source, re.S)[0]]
for name in ("crop_cubemap_level", "texture_level_copy"):
    position = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    parts.append(source[start:end])
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="vk-cube-crop-") as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "gcc", "-O2", "-std=gnu11",
                    "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(repo),
                    str(folder / "test.c"), "-lvulkan", "-o", str(folder / "test")], check=True)
    subprocess.run([str(folder / "test")], check=True)
