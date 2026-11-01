# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise current-image draw references, replacement and deferred retirement."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
texture = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
draw = (repo / "hw/xbox/nv2a/pgraph/vk/draw.c").read_text()

def function(source, name):
    match = re.search(r"^(?:static )?(?:bool|void) " + name + r"\([^;{]*\)\s*\{", source, re.M)
    assert match, name
    start = match.start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

start = texture.index("    {\n        PGRAPHVkState *rr =", texture.index("static void upload_texture_image("))
end = texture.index("    PGRAPHVkState *r =", start)
guard = "static void upload_guard(PGRAPHState *pg, TextureBinding *binding)\n{\n" + texture[start:end] + "}\n"
parts = [function(draw, "pgraph_vk_trash_push"), function(texture, "texture_cow_if_referenced"),
         function(texture, "update_timestamps"), function(texture, "texture_cache_entry_init"),
         function(texture, "texture_cache_entry_pre_evict"), function(texture, "texture_cache_release_node_resources"), guard]
code = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="texture-cow-ordering-", dir=repo.parent) as directory:
    folder = Path(directory)
    (folder / "test.c").write_text(code)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    subprocess.run([cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-sign-compare", "-Wno-unused-parameter", "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
                    "-I", str(repo / "deps/vulkan-headers"), str(folder / "test.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
