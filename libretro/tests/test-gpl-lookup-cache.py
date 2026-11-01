#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GPL lookup caching versus a first-match scan, including moving arrays."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
types=re.search(r'typedef struct GplLibrary \{.*?\} GplLibrary;',s,re.S).group()+'\n'+re.search(r'typedef struct PipelineLibraryLookup \{.*?\} PipelineLibraryLookup;',h,re.S).group()
parts=[]
for name in ['gpl_init','gpl_finalize','gpl_lookup','gpl_store']:
 m=re.search(r'^static (?:void|VkPipeline) '+name+r'\([^;{]*\)\s*\{.*?^\}',s,re.M|re.S);assert m,name
 parts.append(m.group())
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define VK_NULL_HANDLE 0
#define guint unsigned
typedef uint64_t VkPipeline;
/* TYPES */
typedef struct { unsigned len;GplLibrary *data; } GArray;
typedef struct {GArray *gpl_pre,*gpl_frag;PipelineLibraryLookup gpl_lookup_cache[2][64];int device;} PGRAPHVkState;
static uint64_t probes,allocations,destroyed,cases;
static GArray *g_array_new(bool zero,bool clear,size_t size) {
 assert(!zero && !clear && size==sizeof(GplLibrary));allocations++;
 GArray *a=calloc(1,sizeof(*a));assert(a);return a;
}
static GplLibrary *entry(GArray *a,unsigned i) {assert(i<a->len);probes++;return &a->data[i];}
#define g_array_index(a,t,i) (*entry(a,i))
static void append(GArray *a,const GplLibrary *value) {
 /* Every append changes address: cached raw pointers would dangle. */
 GplLibrary *p=malloc((a->len+1)*sizeof(*p));assert(p);allocations++;
 if(a->len) {memcpy(p,a->data,a->len*sizeof(*p));}
 p[a->len]=*value;
 free(a->data);a->data=p;a->len++;
}
#define g_array_append_val(a,v) append(a,&(v))
static void g_array_free(GArray *a,bool data) {assert(data);free(a->data);free(a);}
static void vkDestroyPipeline(int device,VkPipeline lib,void *allocator) {assert(device==1 && lib && !allocator);destroyed++;}
/* PRODUCTION */
static void check(PGRAPHVkState *r,bool frag,uint64_t hash) {
 GArray *a=frag?r->gpl_frag:r->gpl_pre;VkPipeline want=0;bool want_found=false;
 for(unsigned i=0;i<a->len;i++)if(a->data[i].hash==hash){want=a->data[i].lib;want_found=true;break;}
 bool found=!want_found;uint64_t before=allocations;
 VkPipeline got=gpl_lookup(r,frag,hash,&found);
 assert(got==want && found==want_found && allocations==before);cases++;
}
int main(void) {
 PGRAPHVkState r={.device=1};gpl_init(&r);
 const uint64_t edge[]={0,1,65,UINT64_MAX,0x100000001ull,0x8000000000000000ull};
 for(unsigned frag=0;frag<2;frag++)for(unsigned i=0;i<ARRAY_SIZE(edge);i++) {
  check(&r,frag,edge[i]);gpl_store(&r,frag,edge[i],1+frag*100+i);check(&r,frag,edge[i]);check(&r,frag,edge[i]);
 }
 /* Same-hash append must preserve the original first-match behavior. */
 for(unsigned frag=0;frag<2;frag++) {gpl_store(&r,frag,1,9999);check(&r,frag,1);}
 for(unsigned i=0;i<2048;i++) {gpl_store(&r,false,10000+i,100000+i);check(&r,false,1);}
 for(unsigned i=0;i<50000;i++) {check(&r,i&1,edge[(i*7)%ARRAY_SIZE(edge)]);check(&r,false,(uint64_t)i*7919);}
 /* Warm distinct slots near the end of a large table. */
 for(unsigned i=0;i<32;i++)check(&r,false,12000+i);
 probes=0;
 for(unsigned i=0;i<20000;i++)check(&r,false,12000+i%32);
 assert(probes==20000);printf("GPL warm lookups: 20000 library-entry probes for 20000 hits in a %u-entry table\n",r.gpl_pre->len);
 unsigned old=r.gpl_pre->len+r.gpl_frag->len;gpl_finalize(&r);assert(destroyed==old);
 gpl_init(&r);check(&r,false,1);gpl_store(&r,false,1,777);check(&r,false,1);
 bool found=false;assert(gpl_lookup(&r,false,1,&found)==777 && found);
 gpl_finalize(&r);
 printf("GPL lookup: %llu scan-oracle/miss/zero/collision/duplicate/moving-array/lifecycle cases; no lookup allocations PASS\n",(unsigned long long)cases);
}
'''.replace('/* TYPES */',types).replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='gpl-lookup-') as tmp:
 d=Path(tmp);c=d/'test.c';c.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-sign-compare',*shlex.split(os.environ.get('GPL_LOOKUP_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)

