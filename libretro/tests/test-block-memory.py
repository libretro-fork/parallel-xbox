# SPDX-License-Identifier: GPL-2.0-or-later
"""Production block read/write/zero functions against faulting address-space I/O."""
from pathlib import Path
import os, re, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "cc"
source = Path(os.environ.get("BLOCK_MEMORY_TEST_SOURCE", repo / "block/libretro-disk.c")).read_text()

def function(signature):
    match = re.search(re.escape(signature) + r"[^;{]*\{", source)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef ENOMEDIUM
#define ENOMEDIUM ENODEV
#endif
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MEMTXATTRS_UNSPECIFIED 0
#define MEMTX_OK 0
typedef int MemTxResult;
typedef struct { void *iov_base; size_t iov_len; } IOV;
typedef IOV LRFileIOV;
typedef struct { IOV *iov; int niov; size_t size; } QEMUIOVector;
enum { LR_MEDIUM_NONE, LR_MEDIUM_FILE, LR_MEDIUM_QCOW2, LR_MEDIUM_VFS, LR_MEDIUM_MEMORY };
typedef struct { int kind; int64_t length, base, vfs_position; bool writable;
                 void *file, *qcow2, *vfs, *as; } LRMedium;
typedef struct { LRMedium m; } BlockBackend;
static void *xemu_libretro_vfs;
static int calls, fail_at, failure, writing;
static int64_t next_offset;
static MemTxResult transfer(void *as, int64_t off, int attr, const void *buf, size_t len, bool write) {
    (void)as; (void)attr; (void)buf;
    assert(off == next_offset && write == writing);
    calls++; next_offset += len;
    return calls == fail_at ? failure : MEMTX_OK;
}
static MemTxResult address_space_read(void *as, int64_t off, int attr, void *buf, size_t len) {
    return transfer(as, off, attr, buf, len, false);
}
static MemTxResult address_space_write(void *as, int64_t off, int attr, const void *buf, size_t len) {
    return transfer(as, off, attr, buf, len, true);
}
static int filev(void *p, int64_t off, const LRFileIOV *v, int n) {
    (void)p; (void)off; (void)v; (void)n; assert(0); return -EIO;
}
#define lr_file_preadv filev
#define lr_file_pwritev filev
#define lr_qcow2_preadv filev
#define lr_qcow2_pwritev filev
static int lr_vfs_readv(void *api, void *file, int64_t *pos, int64_t off, const LRFileIOV *v, int n) {
    (void)api; (void)pos; return filev(file, off, v, n);
}
static int filezero(void *p, int64_t off, int64_t len) {
    (void)p; (void)off; (void)len; assert(0); return -EIO;
}
#define lr_file_zero filezero
#define lr_qcow2_zero filezero
/* PRODUCTION */
int main(void) {
    char data[8193];
    IOV iov[] = {{data, 17}, {data + 17, 4096}, {data + 4113, 4080}};
    QEMUIOVector q = {iov, 3, sizeof(data)};
    BlockBackend b = { .m = { .kind = LR_MEDIUM_MEMORY, .length = 16384, .writable = true } };
    for (int op = 0; op < 3; op++) {
        writing = op != 0;
        for (failure = 1; failure <= 7; failure++) {
            for (fail_at = 0; fail_at <= 3; fail_at++) {
                calls = 0; next_offset = 123;
                int ret = op == 2 ? lr_disk_zero(&b, 123, sizeof(data))
                                  : lr_disk_rw(&b, 123, &q, writing);
                assert(ret == (fail_at ? -EIO : 0));
                assert(calls == (fail_at ? fail_at : 3));
                if (!fail_at) assert(next_offset == 123 + sizeof(data));
            }
        }
    }
    puts("block memory: read/write/zero errors propagated, later transfers stopped: PASS");
}
'''.replace("/* PRODUCTION */", function("static int lr_disk_rw(") + "\n" + function("int lr_disk_zero("))
with tempfile.TemporaryDirectory(prefix="block-memory-") as tmp:
    unit = Path(tmp) / "test.c"
    unit.write_text(code)
    exe = Path(os.environ.get("BLOCK_MEMORY_TEST_OUTPUT", Path(tmp) / "test"))
    flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra"]
    flags += ["-static"] if "mingw" in cc or os.name == "nt" else ["-fsanitize=address,undefined"]
    subprocess.run([cc, *flags, str(unit), "-o", str(exe)], check=True)
    if not os.environ.get("BLOCK_MEMORY_TEST_NO_RUN"):
        subprocess.run([str(exe)], check=True, timeout=15)
