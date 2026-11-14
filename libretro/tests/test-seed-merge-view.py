#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production seed views/merging with poisoned mapping lifetimes."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile
from glib_fixture import compat_glib_flags
r = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
s = (r / 'hw/xbox/nv2a/pgraph/seed.c').read_text()
parts = []
for name in ['SeedHeader', 'SeedSection']:
    parts.append(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',s,re.S).group())
parts.append(re.search(r'struct SeedFind \{.*?\};', s, re.S).group())
for name in ['seed_walk','seed_find_visit','seed_find_view','seed_find','pgraph_seed_read','pgraph_seed_hash_set_new','pgraph_seed_hash_set_add','pgraph_seed_merge']:
    match = re.search(r'^(?:static )?[^\n]*\b'+name+r'\(.*?^\}',s,re.M|re.S)
    assert match, name
    parts.append(match.group())
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include "include/qemu/file-map.h"
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define SEED_MAGIC 0x44455358u
#define SEED_VERSION 1u
#define SEED_MAX_ITEMS 65536u
static unsigned duplicates, maps, unmaps, writes, written_count, payload_allocations;
static size_t payload_bytes;
static bool available, active, path_enabled = true, fail_write, force_collision;
static uint8_t image[32768];
static size_t image_len;
static uint32_t written[2048];
static char *seed_path(void) { return path_enabled ? g_strdup("test.seed") : NULL; }
bool xemu_file_map(XemuFileMap *m, const char *path) {
    assert(path && !active);
    if (!available) return false;
    m->owned = malloc(image_len ? image_len : 1);
    memcpy(m->owned, image, image_len);
    m->data = m->owned; m->len = image_len;
    active = true; maps++; return true;
}
void xemu_file_unmap(XemuFileMap *m) {
    assert(active); memset(m->owned, 0xa5, m->len);
    free(m->owned); memset(m,0,sizeof(*m)); active = false; unmaps++;
}
static gpointer duplicate(gconstpointer p, gsize n) {
    if (n != sizeof(uint64_t)) duplicates++; /* Hash keys remain owned. */
    return g_memdup2(p,n);
}
#define g_memdup2 duplicate
static gpointer payload_alloc(gsize n,gsize size) {
    payload_allocations++;payload_bytes+=n*size;return g_malloc_n(n,size);
}
#define g_malloc_n payload_alloc
static uint64_t xxh3_64(const void *p, size_t n, uint64_t seed __attribute__((unused))) {
    if(force_collision)return 7;
    const uint8_t *b=p; uint64_t h=1469598103934665603ull;
    for (size_t i=0;i<n;i++) h=(h^b[i])*1099511628211ull;
    return h;
}
bool pgraph_seed_write(int section, size_t size, const void *items, unsigned count) {
    assert(!active && section==7 && size==4 && count<=2048);
    writes++; written_count=count; memcpy(written,items,size*count);
    return !fail_write;
}
/* PRODUCTION */
static void setup(void) {
    assert(!active); available=true; path_enabled=true; fail_write=false;
    duplicates=maps=unmaps=writes=written_count=payload_allocations=0;payload_bytes=0;force_collision=false;
    SeedHeader hdr={SEED_MAGIC,SEED_VERSION,2,0};
    SeedSection other={9,1,3,0}, target={7,4,3,0};
    uint32_t records[]={1,2,1};
    size_t off=0;
#define PUT(v) do { memcpy(image+off,&(v),sizeof(v));off+=sizeof(v); } while(0)
    PUT(hdr); PUT(other); memcpy(image+off,"abc",3);off+=3;
    PUT(target); PUT(records); image_len=off;
#undef PUT
}
static unsigned rng=42;
static unsigned random_u(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void) {
    unsigned total;
    const uint32_t incoming[]={2,3,3,4};
    for(unsigned cap=1;cap<=8;cap++) {
        setup(); unsigned added=pgraph_seed_merge(7,4,incoming,4,cap,&total);
        unsigned expected=cap<=2?2:cap<4?3:4;
        assert(total==expected && added==expected-2 && !duplicates);
        assert(payload_allocations==!!added && payload_bytes==(added?expected*4:0));
        assert(maps==1 && unmaps==1 && !active && writes==!!added);
        if(added) { assert(written_count==expected); for(unsigned i=0;i<expected;i++) assert(written[i]==i+1); }
    }
    setup(); fail_write=true;
    assert(!pgraph_seed_merge(7,4,incoming,4,8,&total) && total==4);
    assert(!active && maps==unmaps && !duplicates && writes==1);
    for(unsigned mode=0;mode<5;mode++) {
        setup();
        if(mode==0) available=false;
        if(mode==1) path_enabled=false;
        if(mode==2) image[0]^=1;
        if(mode==3) image_len--;
        if(mode==4) { uint32_t size=8;memcpy(image+16+16+3+4,&size,4); }
        assert(pgraph_seed_merge(7,4,incoming,4,8,&total)==3 && total==3);
        assert(!duplicates && !active && maps==unmaps && writes==1);
        assert(written[0]==2 && written[1]==3 && written[2]==4);
    }
    setup(); uint32_t *owned=pgraph_seed_read(7,4,&total);
    assert(total==3 && duplicates==1 && !active && owned[0]==1 && owned[1]==2 && owned[2]==1);
    g_free(owned);
    setup(); assert(!pgraph_seed_merge(7,4,NULL,0,8,&total));
    assert(total==2 && !writes && !duplicates && !active);
    setup(); const uint32_t known[]={1,2,1,2};
    assert(!pgraph_seed_merge(7,4,known,4,8,&total) && total==2);
    assert(!writes && !payload_allocations && !payload_bytes && !active);
    for(unsigned trial=0;trial<512;trial++) {
        setup();force_collision=trial%5==0;fail_write=trial%11==0;
        unsigned disk_n=random_u()%129,in_n=random_u()%129,cap=random_u()%129;
        uint32_t disk[128],in[128],expected[256];uint64_t hashes[256];unsigned n=0,added=0;
        for(unsigned i=0;i<disk_n;i++)disk[i]=random_u()%197;
        for(unsigned i=0;i<in_n;i++)in[i]=random_u()%197;
        SeedHeader hdr={SEED_MAGIC,SEED_VERSION,1,0};SeedSection sec={7,4,disk_n,0};
        memcpy(image,&hdr,sizeof(hdr));memcpy(image+sizeof(hdr),&sec,sizeof(sec));memcpy(image+sizeof(hdr)+sizeof(sec),disk,disk_n*4);
        image_len=sizeof(hdr)+sizeof(sec)+disk_n*4;
        for(unsigned pass=0;pass<2;pass++) {
            uint32_t *values=pass?in:disk;unsigned count=pass?in_n:disk_n;
            for(unsigned i=0;i<count && (!pass || n<cap);i++) {
                uint64_t h=xxh3_64(values+i,4, 0);unsigned j=0;while(j<n && hashes[j]!=h)j++;
                if(j==n){hashes[n]=h;expected[n++]=values[i];added+=pass;}
            }
        }
        unsigned got=pgraph_seed_merge(7,4,in,in_n,cap,&total);
        assert(got==(fail_write?0:added) && total==n && writes==!!added);
        assert(payload_allocations==!!added && payload_bytes==(added?n*4:0));
        assert(!active && maps==unmaps && !duplicates);
        if(added)assert(written_count==n && !memcmp(written,expected,n*4));
    }
    puts("Seed merge: borrowed mapped records, ownership boundary, dedup/caps, malformed input, write failure and unmap-before-write and deferred single payload allocation; 512 randomized growth/collision/cap cases PASS");
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='seed-view-') as tmp:
    d=Path(tmp); c=d/'test.c'; c.write_text(code); exe=Path(os.environ.get('SEED_VIEW_BINARY',str(d/('test.exe' if os.name=='nt' else 'test'))))
    env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
    subprocess.run([cc,'-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-I'+str(r),str(c),'-o',str(exe),*shlex.split(os.environ.get('SEED_VIEW_CFLAGS','')),*compat_glib_flags(r,cc)],check=True,env=env)
    if not os.environ.get('SEED_VIEW_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env,timeout=30)



