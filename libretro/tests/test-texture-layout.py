# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production texture span/face sizing with an independent mip walker.

Usage: python SCRIPT CHECKOUT [CC]. No graphics driver or QEMU runtime needed.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/texture.c").read_text()
header = (repo / "hw/xbox/nv2a/pgraph/texture.h").read_text()
pgraph = (repo / "hw/xbox/nv2a/pgraph/pgraph.h").read_text()

def function(text, name):
    match = re.search(r"^(?:static )?(?:inline )?(?:size_t|bool) " + name + r"\(", text, re.M)
    assert match, name
    end = text.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]

parts = []
for name in ("TextureShape", "BasicColorFormatInfo"):
    parts.append(re.search(r"typedef struct " + name + r" \{.*?\} " + name + ";", header, re.S)[0])
start = source.index("const BasicColorFormatInfo kelvin_color_format_info_map")
parts.append(source[start:source.index("\n};", start) + 3])
parts.append(function(pgraph, "pgraph_is_texture_format_compressed"))
parts += [function(source, name) for name in ("texture_mip_chain_size",
          "pgraph_get_cubemap_face_length", "pgraph_get_texture_length",
          "pgraph_converted_texture_size", "pgraph_texture_hash_length")]
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="texture-layout-") as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "gcc", "-O2", "-std=c99",
                    "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-Wno-missing-field-initializers", "-fsanitize=undefined",
                    "-fsanitize-undefined-trap-on-error", "-I", str(repo),
                    str(folder / "test.c"), "-o", str(folder / "test")], check=True)
    subprocess.run([str(folder / "test")], check=True)
