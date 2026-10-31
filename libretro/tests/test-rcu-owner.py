#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production RCU request ownership and read-side grace periods."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else "cc")
source = (root / "util/rcu.c").read_text()
header = (root / "include/qemu/rcu.h").read_text()
readers = header[header.index("struct rcu_reader_data {"):header.index("void synchronize_rcu(void);")]
control = source[source.index("retro_atomic_size_t rcu_gp_ctr ="):source.index("/* Multi-producer, single-consumer queue based")]
# Production publication points expose a deterministic overlapping-period schedule.
control = control.replace("retro_mpsc_push(&rcu_requests, &request.node);",
                          "retro_mpsc_push(&rcu_requests, &request.node); test_published(kind);")
begin = control.index("static void rcu_gp_worker(")
end = control.index("void synchronize_rcu(void)", begin)
worker = control[begin:end].replace("for (;;) {", "while (!retro_atomic_load_acquire_int(&stop)) {", 1)
worker = worker.replace("} else {\n            retro_eventcount_commit_wait",
                        "} else if (retro_atomic_load_acquire_int(&stop)) {\n"
                        "            retro_eventcount_cancel_wait(&rcu_gp_event);\n"
                        "        } else {\n            retro_eventcount_commit_wait")
control = control[:begin] + worker + control[end:]
lifecycle = source[source.index("void rcu_register_thread(void)"):source.index("static void rcu_init_complete(")]
initialization = source[source.index("static void rcu_init_complete("):source.index("static int atfork_depth")]
begin = source.index("static void rcu_init_child(void)")
end = source.index("\n}", begin) + 2
child = "\n#ifndef _WIN32\n" + source[begin:end].replace("static void rcu_init_child", "static void __attribute__((unused)) rcu_init_child") + "\n#endif\n"
notify = (root / "util/notify.c").read_text()
notify = notify[notify.index("void notifier_list_init("):]
with tempfile.TemporaryDirectory(prefix="rcu-owner-") as temporary:
    directory = Path(temporary)
    (directory / "rcu-read-production.h").write_text(readers)
    (directory / "rcu-owner-production.h").write_text(control + "\n" + lifecycle + initialization + child)
    (directory / "rcu-notify-production.h").write_text(notify)
    windows = os.name == "nt" or "mingw" in compiler[0]
    binary = Path(os.environ["RCU_OWNER_BINARY"]) if "RCU_OWNER_BINARY" in os.environ else (
        directory / ("test.exe" if windows else "test"))
    command = compiler + ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                         "-Wno-unused-parameter", "-DHAVE_THREADS",
                         "-I" + str(root / "include"),
                         "-I" + str(root / "libretro-common/include"), "-I" + str(directory)]
    flags = shlex.split(os.environ.get("RCU_OWNER_CFLAGS", ""))
    if any("fsanitize=thread" in flag for flag in flags):
        # TSan cannot start new threads in a child of a multithreaded fork.
        command += ["-DRCU_OWNER_NO_FORK"]
    command += flags
    command += [str(root / path) for path in ["libretro/tests/test-rcu-owner.c",
                "libretro-common/rthreads/retro_eventcount.c", "libretro-common/rthreads/rthreads.c"]]
    if windows:
        command += ["-Wno-cast-function-type"]
        command += [str(root / path) for path in [
            "libretro-common/features/features_cpu.c", "libretro-common/time/rtime.c"]]
    command += ["-pthread", "-o", str(binary)]
    subprocess.run(command, check=True)
    if not os.environ.get("RCU_OWNER_SKIP_RUN"):
        subprocess.run([str(binary)], check=True, timeout=120)
