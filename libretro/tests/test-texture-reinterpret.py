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
gate = source[match.start():end]
code = Path(__file__).with_suffix(".c").read_text().replace(
    "/* PRODUCTION */", gate)
with tempfile.TemporaryDirectory(prefix="texture-alias-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo), "-I", str(repo / "deps/vulkan-headers"),
                    str(folder / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# Apply the same layout/overlap cases to the OpenGL gate. GL allocates its
# transfer buffer on demand and has no Vulkan native-resolution image mode.
gl_source = (repo / "hw/xbox/nv2a/pgraph/gl/texture.c").read_text()
match = re.search(r"^static bool can_reinterpret_argb_as_rgb565\(", gl_source, re.M)
assert match
start = gl_source.index("{", match.start())
end, depth = start + 1, 1
while depth:
    depth += (gl_source[end] == "{") - (gl_source[end] == "}")
    end += 1
gl_gate = gl_source[match.start():end]
# A representation adapter leaves the extracted production predicate unchanged.
code = code.replace(gate, gl_gate)
code = code.replace("    REJECT_FIELD(source,native_resolution,true);", "")
code = code.replace("    renderer.storage_buffers[0].buffer_size=2047;\n    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));", "")
code = code.replace("    pg.surface_scale_factor=2; scratch_available=false;\n    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));", "")
code = code.replace("static bool scratch_available = true;", "")
a = code.index("static VkImage native_get_scratch")
b = code.index("static bool can_reinterpret_argb_as_rgb565", a)
code = code[:a] + code[b:]
code = code.replace("PGRAPHVkState", "PGRAPHGLState").replace("vk_renderer_state", "gl_renderer_state")
code = code.replace("host_fmt", "fmt").replace("vk_format", "gl_internal_format")
code = code.replace("invalid_surfaces", "parked").replace("VK_FORMAT_B8G8R8A8_UNORM", "GL_RGBA8")
code = code.replace("formats, layouts, bounds, capacity, scratch and dirty-overlap",
                    "OpenGL formats, layouts, bounds and dirty-overlap")
code = "#define GL_RGBA8 0x8058\n" + code
with tempfile.TemporaryDirectory(prefix="gl-texture-alias-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo), "-I", str(repo / "deps/vulkan-headers"),
                    str(folder / "test.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
