# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production alias eligibility gate. Usage: python SCRIPT CHECKOUT [CC]."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
source = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
match = re.search(r"^static bool can_reinterpret_argb_as_rgb565\(", source, re.M)
assert match
start = source.index("{", match.start())
end, depth = start + 1, 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
code = Path(__file__).with_suffix(".c").read_text().replace(
    "/* PRODUCTION */", source[match.start():end])
with tempfile.TemporaryDirectory(prefix="texture-alias-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo), "-I", str(repo / "deps/vulkan-headers"),
                    str(folder / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
