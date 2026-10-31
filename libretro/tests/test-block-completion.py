# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual libretro block request functions against a bottom-half boundary model."""
from pathlib import Path
import os, re, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "cc"
source = Path(os.environ.get("BLOCK_COMPLETION_TEST_SOURCE", repo / "block/libretro-disk.c")).read_text()

def function(signature):
    definition = re.search(re.escape(signature) + r"[^;{]*\{", source)
    start = definition.start()
    end = definition.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

limit = re.search(r"^#define LR_DISK_REQS \d+", source, re.M).group()
signatures = ["static void lr_disk_req_done(", "static BlockAIOCB *lr_disk_submit(",
              "static bool lr_disk_run_pending(", "static bool lr_disk_progress(",
              "void blk_aio_cancel_async(", "void blk_aio_cancel(", "void blk_drain(",
              "static int lr_disk_in_flight(", "void bdrv_drain_all("]
production = "\n".join(function(s) for s in signatures)
code = limit + "\n" + Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", production)
with tempfile.TemporaryDirectory(prefix="block-completion-") as tmp:
    work = Path(tmp)
    unit = work / "test.c"
    unit.write_text(code)
    exe = Path(os.environ.get("BLOCK_COMPLETION_TEST_OUTPUT", work / ("test.exe" if os.name == "nt" else "test")))
    flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-I" + str(repo / "include"),
             "-I" + str(repo / "libretro-common/include")]
    flags += ["-fsanitize=address,undefined"] if os.name != "nt" and "mingw" not in cc else ["-static"]
    subprocess.run([cc, *flags, str(unit), "-o", str(exe)], check=True)
    if not os.environ.get("BLOCK_COMPLETION_TEST_NO_RUN"):
        modes = sys.argv[3:] or ["ordinary", "self-drain", "self-cancel", "drain-others", "submit-drain", "global-drain", "cancel-other", "slot-hold", "nested-drain"]
        for mode in modes:
            subprocess.run([str(exe), mode], check=True, timeout=15)
