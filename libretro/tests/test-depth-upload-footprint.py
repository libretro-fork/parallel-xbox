# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production depth-upload sizing and barrier/region declarations."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
source = (repo / "hw/xbox/nv2a/pgraph/vk/surface.c").read_text()

def block(start):
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

helper = block(source.index("static VkDeviceSize depth_stencil_plane_size("))
upload = source.index("void pgraph_vk_upload_surface_data(")
capacity = block(source.index("    if (use_compute_to_convert_depth_stencil_format) {", upload))
start = source.index("        VkDeviceSize packed_input_size =", upload)
end = source.index("        VkBufferMemoryBarrier post_copy_src_barrier", start)
calculation = source[start:end]
barrier_start = source.index("        VkBufferMemoryBarrier post_copy_src_barrier", upload)
barrier_end = source.index("        // Already scaled during compute.", barrier_start)
barriers = source[barrier_start:barrier_end]
region = re.search(r"        regions\[1\]\.bufferOffset =.*?;", source[upload:], re.S)[0]
code = Path(__file__).with_suffix(".c").read_text().replace("/* HELPER */", helper).replace(
    "/* PLAN */", capacity + "\n" + calculation + "\n" + barriers + "\n" + region)
with tempfile.TemporaryDirectory(prefix="depth-upload-footprint-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo / "deps/vulkan-headers"), str(folder / "test.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
