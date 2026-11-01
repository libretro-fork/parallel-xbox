/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
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
static bool pre_evict(Lru *l,LruNode *n) {event(l,(Node *)n,3,((Node *)n)->key);return !blocked[((Node *)n)->id];}
static void post_evict(Lru *l,LruNode *n) {event(l,(Node *)n,4,((Node *)n)->key);}
/* Original unlink/reinsert implementation, independent of new head checks. */
static LruNode *reference_lookup(Lru *lru,uint64_t hash,const void *key) {
 unsigned bin=lru_hash_to_bin(lru,hash);LruNode *iter,*found=NULL;
 QTAILQ_FOREACH(iter,&lru->bins[bin],next_bin)if(iter->hash==hash && !lru->compare_nodes(lru,iter,key)){found=iter;break;}
 if(found)QTAILQ_REMOVE(&lru->bins[bin],found,next_bin);
 else {found=lru_get_one_free(lru);found->hash=hash;if(lru->init_node)lru->init_node(lru,found,key);assert(found->hash==hash);lru->num_used++;lru->num_free--;}
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
  else if(op==26){lru_flush(caches[0]);lru_flush(caches[1]);}
  else if(op<29){LruNode *a=lru_try_evict_one(caches[0]),*b=lru_try_evict_one(caches[1]);assert((!a && !b)||(a && b && ((Node *)a)->id==((Node *)b)->id));}
  else {lru_evict_node(caches[0],&nodes[0][key%CAP].node);lru_evict_node(caches[1],&nodes[1][key%CAP].node);}
  verify();
 }
 recording=false;
#ifndef LRU_LOOKUP_BASELINE
 readonly_mru();
#endif
 printf("LRU promotions: 30000 order/callback/collision/eviction cases; read-only MRU metadata PASS\n");
#else
 memset(blocked,0,sizeof(blocked));
 for(unsigned mode=0;mode<4;mode++) {
  setup(caches[0],nodes[0],CAP);
  for(unsigned i=0;i<16;i++){unsigned key=i;lookup_call(caches[0],mode==2?(uint64_t)key*LRU_NUM_BINS+1:key,&key);}
  const unsigned loops=2000000;uintptr_t sum=0;double start=now();
  for(unsigned i=0;i<loops;i++){unsigned key=mode==0?15:mode==3?i+100:i%16;uint64_t hash=mode==2?(uint64_t)key*LRU_NUM_BINS+1:key;sum+=(uintptr_t)lookup_call(caches[0],hash,&key);}
  sink=sum;printf("mode=%u ns=%.3f\n",mode,(now()-start)/loops);
 }
#endif
 for(unsigned c=0;c<2;c++)free(caches[c]);
}
