# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the production depth-transfer planning prefix before GPU recording."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
source = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
start = source.index("static unsigned int vk_format_texel_size(")
end = source.index("\n// FIXME", start)
helper = source[start:end]
start = source.index("static void copy_zeta_surface_to_texture(")
end = source.index("    pgraph_vk_transition_image_layout(", start)
prefix = source[start:end]
tail = """
    assert(copied_image_size <= requested);
    assert(stencil_buffer_size == (num_regions == 2 ? (size_t)cw * ch : 0));
    assert(regions[0].imageExtent.width == cw && regions[0].imageExtent.height == ch);
    assert(num_regions == 1 || regions[1].bufferOffset == stencil_buffer_offset);
    planned_pixels = (size_t)cw * ch;
    (void)cmd;
}
"""
code = Path(__file__).with_suffix(".c").read_text().replace(
    "/* PRODUCTION */", helper + "\n" + prefix + tail)
with tempfile.TemporaryDirectory(prefix="depth-transfer-capacity-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo / "deps/vulkan-headers"),
                    str(folder / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
