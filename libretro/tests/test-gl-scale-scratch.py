# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production discard-on-growth scratch, reuse and finalization."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
header = (r / 'hw/xbox/nv2a/pgraph/pool.h').read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations, releases, live;
static size_t peak, current;
static void *allocate(size_t n) {
    assert(n && !live); void *p = malloc(n); assert(p);
    allocations++; live++; current = n; if (n > peak) peak = n;
    memset(p, 0xCD, n); return p;
}
static void release(void *p) {
    if (p) { assert(live == 1); releases++; live--; current = 0; free(p); }
}
#define g_malloc allocate
#define g_free release
/* SCRATCH */
int main(void) {
    Scratch s = {0};
    assert(!scratch_reserve(&s, 0) && !allocations);
    size_t requests[] = {1024, 64, 4096, 1, 16384, 1024, 0, 8192};
    for (unsigned i = 0; i < 10000; i++) {
        size_t n = requests[i % 8], old_capacity = s.size;
        uint8_t *old = s.data;
        unsigned before = allocations;
        uint8_t *p = scratch_reserve(&s, n);
        assert(s.size >= n && current == s.size);
        if (n <= old_capacity) {
            assert(p == old && allocations == before);
        } else {
            assert(allocations == before + 1);
            for (size_t j = 0; j < n; j++) assert(p[j] == 0xCD);
        }
        /* Fully overwrite temporary data, including after smaller uses. */
        if (n) memset(p, (int)i, n);
    }
    assert(allocations == 3 && peak == 16384);
    scratch_finalize(&s);
    assert(!s.data && !s.size && !live && releases == allocations);
    scratch_finalize(&s);
    assert(!scratch_reserve(&s, 0));
    uint8_t *p = scratch_reserve(&s, 7); assert(p && s.size == 7);
    scratch_finalize(&s); assert(!live && releases == allocations);
    printf("10000 varying-size requests: 3 growth allocations, zero reuse allocations; discard/zero/reinit/finalize PASS\n");
    return 0;
}
'''.replace('/* SCRATCH */', header[header.index('typedef struct Scratch'):header.rindex('#endif')])
with tempfile.TemporaryDirectory(prefix='gl-scale-scratch-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
    binary = os.environ.get('SCALE_SCRATCH_BINARY', str(p / 'test'))
    flags = os.environ.get('SCALE_SCRATCH_CFLAGS', '-fsanitize=address,undefined')
    subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(flags), str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('SCALE_SCRATCH_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
