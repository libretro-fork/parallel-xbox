#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production Vulkan cache export/teardown against a growing-driver oracle."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve(); cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
functions=[]
for name in ['pcache_save_now','finalize_pipeline_cache']:
    m=re.search(r'^static void '+name+r'\(.*?^\}',s,re.M|re.S);assert m,name
    functions.append(m.group())
code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define VK_SUCCESS 0
static unsigned allocations,exports,writes,queries,destroys;
static size_t query_size=100, export_size=100;
static int query_result, export_result;
static bool write_ok=true;
typedef struct {
    int device,vk_pipeline_cache,pipeline_cache;
    void *pipeline_cache_entries;
    size_t pcache_saved_len;
    void *pcache_blob;
    size_t pcache_blob_capacity;
} PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
static void *g_malloc(size_t n) { allocations++;void *p=malloc(n);assert(p);return p; }
static void g_free(void *p) { free(p); }
static const char *xemu_settings_get_base_path(void) { return "fixture"; }
static char *g_strdup_printf(const char *fmt,...) {
    char *p=malloc(128);assert(p);va_list ap;va_start(ap,fmt);vsnprintf(p,128,fmt,ap);va_end(ap);return p;
}
static int vkGetPipelineCacheData(int device,int cache,size_t *size,void *data) {
    assert(device==1 && cache==2);
    if(!data) {queries++;*size=query_size;return query_result;}
    exports++;assert(*size>=query_size);
    if(export_result) return export_result;
    assert(*size>=export_size);memset(data,0x5a,export_size);*size=export_size;return VK_SUCCESS;
}
static bool g_file_set_contents(const char *path,const void *data,size_t len,void *error) {
    assert(!strcmp(path,"fixture/vk_pipeline_cache.bin") && !error && len==export_size);
    const unsigned char *b=data;for(size_t i=0;i<len;i++)assert(b[i]==0x5a);
    writes++;return write_ok;
}
static void lru_flush(int *lru) { (void)lru; }
static void pcache_save_now(PGRAPHVkState *r);
static void pgraph_vk_save_pipeline_cache(PGRAPHState *pg,bool sync) {assert(sync);pcache_save_now(pg->vk_renderer_state);}
static PGRAPHVkState *state;
static void vkDestroyPipelineCache(int device,int cache,void *allocator) {
    assert(device==1 && cache==2 && !allocator && !state->pcache_blob && !state->pcache_blob_capacity);
    destroys++;
}
/* PRODUCTION */
int main(void) {
    PGRAPHVkState r={.device=1,.vk_pipeline_cache=2,.pcache_blob_capacity=65536};
    PGRAPHState pg={&r};state=&r;r.pcache_blob=g_malloc(r.pcache_blob_capacity);
    pcache_save_now(&r);assert(r.pcache_saved_len==100 && allocations==1 && writes==1);
    void *first=r.pcache_blob;
    pcache_save_now(&r);assert(exports==1 && writes==1 && allocations==1);
    query_size=export_size=200;pcache_save_now(&r);assert(r.pcache_blob==first && allocations==1 && r.pcache_saved_len==200);
    query_size=export_size=70000;pcache_save_now(&r);assert(allocations==2 && r.pcache_blob_capacity==131072);
    query_size=100000;export_size=110000;pcache_save_now(&r);assert(allocations==2 && r.pcache_saved_len==110000);
    query_size=export_size=120000;write_ok=false;pcache_save_now(&r);assert(r.pcache_saved_len==110000);
    write_ok=true;pcache_save_now(&r);assert(r.pcache_saved_len==120000 && allocations==2);
    query_size=export_size=140000;export_result=5;unsigned before=writes;
    pcache_save_now(&r);assert(r.pcache_saved_len==120000 && writes==before && allocations==3);
    export_result=0;pcache_save_now(&r);assert(r.pcache_saved_len==140000 && allocations==3);
    query_result=-1;before=exports;pcache_save_now(&r);assert(exports==before && allocations==3);
    query_result=0;query_size=0;pcache_save_now(&r);assert(exports==before && allocations==3);
    query_size=export_size=150000;r.pipeline_cache_entries=malloc(1);
    finalize_pipeline_cache(&pg);assert(destroys==1 && !r.pipeline_cache_entries && allocations==3);
    puts("Pipeline cache export: scratch reuse/growth, concurrent cache growth, incomplete data, failed-write retry, query errors and teardown PASS");
}
'''.replace('/* PRODUCTION */','\n'.join(functions))
with tempfile.TemporaryDirectory(prefix='pcache-export-') as tmp:
    d=Path(tmp);c=d/'test.c';c.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
    env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
    subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('PCACHE_EXPORT_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env,timeout=30)
