# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate production linear staging spans and direct VRAM copies.
Usage: python3 SCRIPT CHECKOUT [CC]. Requires Vulkan; validation is on unless explicitly disabled.
"""
from pathlib import Path
import re
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
parts = [re.search(r"typedef struct TextureLevel \{.*?\} TextureLevel;", source, re.S)[0]]
for name in ("stage_level", "level_from_vram", "stage_linear_2d", "texture_level_copy"):
    position = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    parts.append(source[start:end])
command = (repo / 'hw/xbox/nv2a/pgraph/vk/command.c').read_text()
a = command.index('void pgraph_vk_upload_source_barrier(')
b = command.index('\n}\n', a) + 2
parts.append(command[a:b])
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="vk-linear-span-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    link = [str(Path(os.environ.get("WINDIR", "C:/Windows")) / "System32/vulkan-1.dll")] if os.name == "nt" else ["-lvulkan"]
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "gcc", "-O2", "-std=gnu11",
                    "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
                    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error", "-I", str(repo),
                    "-I", str(repo / "deps/vulkan-headers"),
                    str(folder / "test.c"), *link, "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=120)
