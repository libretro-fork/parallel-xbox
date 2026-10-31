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
#endif
#include "qemu/lru.h"

#define NODES 1024
typedef struct { LruNode node; unsigned key; } Node;
typedef struct { Node *nodes; unsigned count; bool seen[NODES],evict; } Visit;
static bool differs(Lru *lru,LruNode *node,const void *key) {
    (void)lru;return ((Node *)node)->key!=*(const unsigned *)key;
}
static void init(Lru *lru,LruNode *node,const void *key) {
    (void)lru;((Node *)node)->key=*(const unsigned *)key;
}
static void collect(Lru *lru,LruNode *node,void *opaque) {
    Visit *v=opaque;ptrdiff_t i=(Node *)node-v->nodes;
    assert(i>=0 && i<NODES && !v->seen[i]);v->seen[i]=true;v->count++;
    if(v->evict) lru_evict_node(lru,node);
}
/* Independent oracle: the old bin enumeration, with no dependence on
 * global-list ordering. Membership and visitation counts must agree. */
static void by_bin(Lru *lru,LruNodeVisitorFunc visitor,void *opaque) {
    for(unsigned bin=0;bin<LRU_NUM_BINS;bin++) {
        LruNode *node,*next;
        QTAILQ_FOREACH_SAFE(node,&lru->bins[bin],next_bin,next) visitor(lru,node,opaque);
    }
}
static volatile uint64_t checksum;
static void touch(Lru *lru,LruNode *node,void *opaque) {
    (void)lru;(void)opaque;checksum+=node->hash;
}
static double now(void) {
#ifdef _WIN32
    LARGE_INTEGER t,frequency;
    assert(QueryPerformanceCounter(&t) && QueryPerformanceFrequency(&frequency));
    return (double)t.QuadPart/frequency.QuadPart;
#else
    struct timespec t;assert(timespec_get(&t,TIME_UTC)==TIME_UTC);
    return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
int main(void) {
    Lru *lru=calloc(1,sizeof(*lru));Node *nodes=calloc(NODES,sizeof(*nodes));
    assert(lru && nodes);lru_init(lru);lru->init_node=init;lru->compare_nodes=differs;
    for(unsigned i=0;i<NODES;i++) lru_add_free(lru,&nodes[i].node);
    unsigned cases=0;
    for(unsigned cycle=0;cycle<8;cycle++) {
        for(unsigned used=0;used<=NODES;used++) {
            Visit reference={.nodes=nodes},actual={.nodes=nodes};
            by_bin(lru,collect,&reference);lru_visit_active(lru,collect,&actual);
            assert(reference.count==used && actual.count==used);
            assert(!memcmp(reference.seen,actual.seen,sizeof(actual.seen)));
            if(used<NODES) {
                unsigned key=used;
                /* Alternate collisions and widely distributed bins. */
                uint64_t hash=cycle&1?(uint64_t)used*LRU_NUM_BINS:(uint64_t)used*619;
                lru_lookup(lru,hash,&key);
            }
            cases++;
        }
        /* A cache hit changes recency but not membership. */
        for(unsigned key=0;key<NODES;key++) {
            uint64_t hash=cycle&1?(uint64_t)key*LRU_NUM_BINS:(uint64_t)key*619;
            assert(((Node *)lru_lookup(lru,hash,&key))->key==key);
        }
        Visit eviction={.nodes=nodes,.evict=true};
        lru_visit_active(lru,collect,&eviction);
        assert(eviction.count==NODES && lru->num_used==0 && lru->num_free==NODES);
        Visit empty={.nodes=nodes};lru_visit_active(lru,collect,&empty);assert(!empty.count);
    }
    for(unsigned key=0;key<NODES;key++) lru_lookup(lru,(uint64_t)key*619,&key);
    double t=now();for(unsigned i=0;i<5000;i++) by_bin(lru,touch,NULL);
    double bin_ms=(now()-t)*1000;
    uint64_t expected=checksum;checksum=0;
    t=now();for(unsigned i=0;i<5000;i++) lru_visit_active(lru,touch,NULL);
    double list_ms=(now()-t)*1000;assert(checksum==expected);
    printf("LRU visit: %u membership cases, collisions, hits and callback eviction passed\n",cases);
    printf("5000 visits of 1024 nodes: bin scan %.3f ms; resident traversal %.3f ms (host-specific)\n",bin_ms,list_ms);
    free(nodes);free(lru);
}
