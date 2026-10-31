#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production CPU work dispatch against a minimal CPU fixture."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else "cc")
source = (root / "cpu-common.c").read_text()
starts = ["struct qemu_work_item {", "static void queue_work_on_cpu(",
          "void start_exclusive(", "void end_exclusive(",
          "void cpu_exec_start(", "void cpu_exec_end(",
          "void do_run_on_cpu(", "void async_run_on_cpu(",
          "void async_safe_run_on_cpu(", "static struct qemu_work_item *take_cpu_work(",
          "void free_queued_cpu_work(", "void process_queued_cpu_work("]
with tempfile.TemporaryDirectory(prefix="cpu-work-") as temporary:
    directory = Path(temporary)
    pieces = []
    for start in starts:
        begin = source.index(start)
        ending = "\n};" if start.startswith("struct ") else "\n}"
        end = source.index(ending, begin) + len(ending)
        pieces.append(source[begin:end])
    (directory / "cpu-work-production.h").write_text("\n\n".join(pieces))
    windows = os.name == "nt" or "mingw" in compiler[0]
    binary = Path(os.environ["CPU_WORK_BINARY"]) if "CPU_WORK_BINARY" in os.environ else (
        directory / ("cpu-work.exe" if windows else "cpu-work"))
    command = compiler + ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                         "-Wno-unused-parameter", "-DHAVE_THREADS",
                         "-I" + str(root / "libretro-common/include"),
                         "-I" + str(directory)]
    command += shlex.split(os.environ.get("CPU_WORK_CFLAGS", ""))
    command += [str(root / path) for path in ["libretro/tests/test-cpu-work.c",
                "libretro-common/rthreads/retro_eventcount.c",
                "libretro-common/rthreads/rthreads.c"]]
    if windows:
        command += [str(root / path) for path in [
            "libretro-common/features/features_cpu.c", "libretro-common/time/rtime.c"]]
        command += ["-Wno-cast-function-type"]
    command += ["-pthread", "-o", str(binary)]
    subprocess.run(command, check=True)
    if not os.environ.get("CPU_WORK_SKIP_RUN"):
        subprocess.run([str(binary)], check=True, timeout=60)
