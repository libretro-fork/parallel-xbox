# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production discard range check before modeled media dispatch."""
from pathlib import Path
import os, re, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "cc"
source = Path(os.environ.get("BLOCK_DISCARD_TEST_SOURCE", repo / "block/libretro-disk.c")).read_text()
match = re.search(r"int lr_disk_zero\([^;{]*\{", source)
end, depth = match.end(), 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
function = source[match.start():end]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#ifndef ENOMEDIUM
#define ENOMEDIUM ENODEV
#endif
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MEMTXATTRS_UNSPECIFIED 0
enum { LR_MEDIUM_NONE, LR_MEDIUM_FILE, LR_MEDIUM_QCOW2, LR_MEDIUM_MEMORY };
typedef struct { int kind; int64_t length, base; bool writable; void *file, *qcow2, *as; } LRMedium;
typedef struct { LRMedium m; } BlockBackend;
static int calls;
static int64_t total;
static int zero(void *p, int64_t off, int64_t len) {
    (void)p; assert(off >= 0 && len >= 0); calls++; total += len; return 0;
}
#define lr_file_zero zero
#define lr_qcow2_zero zero
static void address_space_write(void *p, int64_t off, int attr, const void *buf, int64_t len) {
    (void)attr; (void)buf; zero(p, off, len);
}
/* PRODUCTION */
int main(void) {
    for (int kind = LR_MEDIUM_FILE; kind <= LR_MEDIUM_MEMORY; kind++) {
        BlockBackend b = { .m = { .kind = kind, .length = 8192, .writable = true } };
        const int64_t invalid[][2] = {{0,-1}, {8192,-1}, {0,INT64_MIN}, {-1,1},
            {8193,0}, {8192,1}, {1,INT64_MAX}, {INT64_MAX,1}};
        for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
            calls = 0; total = 0;
            assert(lr_disk_zero(&b, invalid[i][0], invalid[i][1]) == -EIO);
            assert(calls == 0 && total == 0);
        }
        calls = 0; total = 0;
        assert(lr_disk_zero(&b, 8192, 0) == 0 && total == 0);
        calls = 0; total = 0;
        assert(lr_disk_zero(&b, 4095, 4097) == 0 && total == 4097);
        assert(calls == (kind == LR_MEDIUM_MEMORY ? 2 : 1));
        b.m.writable = false; calls = 0;
        assert(lr_disk_zero(&b, 0, 1) == -EPERM && calls == 0);
        b.m.kind = LR_MEDIUM_NONE;
        assert(lr_disk_zero(&b, 0, 1) == -ENOMEDIUM && calls == 0);
    }
    puts("block discard: invalid ranges rejected before I/O, valid boundaries preserved: PASS");
}
'''.replace("/* PRODUCTION */", function)
with tempfile.TemporaryDirectory(prefix="block-discard-") as tmp:
    unit = Path(tmp) / "test.c"
    unit.write_text(code)
    exe = Path(os.environ.get("BLOCK_DISCARD_TEST_OUTPUT", Path(tmp) / "test"))
    flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra"]
    flags += ["-static"] if "mingw" in cc or os.name == "nt" else ["-fsanitize=address,undefined"]
    subprocess.run([cc, *flags, str(unit), "-o", str(exe)], check=True)
    if not os.environ.get("BLOCK_DISCARD_TEST_NO_RUN"):
        subprocess.run([str(exe)], check=True, timeout=15)
