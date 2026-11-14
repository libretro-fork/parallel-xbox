# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the production native color/D16 readback against a real Vulkan device."""
from pathlib import Path

vk_loader_root = Path(__file__).resolve().parents[2]
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
source = (repo / "hw/xbox/nv2a/pgraph/vk/surface.c").read_text()
parts = []
for name in ("depth_stencil_plane_size", "surface_readback_needs_swizzle", "readback_to_host_barrier", "download_surface_to_buffer"):
    position = source.index(name + "(")
    start = source.rfind("\nstatic ", 0, position) + 1
    end = source.index("{", position) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    parts.append(source[start:end])
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="single-axis-readback-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    # Unused variables belong to deliberately rejected fallback stubs.
                    "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                    "-I", str(repo / "deps/vulkan-headers"),
                    "-I", str(vk_loader_root / "libretro"), str(folder / "test.c"),
                    str(vk_loader_root / "libretro/vk_dispatch.c"),str(vk_loader_root / "libretro/tests/vk-test-loader.c"), "-o", str(exe)] +
                   ([] if os.name == "nt" else ["-ldl"]), check=True)
    subprocess.run([str(exe)], check=True, timeout=120)
