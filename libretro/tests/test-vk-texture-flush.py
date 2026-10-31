# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check production texture flush spans against allocated bytes across ring wrap."""
from pathlib import Path
import os, subprocess, sys, tempfile
r = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
s = (r / 'hw/xbox/nv2a/pgraph/vk/texture.c').read_text()
a = s.index('typedef struct TextureLevel')
b = s.index('/* The host format', a)
production = s[a:b]
code = r'''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint64_t VkDeviceSize;
typedef struct { int unused; } PGRAPHState;
#define MAX(a,b) ((a)>(b)?(a):(b))
static unsigned char flushed[4096], wanted[4096];
static unsigned calls;
static void pgraph_vk_staging_flush(PGRAPHState *pg, VkDeviceSize off, size_t size)
{ (void)pg; assert(size && off+size<=4096); calls++; memset(flushed+off,1,size); }
/* PRODUCTION */
int main(void) {
    PGRAPHState pg = {0};
    for (unsigned trial=0; trial<20000; trial++) {
        TextureLayout layout = {0};
        unsigned skip = trial % 64, head = trial % 2 ? 3900 : 0, wraps=0;
        memset(flushed,0,sizeof flushed); memset(wanted,0,sizeof wanted); calls=0;
        for (unsigned layer=0; layer<6; layer++) {
            if (skip & (1u<<layer)) continue;
            for (unsigned mip=0; mip<16; mip++) {
                TextureLevel *l=&layout.layers[layer].levels[mip];
                l->source = rand()%3;
                l->size = 1+rand()%24;
                l->crop_offset = l->size/2;
                if (l->source != LEVEL_STAGING) continue;
                if (head+l->size>4096) { head=0; wraps++; }
                assert(wraps<=1);
                l->offset=head;
                memset(wanted+head,1,l->size);
                head+=l->size;
            }
        }
        flush_texture_staging(&pg,&layout,6,16,skip);
        assert(calls<=2);
        assert(!memcmp(wanted,flushed,sizeof wanted));
    }
    puts("20000 production texture flush layouts passed");
}
'''.replace('/* PRODUCTION */', production)
env=os.environ.copy()
env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='texture-flush-') as tmp:
    d=Path(tmp); (d/'test.c').write_text(code)
    exe=d/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(d/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env,timeout=30)
