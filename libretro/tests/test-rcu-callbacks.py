#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production callback worker, publication and intrusive nodes."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve()
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else "cc")
source = (root / "util/rcu.c").read_text()
begin = source.index("static retro_mpsc_t rcu_callbacks;")
end = source.index("struct rcu_drain {", begin)
callbacks = source[begin:end].replace("    for (;;) {", "    while (!retro_atomic_load_acquire_int(&stop)) {", 1)
callbacks = callbacks.replace("        if (!batch) {", "        if (!batch) {\n            if (retro_atomic_load_acquire_int(&stop)) break;", 1)
header = (root / "include/qemu/rcu.h").read_text()
begin = header.index("struct rcu_head {")
head = header[begin:header.index("\n};", begin)+3]
code = Path(__file__).with_suffix(".c").read_text().replace("/* HEAD */", head).replace("/* CALLBACKS */", callbacks)
with tempfile.TemporaryDirectory(prefix="rcu-callbacks-") as temporary:
    directory = Path(temporary)
    (directory / "test.c").write_text(code)
    binary = Path(os.environ.get("RCU_CALLBACK_BINARY", str(directory / "test")))
    windows = os.name == "nt" or "mingw" in compiler[0]
    cmd = compiler + ["-O1", "-g", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DHAVE_THREADS", "-I"+str(root / "libretro-common/include")]
    cmd += shlex.split(os.environ.get("RCU_CALLBACK_CFLAGS", ""))
    cmd += [str(directory / "test.c"), str(root / "libretro-common/rthreads/retro_eventcount.c"), str(root / "libretro-common/rthreads/rthreads.c")]
    if windows:
        cmd += ["-Wno-cast-function-type", str(root / "libretro-common/features/features_cpu.c"), str(root / "libretro-common/time/rtime.c")]
    cmd += ["-pthread", "-o", str(binary)]
    subprocess.run(cmd, check=True)
    if not os.environ.get("RCU_CALLBACK_SKIP_RUN"):
        subprocess.run([str(binary)], check=True, timeout=90)
