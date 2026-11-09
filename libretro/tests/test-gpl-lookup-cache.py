#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GPL lookup caching versus a first-match scan, including moving arrays."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',source,re.S).group() for n,source in [('RenderPassState',h),('GplPreKey',s),('GplFragKey',s),('GplLibrary',s),('PipelineLibraryLookup',h)])
parts=[]
for name in ['gpl_key_size','gpl_entry','gpl_init','gpl_finalize','gpl_lookup','gpl_store','library_land']:
 m=re.search(r'^static (?:void|size_t|GplLibrary \*|VkPipeline) *'+name+r'\([^;{]*\)\s*\{.*?^\}',s,re.M|re.S);assert m,name
 parts.append(m.group())
code=r'''
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

#define guint unsigned
#define NV2A_VERTEXSHADER_ATTRIBUTES 16

/* TYPES */
typedef struct { unsigned len;char *data;size_t size; } GArray;
typedef struct {GArray *gpl_pre,*gpl_frag;PipelineLibraryLookup gpl_lookup_cache[2][64];VkDevice device;bool dyn_vertex_input;} PGRAPHVkState;
typedef struct {bool frag;uint64_t hash;union {GplPreKey pre_key;GplFragKey frag_key;};VkPipeline result;VkPipelineLayout layout;} LibJob;
static uint64_t probes,allocations,destroyed,cases,layouts;
static GArray *g_array_new(bool zero,bool clear,size_t size) {
 assert(!zero && !clear);allocations++;GArray *a=calloc(1,sizeof(*a));assert(a);a->size=size;return a;
}
static size_t g_array_get_element_size(GArray *a) {probes++;return a->size;}
static void g_array_set_size(GArray *a,unsigned n) {
 assert(n==a->len+1);char *p=malloc(n*a->size);assert(p);allocations++;
 if(a->len)memcpy(p,a->data,a->len*a->size);
 free(a->data);a->data=p;a->len=n;
}
static void g_array_free(GArray *a,bool data) {assert(data);free(a->data);free(a);}
static void vkDestroyPipeline(VkDevice device,VkPipeline lib,void *allocator) {assert(device==(VkDevice)(uintptr_t)1 && lib && !allocator);destroyed++;}
static void vkDestroyPipelineLayout(VkDevice device,VkPipelineLayout layout,void *allocator) {assert(device && layout && !allocator);layouts++;}
/* PRODUCTION */
typedef union {GplPreKey pre;GplFragKey frag;} Key;
static void check(PGRAPHVkState *r,bool frag,uint64_t hash,const Key *key) {
 GArray *a=frag?r->gpl_frag:r->gpl_pre;VkPipeline want=0;bool want_found=false;
 size_t size=frag?sizeof(GplFragKey):r->dyn_vertex_input?offsetof(GplPreKey,bind):sizeof(GplPreKey);
 for(unsigned i=0;i<a->len;i++) {GplLibrary *e=(GplLibrary *)(a->data+i*a->size);if(e->hash==hash && !memcmp(e->key,key,size)){want=e->lib;want_found=true;break;}}
 bool found=!want_found;uint64_t before=allocations;
 VkPipeline got=gpl_lookup(r,frag,hash,key,&found);
 assert(got==want && found==want_found && allocations==before);cases++;
}
int main(void) {
 for(unsigned dynamic=0;dynamic<2;dynamic++) {
 PGRAPHVkState r={.device=(VkDevice)(uintptr_t)1,.dyn_vertex_input=dynamic};gpl_init(&r);
 assert(g_array_get_element_size(r.gpl_pre)==r.gpl_pre->size);
 printf("GPL entry bytes dynamic=%u pre=%zu fragment=%zu\n",dynamic,r.gpl_pre->size,r.gpl_frag->size);
 for(unsigned frag=0;frag<2;frag++) {
 size_t size=frag?sizeof(GplFragKey):dynamic?offsetof(GplPreKey,bind):sizeof(GplPreKey);
 /* Force every digest to zero. One differing byte exercises every retained
  * field, including static vertex input with identical module identities. */
 for(unsigned i=0;i<size;i++) {
  Key key={0};((unsigned char *)&key)[i]=1;
  check(&r,frag,0,&key);gpl_store(&r,frag,0,&key,(VkPipeline)(uintptr_t)(i+1));
  Key snapshot=key;memset(&key,0xff,sizeof(key));check(&r,frag,0,&snapshot);key=snapshot;
  check(&r,frag,0,&key);bool found=false;assert(gpl_lookup(&r,frag,0,&key,&found)==(VkPipeline)(uintptr_t)(i+1) && found);Key other=key;((unsigned char *)&other)[i]=2;check(&r,frag,0,&other);
 }
 for(unsigned i=0;i<10000;i++){Key key={0};((unsigned char *)&key)[(i*7)%size]=1;check(&r,frag,0,&key);}
 }
 for(unsigned frag=0;frag<2;frag++) {
  LibJob job={.frag=frag,.hash=0,.result=(VkPipeline)(uintptr_t)9000,.layout=(VkPipelineLayout)(uintptr_t)1};
  GArray *table=frag?r.gpl_frag:r.gpl_pre;unsigned old=table->len;uint64_t drops=destroyed,old_layouts=layouts;
  library_land(&r,&job);assert(table->len==old+1 && destroyed==drops && layouts==old_layouts+1);
  job.result=(VkPipeline)(uintptr_t)9001;library_land(&r,&job);assert(table->len==old+1 && destroyed==drops+1);
  memset(frag?(void *)&job.frag_key:(void *)&job.pre_key,2,frag?sizeof(job.frag_key):sizeof(job.pre_key));
  job.result=(VkPipeline)(uintptr_t)9002;library_land(&r,&job);assert(table->len==old+2 && destroyed==drops+1);
  Key saved={0};memcpy(&saved,frag?(void *)&job.frag_key:(void *)&job.pre_key,frag?sizeof(job.frag_key):sizeof(job.pre_key));
  memset(&job,0xff,sizeof(job));check(&r,frag,0,&saved);
 }
 Key warm={0};for(unsigned i=0;i<32;i++)gpl_store(&r,false,12000+i,&warm,(VkPipeline)(uintptr_t)(10000+i));
 for(unsigned i=0;i<32;i++)check(&r,false,12000+i,&warm);
 probes=0;for(unsigned i=0;i<20000;i++)check(&r,false,12000+i%32,&warm);assert(probes==20000);
 unsigned old=r.gpl_pre->len+r.gpl_frag->len;uint64_t before=destroyed;gpl_finalize(&r);assert(destroyed-before==old && !r.gpl_pre && !r.gpl_frag);
 gpl_init(&r);check(&r,false,0,&warm);gpl_finalize(&r);
 }
 printf("GPL exact lookup: %llu forced-collision/poisoned-lend/moving-array/warm/lifecycle cases PASS\n",(unsigned long long)cases);
}
'''.replace('/* TYPES */',types).replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='gpl-lookup-') as tmp:
 d=Path(tmp);c=d/'test.c';c.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-sign-compare','-I'+str(r/'deps/vulkan-headers'),*shlex.split(os.environ.get('GPL_LOOKUP_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
 controls=[('direct-index collision',code.replace(' && !memcmp(e->key, key, key_size)','',1)),('scan collision',code.replace('if (e->hash == hash && !memcmp(e->key, key, key_size)) {\n            cached->hash','if (e->hash == hash) {\n            cached->hash',1)),('missing owned metadata',code.replace('memcpy(entry->key, key, gpl_key_size(r, frag));','memset(entry->key, 0, gpl_key_size(r, frag)); (void)key;',1)),('static vertex tail omitted',code.replace('r->dyn_vertex_input ?','(r->dyn_vertex_input || true) ?',1))]
 for name,mutant in controls:
  assert mutant!=code,name;c.write_text(mutant);subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-sign-compare','-I'+str(r/'deps/vulkan-headers'),str(c),'-o',str(exe)],check=True,env=env)
  bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30);assert bad.returncode==1 and b'FAIL ' in bad.stderr,name
  print('Required negative control rejected:',name)

