#!/usr/bin/env python3
"""Linux regression of the production Apple JIT adapter with a strict backend.
Usage: python3 test-libretro-jit.py CHECKOUT [CC]
"""
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "cc"
with tempfile.TemporaryDirectory(prefix="jit-adapter-", dir=repo.parent) as directory:
    work = Path(directory)
    (work / "qemu").mkdir()
    # Only QEMU's broad OS header is replaced. Compile the real adapter and
    # libretro-common API header, with the Apple Silicon path enabled.
    (work / "qemu/osdep.h").write_text(
        "#include <stdbool.h>\n"
        "void qemu_thread_jit_execute(void);\n"
        "void qemu_thread_jit_write(void);\n")
    (work / "other.c").write_text(
        '#include "qemu/osdep.h"\n'
        "void write_from_other_tu(void) { qemu_thread_jit_write(); }\n"
        "void execute_from_other_tu(void) { qemu_thread_jit_execute(); }\n")
    exe = work / "jit-test"
    subprocess.run([
        cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-fsanitize=undefined", "-fsanitize-undefined-trap-on-error",
        "-D__APPLE__", "-D__aarch64__", f"-I{work}",
        f"-I{repo / 'libretro-common/include'}",
        str(repo / "util/libretro-jit.c"),
        str(repo / "libretro/tests/test-libretro-jit.c"),
        str(work / "other.c"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
