# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual IDE cancellation/completion code with re-entrant callbacks."""
from pathlib import Path
import os, re, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "cc"
source = Path(os.environ.get("IDE_CANCEL_TEST_SOURCE", repo / "hw/ide/core.c")).read_text()

def function(signature):
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

limit = re.search(r"^#define MAX_BUFFERED_REQS \d+", source, re.M).group()
production = function("static void ide_buffered_readv_cb(") + "\n" + function("void ide_cancel_dma_sync(")
code = limit + "\n" + Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", production)
with tempfile.TemporaryDirectory(prefix="ide-cancel-") as tmp:
    work = Path(tmp)
    unit = work / "test.c"
    unit.write_text(code)
    exe = Path(os.environ.get("IDE_CANCEL_TEST_OUTPUT", work / ("test.exe" if os.name == "nt" else "test")))
    flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-I" + str(repo / "include"),
             "-I" + str(repo / "libretro-common/include")]
    if os.name != "nt" and "mingw" not in cc:
        flags += ["-fsanitize=address,undefined"]
    else:
        flags += ["-static"]
    subprocess.run([cc, *flags, str(unit), "-o", str(exe)], check=True)
    if not os.environ.get("IDE_CANCEL_TEST_NO_RUN"):
        modes = sys.argv[3:] or ["normal", "recursive", "callback-drain", "new-request", "orphan", "unbuffered", "recursive-unbuffered", "limit", "complete-cancel", "error-cancel", "complete-submit"]
        for mode in modes:
            subprocess.run([str(exe), mode], check=True, timeout=15)
