#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production shader cache publication and immutable disk-save jobs."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else "cc")
source = (root / "hw/xbox/nv2a/pgraph/gl/shaders.c").read_text()
starts = ["typedef struct ShaderDiskResult {", "static void shader_adopt_disk_results(",
          "static void shader_write_lru_list_entry_to_disk(",
          "void pgraph_gl_shader_write_cache_reload_list(",
          "static void shader_write_to_disk(", "static void shader_start_disk_save("]
pieces = []
for start in starts:
    begin = source.index(start)
    ending = "\n} ShaderDiskResult;" if start.startswith("typedef") else "\n}"
    end = source.index(ending, begin) + len(ending)
    pieces.append(source[begin:end])
with tempfile.TemporaryDirectory(prefix="shader-cache-handoff-") as temporary:
    directory = Path(temporary)
    (directory / "shader-cache-production.h").write_text("\n\n".join(pieces))
    windows = os.name == "nt" or "mingw" in compiler[0]
    binary = Path(os.environ["SHADER_CACHE_BINARY"]) if "SHADER_CACHE_BINARY" in os.environ else (
        directory / ("test.exe" if windows else "test"))
    command = compiler + ["-O1", "-g", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                         "-Wno-unused-parameter", "-DHAVE_THREADS",
                         "-I" + str(root / "include"),
                         "-I" + str(root / "libretro-common/include"), "-I" + str(directory)]
    command += shlex.split(os.environ.get("SHADER_CACHE_CFLAGS", ""))
    command += [str(root / path) for path in ["libretro/tests/test-shader-cache-handoff.c",
                                            "libretro-common/rthreads/rthreads.c"]]
    if windows:
        command += ["-Wno-cast-function-type"]
        command += [str(root / path) for path in [
            "libretro-common/features/features_cpu.c", "libretro-common/time/rtime.c"]]
    command += ["-pthread", "-o", str(binary)]
    subprocess.run(command, check=True)
    if not os.environ.get("SHADER_CACHE_SKIP_RUN"):
        subprocess.run([str(binary), str(directory)], check=True, timeout=60)
