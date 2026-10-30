# SPDX-License-Identifier: GPL-2.0-or-later
"""Production RGB655 staging oracle. Linux Vulkan + validation layers required.

Usage: python3 SCRIPT CHECKOUT [CC]. Unswizzle output is supplied linearly;
the real shared conversion, Vulkan staging and alpha expansion are executed.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
vk = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
common = (repo / "hw/xbox/nv2a/pgraph/texture.c").read_text()
constants = (repo / "hw/xbox/nv2a/pgraph/vk/constants.h").read_text()
fmt = re.search(r"\[NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5\]\s*=\s*\{\s*(VK_FORMAT_\w+)", constants)[1]

def function(source, name):
    for match in re.finditer(name + r"\(", source):
        start = source.rfind("\n", 0, match.start()) + 1
        if source[start:match.start()].startswith(("static ", "void ", "size_t ")):
            end = source.index("{", match.start()) + 1
            depth = 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[start:end]
    raise AssertionError(name)

parts = [function(common, n) for n in ("pgraph_converted_texture_size", "pgraph_convert_texture_data_into")]
parts += [function(vk, n) for n in ("stage_level", "expand_rgb_snorm_alpha", "stage_swizzled",
                                   "vk_format_texel_size", "check_surface_to_texture_compatiblity")]
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", "\n".join(parts))
code = code.replace("ORACLE_FORMAT", fmt)
with tempfile.TemporaryDirectory(prefix="vk-rgb-snorm-") as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "gcc", "-O2", "-std=gnu11",
                    "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(repo),
                    str(folder / "test.c"), "-lvulkan", "-o", str(folder / "test")], check=True)
    subprocess.run([str(folder / "test")], env=os.environ, check=True)
