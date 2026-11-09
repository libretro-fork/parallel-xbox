#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production GLSL generation handoff without a GL driver."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else "cc")
source = (root / "hw/xbox/nv2a/pgraph/gl/shaders.c").read_text()
begin = source.index("#define GEN_JOBS 16")
end = source.index("static bool module_known(", begin)
production = source[begin:end]
# Give the production worker a shutdown point for this bounded test only.
production = production.replace("for (;;) {", "while (!retro_atomic_load_acquire_int(&stop)) {", 1)
production = production.replace("if (gen_jobs_pending()) {",
                                "if (retro_atomic_load_acquire_int(&stop) || gen_jobs_pending()) {")
with tempfile.TemporaryDirectory(prefix="glslgen-queue-") as temporary:
    directory = Path(temporary)
    (directory / "glslgen-production.h").write_text(production)
    windows = os.name == "nt" or "mingw" in compiler[0]
    binary = Path(os.environ["GLSLGEN_BINARY"]) if "GLSLGEN_BINARY" in os.environ else (
        directory / ("test.exe" if windows else "test"))
    command = compiler + ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                         "-Wno-unused-parameter", "-Wno-unused-variable", "-DHAVE_THREADS",
                         "-I" + str(root / "libretro-common/include"), "-I" + str(directory)]
    command += shlex.split(os.environ.get("GLSLGEN_CFLAGS", ""))
    command += [str(root / path) for path in ["libretro/tests/test-glslgen-queue.c",
                "libretro-common/rthreads/retro_eventcount.c",
                "libretro-common/rthreads/rthreads.c"]]
    if windows:
        command += [str(root / path) for path in [
            "libretro-common/compat/compat_strl.c","libretro-common/features/features_cpu.c", "libretro-common/time/rtime.c"]]
        command += ["-Wno-cast-function-type"]
    command += ["-pthread", "-o", str(binary)]
    subprocess.run(command, check=True)
    if not os.environ.get("GLSLGEN_SKIP_RUN"):
        subprocess.run([str(binary)], check=True, timeout=90)
        # Copying the published key back onto the worker's stack violates the
        # direct-owner input contract even if the emitted values match.
        borrow = "const ShaderModuleCacheKey *key = &gen_jobs[i].key;"
        assert production.count(borrow) == 1
        copied = production.replace(borrow,
            "ShaderModuleCacheKey snapshot = gen_jobs[i].key;\n"
            "    const ShaderModuleCacheKey *key = &snapshot;", 1)
        (directory / "glslgen-production.h").write_text(copied)
        mutant_binary = directory / ("snapshot.exe" if windows else "snapshot")
        subprocess.run(command[:-1] + [str(mutant_binary)], check=True)
        result = subprocess.run([str(mutant_binary)], capture_output=True, timeout=90)
        assert result.returncode, "worker key snapshot escaped the borrow control"
        print("Required negative control rejected: generator input copied away from its queue owner")
