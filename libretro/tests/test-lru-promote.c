/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "qemu/lru.h"
#define CAP 32
typedef struct { LruNode node; unsigned key, id; } Node;
typedef struct { unsigned kind, id, key; } Event;
static Lru *caches[2];
static Node nodes[2][CAP];
static bool blocked[CAP], recording;
static Event events[2][256];
static unsigned event_n[2];
static void event(Lru *lru, Node *node, unsigned kind, unsigned key) {
 if(!recording)return;
 unsigned c=lru==caches[1];assert(event_n[c]<256);
 events[c][event_n[c]++]=(Event){kind,node->id,key};
}
static void init_node(Lru *l,LruNode *n,const void *k) {Node *node=(Node *)n;event(l,node,1,*(const unsigned *)k);node->key=*(const unsigned *)k;}
static bool differs(Lru *l,LruNode *n,const void *k) {event(l,(Node *)n,2,*(const unsigned *)k);return ((Node *)n)->key!=*(const unsigned *)k;}
static bool pre_evict(Lru *l,LruNode *n) {event(l,(Node *)n,3,((Node *)n)->key);return !blocked[((Node *)n)->id%CAP];}
static void post_evict(Lru *l,LruNode *n) {event(l,(Node *)n,4,((Node *)n)->key);}
static LruNode *reference_try_evict(Lru *lru) {
 LruNode *found;
 QTAILQ_FOREACH_REVERSE(found,&lru->global,next_global) {
  if(lru_is_node_in_use(lru,found) && (!lru->pre_node_evict || lru->pre_node_evict(lru,found))) {lru_evict_node(lru,found);return found;}
 }
 return NULL;
}
static LruNode *reference_get_free(Lru *lru) {
 LruNode *found;
 QTAILQ_FOREACH_REVERSE(found,&lru->global,next_global)if(!lru_is_node_in_use(lru,found))return found;
 found=reference_try_evict(lru);assert(found);return found;
}
static void reference_flush(Lru *lru) {
 LruNode *node,*next;
 for(unsigned bin=0;bin<LRU_NUM_BINS;bin++)QTAILQ_FOREACH_SAFE(node,&lru->bins[bin],next_bin,next) {
  if(!lru->pre_node_evict || lru->pre_node_evict(lru,node)) {lru_evict_node(lru,node);QTAILQ_REMOVE(&lru->global,node,next_global);QTAILQ_INSERT_TAIL(&lru->global,node,next_global);}
 }
}
static bool visit_evict;
static void visit_node(Lru *l,LruNode *n,void *opaque) {
 event(l,(Node *)n,5,((Node *)n)->key);
 if(visit_evict && !(((Node *)n)->id%3))lru_evict_node(l,n);
}
static void reference_visit(Lru *lru,LruNodeVisitorFunc visitor,void *opaque) {
 LruNode *node,*next;
 QTAILQ_FOREACH_SAFE(node,&lru->global,next_global,next)if(lru_is_node_in_use(lru,node))visitor(lru,node,opaque);
}
/* Original unlink/reinsert implementation, independent of new head checks. */
static LruNode *reference_lookup(Lru *lru,uint64_t hash,const void *key) {
 unsigned bin=lru_hash_to_bin(lru,hash);LruNode *iter,*found=NULL;
 QTAILQ_FOREACH(iter,&lru->bins[bin],next_bin)if(iter->hash==hash && !lru->compare_nodes(lru,iter,key)){found=iter;break;}
 if(found)QTAILQ_REMOVE(&lru->bins[bin],found,next_bin);
 else {found=reference_get_free(lru);found->hash=hash;if(lru->init_node)lru->init_node(lru,found,key);assert(found->hash==hash);lru->num_used++;lru->num_free--;}
 QTAILQ_REMOVE(&lru->global,found,next_global);QTAILQ_INSERT_HEAD(&lru->global,found,next_global);QTAILQ_INSERT_HEAD(&lru->bins[bin],found,next_bin);return found;
}
static void setup(Lru *lru,Node *ns,unsigned count) {
 lru_init(lru);for(unsigned i=0;i<count;i++){ns[i]=(Node){.id=i};lru_add_free(lru,&ns[i].node);}
 lru->init_node=init_node;lru->compare_nodes=differs;lru->pre_node_evict=pre_evict;lru->post_node_evict=post_evict;
}
static void verify(void) {
 assert(caches[0]->num_used==caches[1]->num_used && caches[0]->num_free==caches[1]->num_free);
 assert(event_n[0]==event_n[1] && !memcmp(events[0],events[1],event_n[0]*sizeof(Event)));
 LruNode *a=QTAILQ_FIRST(&caches[0]->global),*b=QTAILQ_FIRST(&caches[1]->global);unsigned count=0;
 while(a){assert(b && ((Node *)a)->id==((Node *)b)->id);a=QTAILQ_NEXT(a,next_global);b=QTAILQ_NEXT(b,next_global);count++;}assert(!b && count==CAP);
 for(unsigned bin=0;bin<5;bin++) {
  a=QTAILQ_FIRST(&caches[0]->bins[bin]);b=QTAILQ_FIRST(&caches[1]->bins[bin]);
  while(a){assert(b && ((Node *)a)->id==((Node *)b)->id);a=QTAILQ_NEXT(a,next_bin);b=QTAILQ_NEXT(b,next_bin);}assert(!b);
 }
 for(unsigned i=0;i<CAP;i++)assert(nodes[0][i].key==nodes[1][i].key && nodes[0][i].node.hash==nodes[1][i].node.hash && lru_is_node_in_use(caches[0],&nodes[0][i].node)==lru_is_node_in_use(caches[1],&nodes[1][i].node));
}
static void readonly_mru(void) {
 size_t page,bytes;Lru *lru;Node *node;
#ifdef _WIN32
 SYSTEM_INFO info;DWORD old;GetSystemInfo(&info);page=info.dwPageSize;bytes=(sizeof(Lru)+page-1)/page*page;
 lru=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);node=VirtualAlloc(NULL,page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(lru && node);
#else
 page=sysconf(_SC_PAGESIZE);bytes=(sizeof(Lru)+page-1)/page*page;
 lru=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);node=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(lru!=MAP_FAILED && node!=MAP_FAILED);
#endif
 setup(lru,node,1);unsigned key=7;assert(lru_lookup(lru,key,&key)==&node->node);
#ifdef _WIN32
 assert(VirtualProtect(lru,bytes,PAGE_READONLY,&old) && VirtualProtect(node,page,PAGE_READONLY,&old));
#else
 assert(!mprotect(lru,bytes,PROT_READ) && !mprotect(node,page,PROT_READ));
#endif
 for(unsigned i=0;i<10000;i++)assert(lru_lookup(lru,key,&key)==&node->node);
#ifdef _WIN32
 assert(VirtualProtect(lru,bytes,PAGE_READWRITE,&old) && VirtualProtect(node,page,PAGE_READWRITE,&old));
 assert(VirtualFree(lru,0,MEM_RELEASE) && VirtualFree(node,0,MEM_RELEASE));
#else
 assert(!munmap(lru,bytes) && !munmap(node,page));
#endif
}
/* Protect unrelated nodes/metadata to prove these paths do not scan them. */
typedef struct { unsigned calls; bool evict; } GuardVisit;
static void guarded_visitor(Lru *lru,LruNode *node,void *opaque) {
 GuardVisit *v=opaque;assert(lru_is_node_in_use(lru,node));v->calls++;
 if(v->evict)lru_evict_node(lru,node);
}
static void guarded_counts(void) {
 size_t page,bytes;Lru *lru;unsigned char *pool;
#ifdef _WIN32
 SYSTEM_INFO info;DWORD old;GetSystemInfo(&info);page=info.dwPageSize;
#define PROTECT(p,n,mode) assert(VirtualProtect(p,n,mode,&old))
#define NONE PAGE_NOACCESS
#define READ PAGE_READONLY
#define WRITE PAGE_READWRITE
 bytes=(sizeof(Lru)+page-1)/page*page;
 lru=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,WRITE);
 pool=VirtualAlloc(NULL,3*page,MEM_RESERVE|MEM_COMMIT,WRITE);assert(lru && pool);
#else
 page=sysconf(_SC_PAGESIZE);bytes=(sizeof(Lru)+page-1)/page*page;
#define PROTECT(p,n,mode) assert(!mprotect(p,n,mode))
#define NONE PROT_NONE
#define READ PROT_READ
#define WRITE (PROT_READ|PROT_WRITE)
 lru=mmap(NULL,bytes,WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 pool=mmap(NULL,3*page,WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(lru!=MAP_FAILED && pool!=MAP_FAILED);
#endif
 Node *newest=(Node *)pool,*middle=(Node *)(pool+page),*oldest=(Node *)(pool+2*page);
 memset(blocked,0,sizeof(blocked));setup(lru,newest,1);
 *middle=(Node){.id=1};lru_add_free(lru,&middle->node);
 *oldest=(Node){.id=2};lru_add_free(lru,&oldest->node);
 unsigned key=0;assert(lru_lookup(lru,key,&key)==&oldest->node);
 key=1;assert(lru_lookup(lru,key,&key)==&middle->node);
 key=2;assert(lru_lookup(lru,key,&key)==&newest->node);
 assert(lru->num_used==3 && !lru->num_free);
 PROTECT(newest,page,NONE);
 assert(lru_get_one_free(lru)==&oldest->node);
 assert(lru->num_used==2 && lru->num_free==1);
 assert(lru_get_one_free(lru)==&oldest->node);
 PROTECT(newest,page,WRITE);lru_flush(lru);
 assert(!lru->num_used && lru->num_free==3);
 /* One active head with an inaccessible free-node suffix. */
 key=3;assert(lru_lookup(lru,key,&key)==&newest->node);
 PROTECT(pool+page,2*page,NONE);
 GuardVisit visit={0};lru_visit_active(lru,guarded_visitor,&visit);assert(visit.calls==1);
 visit=(GuardVisit){.evict=true};lru_visit_active(lru,guarded_visitor,&visit);assert(visit.calls==1);
 PROTECT(pool+page,2*page,WRITE);assert(!lru->num_used && lru->num_free==3);
 /* Last-active flush must not scan inaccessible, empty later bin pages. */
 key=4;lru_lookup(lru,key,&key);assert(lru->num_used==1);
 PROTECT(lru,bytes,NONE);PROTECT(lru,page,WRITE);
 PROTECT((unsigned char *)lru+offsetof(Lru,num_used)/page*page,page,WRITE);
 lru_flush(lru);assert(!lru->num_used && lru->num_free==3);
 PROTECT(lru,bytes,WRITE);
 PROTECT(lru,bytes,NONE);PROTECT(pool,3*page,NONE);
 PROTECT((unsigned char *)lru+offsetof(Lru,num_used)/page*page,page,READ);
 assert(!lru_try_evict_one(lru));lru_flush(lru);lru_visit_active(lru,visit_node,NULL);
#ifdef _WIN32
 assert(VirtualFree(lru,0,MEM_RELEASE) && VirtualFree(pool,0,MEM_RELEASE));
#else
 assert(!munmap(lru,bytes) && !munmap(pool,3*page));
#endif
#undef PROTECT
#undef NONE
#undef READ
#undef WRITE
}
static unsigned rng=42;
static unsigned random_u(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
#ifdef _WIN32
static double now(void) {LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return t.QuadPart*(1e9/f.QuadPart);}
#else
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
static volatile uintptr_t sink;
static __attribute__((noinline)) LruNode *lookup_call(Lru *l,uint64_t hash,unsigned *key) {return lru_lookup(l,hash,key);}
int main(void) {
 for(unsigned c=0;c<2;c++){caches[c]=calloc(1,sizeof(Lru));assert(caches[c]);setup(caches[c],nodes[c],CAP);}
#ifndef BENCH
 recording=true;
 for(unsigned i=0;i<30000;i++) {
  unsigned r=random_u(),key=r%97,op=r%32;event_n[0]=event_n[1]=0;
  for(unsigned n=0;n<CAP;n++)blocked[n]=((n+i)%7)==0;
  if(op<26){LruNode *a=lru_lookup(caches[0],key%5,&key),*b=reference_lookup(caches[1],key%5,&key);assert(((Node *)a)->id==((Node *)b)->id);}
  else if(op==26){lru_flush(caches[0]);reference_flush(caches[1]);}
  else if(op<29){LruNode *a=lru_try_evict_one(caches[0]),*b=reference_try_evict(caches[1]);assert((!a && !b)||(a && b && ((Node *)a)->id==((Node *)b)->id));}
  else if(op==31){visit_evict=!!(r&32);lru_visit_active(caches[0],visit_node,NULL);reference_visit(caches[1],visit_node,NULL);}
  else {lru_evict_node(caches[0],&nodes[0][key%CAP].node);lru_evict_node(caches[1],&nodes[1][key%CAP].node);}
  verify();
 }
 recording=false;
#ifndef LRU_LOOKUP_BASELINE
 readonly_mru();
 guarded_counts();
#endif
 printf("LRU promotions: 30000 order/callback/collision/eviction cases; read-only MRU metadata, guarded count gates, free-tail visits and last-active flush PASS\n");
#else
 memset(blocked,0,sizeof(blocked));
 for(unsigned mode=0;mode<9;mode++) {
  if(mode>=5) {
   unsigned count=mode<7?1:mode==7?512:16384,loops=mode<7?4096:mode==7?512:256;
   Node *pool=calloc(count,sizeof(*pool));assert(pool);setup(caches[0],pool,count);
   double start=now();uintptr_t sum=0;
   for(unsigned n=0;n<loops;n++) {
    for(unsigned key=0;key<count;key++)sum+=(uintptr_t)lookup_call(caches[0],mode==5?0:mode==6?LRU_NUM_BINS-1:(uint64_t)key*619,&key);
    lru_flush(caches[0]);
   }
   sink=sum;assert(!caches[0]->num_used && caches[0]->num_free==(int)count);
   printf("flush_mode=%u ns=%.3f\n",mode,(now()-start)/loops);free(pool);continue;
  }
  if(mode==4) {
   const unsigned count=50*1024,loops=1000;Node *large=calloc(count,sizeof(Node));assert(large);setup(caches[0],large,count);
   for(unsigned key=0;key<count;key++)lookup_call(caches[0],key,&key);
   uintptr_t sum=0;double start=now();
   for(unsigned i=0;i<loops;i++){unsigned key=count+i;sum+=(uintptr_t)lookup_call(caches[0],key,&key);}
   sink=sum;printf("mode=%u ns=%.3f\n",mode,(now()-start)/loops);free(large);continue;
  }
  setup(caches[0],nodes[0],CAP);
  for(unsigned i=0;i<16;i++){unsigned key=i;lookup_call(caches[0],mode==2?(uint64_t)key*LRU_NUM_BINS+1:key,&key);}
  const unsigned loops=2000000;uintptr_t sum=0;double start=now();
  for(unsigned i=0;i<loops;i++){unsigned key=mode==0?15:mode==3?i+100:i%16;uint64_t hash=mode==2?(uint64_t)key*LRU_NUM_BINS+1:key;sum+=(uintptr_t)lookup_call(caches[0],hash,&key);}
  sink=sum;printf("mode=%u ns=%.3f\n",mode,(now()-start)/loops);
 }
#endif
 for(unsigned c=0;c<2;c++)free(caches[c]);
}
