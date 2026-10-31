#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production Windows thread-exit registration and cleanup."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve()
compiler=shlex.split(sys.argv[2] if len(sys.argv)>2 else "cc")
source=(root/"util/qemu-thread-win32.c").read_text()
begin=source.index("static QemuWin32TLS tls_key_host_thread_exit")
end=source.index("static unsigned __stdcall win32_start_routine",begin)
notify=(root/"util/notify.c").read_text()
notify=notify[notify.index("void notifier_list_init("):]
code=Path(__file__).with_suffix(".c").read_text().replace("/* NOTIFY */",notify).replace("/* EXIT */",source[begin:end])
with tempfile.TemporaryDirectory(prefix="win32-thread-exit-") as temporary:
    directory=Path(temporary);(directory/"test.c").write_text(code)
    binary=Path(os.environ.get("WIN32_EXIT_BINARY",str(directory/"test.exe")))
    subprocess.run(compiler+["-O2","-std=gnu11","-Wall","-Wextra","-Werror","-Wno-unused-parameter","-DHAVE_THREADS","-DHAVE_THREAD_STORAGE","-Wno-cast-function-type","-I"+str(root/"include"),"-I"+str(root/"libretro-common/include"),str(directory/"test.c"),*[str(root/p) for p in ["libretro-common/rthreads/rthreads.c","libretro-common/features/features_cpu.c","libretro-common/time/rtime.c"]],"-pthread","-static","-o",str(binary)],check=True)
    if not os.environ.get("WIN32_EXIT_SKIP_RUN"):
        subprocess.run([str(binary)],check=True,timeout=30)
