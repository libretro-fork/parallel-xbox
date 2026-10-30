# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual kernel-idle proof against a bounded guest-memory mock."""
from pathlib import Path
import subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve()
source=(repo/"target/i386/tcg/system/misc_helper.c").read_text(encoding="utf-8",errors="surrogateescape")
a=source.index("static const uint8_t *lr_idle_ram_ptr(")
b=source.index("/* A port read",a)
code=Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */",source[a:b])
with tempfile.TemporaryDirectory(prefix="kernel-idle-") as directory:
 p=Path(directory);(p/"test.c").write_text(code)
 exe=p/"test.exe"
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else "cc","-O2","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=undefined","-fsanitize-undefined-trap-on-error",str(p/"test.c"),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
