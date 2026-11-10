/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define CF_PCREL 1
typedef struct { unsigned cflags, pc, flags; } TranslationBlock;
typedef struct { TranslationBlock *tb; } Entry;
typedef struct { Entry array[8], classes[8]; } CPUJumpCache;
typedef struct { CPUJumpCache *tb_jmp_cache; } CPUState;
static CPUJumpCache caches[2];
static CPUState cpus[2]={{&caches[0]}, {&caches[1]}};
static unsigned flushes;
static bool xemu_cpu_boost;
#define CPU_FOREACH(c) for ((c)=cpus; (c)<cpus+2; (c)++)
#define retro_atomic_load_relaxed_ptr(p) (*(p))
#define retro_atomic_store_relaxed_ptr(p,v) (*(p)=(v))
static unsigned tb_cflags(TranslationBlock *t) { return t->cflags; }
static unsigned tb_jmp_cache_hash_func(unsigned pc) { return pc&7; }
static unsigned tb_jmp_cache_class_hash(unsigned pc,unsigned flags) { return (pc^flags)&7; }
static void tcg_flush_jmp_cache(CPUState *c) { (void)c; flushes++; }
/* PRODUCTION */
int main(void)
{
    unsigned cases=0;
    for (unsigned boost=0; boost<2; boost++)
    for (unsigned relative=0; relative<2; relative++)
    for (unsigned batch=0; batch<2; batch++)
    for (unsigned count=0; count<=128; count++) {
        bool owed=false;
        TranslationBlock t={relative?CF_PCREL:0,3,5}, other={0};
        xemu_cpu_boost=boost; flushes=0;
        for (unsigned i=0;i<2;i++) {
            caches[i].array[3].tb=i?&other:&t;
            caches[i].classes[6].tb=i?&other:&t;
        }
        for (unsigned i=0;i<count;i++) tb_jmp_cache_inval_tb(&t,batch?&owed:NULL);
        assert(owed==(bool)(count && relative && batch && !boost));
        assert(flushes==(relative && !boost && !batch?2*count:0));
        if (!relative && count) {
            assert(caches[0].array[3].tb==NULL);
            assert(caches[0].classes[6].tb==(boost?NULL:&t));
            assert(caches[1].array[3].tb==&other && caches[1].classes[6].tb==&other);
        }
        /* Once owed, a later non-PC-relative invalidation cannot clear it. */
        if (owed) { t.cflags=0; tb_jmp_cache_inval_tb(&t,&owed); assert(owed); }
        cases++;
    }
    printf("PASS: %u cache invalidation cases; boost, batching, immediate callers, two CPUs and exact entries\n",cases);
}
