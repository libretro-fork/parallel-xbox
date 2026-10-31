#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the complete production memory cursor against byte/bounds oracles."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve()
compiler=shlex.split(sys.argv[2] if len(sys.argv)>2 else "cc")
source=(root/"migration/qemu-file.c").read_text()
source=source[source.index("/* Saved-state cursors"):]
header=(root/"migration/qemu-file.h").read_text()
begin=header.index("struct QEMUFile {");end=header.index("\n};",begin)+3
code=Path(__file__).with_suffix(".c").read_text().replace("/* STRUCT */",header[begin:end]).replace("/* PRODUCTION */",source)
with tempfile.TemporaryDirectory(prefix="qemu-file-memory-") as temporary:
    directory=Path(temporary);(directory/"test.c").write_text(code)
    binary=Path(os.environ.get("QEMU_FILE_BINARY",str(directory/"test")))
    cmd=compiler+["-O2","-std=gnu11","-Wall","-Wextra","-Werror","-Wno-unused-parameter"]
    cmd+=shlex.split(os.environ.get("QEMU_FILE_CFLAGS",""))+[str(directory/"test.c"),"-o",str(binary)]
    subprocess.run(cmd,check=True)
    if not os.environ.get("QEMU_FILE_SKIP_RUN"):
        subprocess.run([str(binary)],check=True,timeout=30)
