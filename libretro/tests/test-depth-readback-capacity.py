# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production readback capacity and batch eligibility planning."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
source = (repo / "hw/xbox/nv2a/pgraph/vk/surface.c").read_text()

def function(name):
    position = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

start = source.index("static bool download_surface_to_buffer(")
end = source.index("    if (!batched)", start)
prefix = source[start:end]
# Confirm that the actual ensure-capacity call uses the same computed size.
readback = function("download_surface_to_buffer")
assert "pgraph_vk_buffer_ensure_capacity(pg, BUFFER_COMPUTE_DST,\n                                         compute_size);" in readback
tail = """
    (void)ram_size;
    observed_size = compute_size;
    return batched;
}
"""
parts = [function("depth_stencil_plane_size"), function("surface_readback_needs_swizzle"), prefix + tail]
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="depth-readback-capacity-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo / "deps/vulkan-headers"), str(folder / "test.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
