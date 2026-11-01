# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual texture scratch growth: discard old pixels, preserve the other slot."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
s = (r / 'hw/xbox/nv2a/pgraph/gl/texture.c').read_text()
m = re.search(r'^static uint8_t \*tex_scratch\([^;{]*\)\s*\{', s, re.M)
assert m
end = s.index('{', m.start()) + 1
depth = 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef struct {
    uint8_t *tex_scratch[2]; size_t tex_scratch_size[2];
} PGRAPHGLState;
static void *live[2];
static size_t capacity[2];
static unsigned slot, allocations, releases;
static void *allocate(size_t n) {
    /* Growth must release this slot first; the other slot stays live. */
    assert(!live[slot] && n); allocations++;
    void *p=malloc(n); assert(p); memset(p,0xCD,n);
    live[slot]=p; capacity[slot]=n; return p;
}
static void release(void *p) {
    if(p) {
        assert(p==live[slot]);releases++;
        memset(p,0xDD,capacity[slot]);free(p);live[slot]=NULL;
    }
}
#define g_malloc allocate
#define g_free release
/* PRODUCTION */
int main(void) {
    PGRAPHGLState r={0};
    size_t requests[]={0,1,7,2,16,4,257,3,8192,4096,8193,0,32768,64};
    for(unsigned pass=0;pass<1000;pass++)for(unsigned n=0;n<14;n++) {
        slot=(pass+n)&1;unsigned other=slot^1;
        size_t need=requests[n],before=r.tex_scratch_size[slot];
        uint8_t *old=r.tex_scratch[slot],*peer=r.tex_scratch[other];
        unsigned count=allocations;
        uint8_t *p=tex_scratch(&r,slot,need);
        assert(p==live[slot] && r.tex_scratch[other]==peer);
        if(need>before) {
            assert(allocations==count+1);
            assert(r.tex_scratch_size[slot]==MAX(need,2*before));
            for(size_t i=0;i<need;i++)assert(p[i]==0xCD);
        } else {
            assert(p==old && allocations==count);
            assert(r.tex_scratch_size[slot]==before);
        }
        if(peer)for(size_t i=0;i<r.tex_scratch_size[other];i++)assert(peer[i]==(uint8_t)(0x20+other));
        if(p)memset(p,0x20+slot,r.tex_scratch_size[slot]);
    }
    for(slot=0;slot<2;slot++) {release(r.tex_scratch[slot]);r.tex_scratch[slot]=NULL;r.tex_scratch_size[slot]=0;}
    assert(releases==allocations && !live[0] && !live[1]);
    printf("14000 alternating requests: %u growth allocations, discard/doubling/reuse/independent slots/final release PASS\n",allocations);
    return 0;
}
'''.replace('/* PRODUCTION */', s[m.start():end])
with tempfile.TemporaryDirectory(prefix='texture-scratch-') as tmp:
    p = Path(tmp); (p / 'test.c').write_text(code)
    binary = os.environ.get('TEX_SCRATCH_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('TEX_SCRATCH_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', *flags, str(p/'test.c'),
                    '-o', binary], check=True)
    if not os.environ.get('TEX_SCRATCH_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=60)
