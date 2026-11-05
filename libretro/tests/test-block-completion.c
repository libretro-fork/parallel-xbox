#include "block/libretro-disk-provider.h"
/* Real request/completion/drain code with the fixed-slot owner dispatcher.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "qemu/queue.h"

typedef struct BlockBackend BlockBackend;
typedef struct BlockAIOCB BlockAIOCB;
typedef void BlockCompletionFunc(void *, int);
typedef struct {
    size_t aiocb_size;
    void (*cancel_async)(BlockAIOCB *);
} AIOCBInfo;
struct BlockAIOCB {
    const AIOCBInfo *aiocb_info;
    BlockCompletionFunc *cb;
    void *opaque;
    int refcnt;
};
typedef struct {
    BlockAIOCB common;
    BlockBackend *blk;
    bool dispatch;
    int ret;
    bool busy, completing;
} LRDiskReq;
struct BlockBackend {
    const char *name;
    int in_flight;
    LRDiskReq req[LR_DISK_REQS];
    QTAILQ_ENTRY(BlockBackend) link;
};
static const AIOCBInfo lr_disk_aiocb_info = {sizeof(LRDiskReq), NULL};
static QTAILQ_HEAD(, BlockBackend) lr_disks = QTAILQ_HEAD_INITIALIZER(lr_disks);
static int lr_disk_pending, lr_disk_waiting;
static unsigned kicks, slot_visits, bh_polls;
static void (*bh_work)(void);
static void *first_cpu=(void *)(uintptr_t)1;
static void lr_vcpu_exit(void *cpu){assert(cpu==first_cpu);kicks++;}
static void *qemu_get_aio_context(void) { return NULL; }
static int aio_bh_poll(void *ctx) {
    (void)ctx; bh_polls++;
    if (bh_work) { void (*work)(void) = bh_work; bh_work = NULL; work(); return 1; }
    return 0;
}
static void qemu_aio_ref(BlockAIOCB *a) { a->refcnt++; }
static void qemu_aio_unref(BlockAIOCB *a) { a->refcnt--; }
static const char *blk_name(BlockBackend *b) { return b->name; }
#define error_report(...) fprintf(stderr, __VA_ARGS__)
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))

#define LR_DSP_EXTERNAL_GUARD ((void)0)
#define LR_DSP_FENCE_POLL 1
static void lr_dsp_ownership_fence(unsigned reason){assert(reason==1);}
/* PRODUCTION */

typedef struct {
    BlockBackend *blk;
    BlockAIOCB *acb;
    unsigned id, calls;
    int expected;
} Payload;
static BlockBackend backends[2];
static Payload payload[2 * LR_DISK_REQS + 8];
static const char *mode;
static unsigned count;
static void completion(void *opaque, int ret);

static void submit(unsigned id, unsigned backend, int ret)
{
    Payload *p = &payload[id];
    p->id = id;
    p->blk = &backends[backend];
    p->expected = ret;
    p->acb = lr_disk_submit(p->blk, ret, completion, p);
}

static void completion(void *opaque, int ret)
{
    Payload *p = opaque;
    p->calls++;
    if (p->calls != 1) {
        fprintf(stderr, "duplicate id=%u calls=%u pending=%d in_flight=%d\n",
                p->id, p->calls, lr_disk_pending, p->blk->in_flight);
    }
    assert(p->calls == 1 && ret == p->expected);
    assert(lr_disk_pending >= 0 && p->blk->in_flight > 0);
    LRDiskReq *own = container_of(p->acb, LRDiskReq, common);
    assert(own->busy); /* a callback must not make its slot reusable */
    if (!strcmp(mode,"stress") || !strcmp(mode,"empty-progress")) return;
    if (p->id == 0) {
        if (!strcmp(mode,"poll-deferred") || !strcmp(mode,"poll-cross") || !strcmp(mode,"poll-reuse")) {
            if(!strcmp(mode,"poll-reuse"))blk_aio_cancel(payload[1].acb);
            submit(count++, !strcmp(mode,"poll-cross"), -EIO);
            assert(lr_disk_pending==1 && own->busy);return;
        }
        if(!strcmp(mode,"poll-nested")) {
            assert(lr_disk_poll());assert(!lr_disk_pending && own->busy);return;
        }
        if (!strcmp(mode, "self-cancel")) {
            blk_aio_cancel(p->acb);
        } else if (!strcmp(mode, "global-drain")) {
            bdrv_drain_all();
        } else if (!strcmp(mode, "cancel-other")) {
            blk_aio_cancel(payload[1].acb);
        } else if (!strcmp(mode, "submit-drain") || !strcmp(mode, "slot-hold")) {
            submit(count++, 0, -EIO);
            assert(payload[count - 1].acb != p->acb);
            blk_drain(p->blk);
        } else if (strcmp(mode, "ordinary")) {
            blk_drain(p->blk);
        }
        if (strcmp(mode, "ordinary")) {
            assert(lr_disk_pending == 0 && own->busy);
        }
    } else if (p->id == 1 && !strcmp(mode, "nested-drain")) {
        blk_drain(p->blk);
        assert(lr_disk_pending == 0 && own->busy);
    }
}

static void enqueue_from_bh(void) { submit(0, 0, -EIO); }
static void empty_progress(void)
{
#ifdef DISK_EMPTY_FASTPATH
    const unsigned empty_visits = 0;
#else
    const unsigned empty_visits = 2 * LR_DISK_REQS;
#endif
    assert(!lr_disk_run_pending());
    assert(slot_visits == empty_visits && !bh_polls);
    slot_visits = 0; bh_work = enqueue_from_bh;
    assert(lr_disk_progress());
    assert(slot_visits == empty_visits && bh_polls == 1);
    assert(lr_disk_pending == 1 && !payload[0].calls);
    slot_visits = 0;
    assert(lr_disk_progress());
    assert(slot_visits == 2 * LR_DISK_REQS && bh_polls == 1);
    assert(payload[0].calls == 1 && !lr_disk_pending && !backends[0].in_flight);
    slot_visits = 0;
    assert(!lr_disk_progress());
    assert(slot_visits == empty_visits && bh_polls == 2 && kicks == 1);
    printf("empty block progress: slot visits=%u, BH fallback and deferred enqueue/callback PASS\n",empty_visits);
}

typedef struct Gate {
    LRDiskProvider provider;
    bool ready; int result, checks, abandons, calls, received;
    struct Gate *chain;
} Gate;
static bool provider_ready(LRDiskProvider *opaque, int *ret)
{
    Gate *g=(Gate *)opaque; g->checks++;
    if(!g->ready)return false;
    *ret=g->result;return true;
}
static void provider_abandon(LRDiskProvider *opaque)
{
    Gate *g=(Gate *)opaque;assert(!g->abandons);g->abandons++;
}
static void provider_complete(void *opaque,int ret)
{
    Gate *g=opaque;assert(!g->calls);g->calls++;g->received=ret;
    if(g->chain)lr_disk_submit_deferred(&backends[1],&g->chain->provider,provider_complete,g->chain);
}
static void provider_test(void)
{
    Gate g={.provider={provider_ready,provider_abandon},.result=-EIO};
    BlockAIOCB *a=lr_disk_submit_deferred(&backends[0],&g.provider,provider_complete,&g);
    assert(a && !lr_disk_pending && lr_disk_waiting==1);
    for(unsigned i=0;i<10000;i++)assert(!lr_disk_poll());
    assert(g.checks==10000 && !g.calls && backends[0].in_flight==1);
    blk_drain(&backends[0]);assert(g.checks==10001 && !g.calls);
    bdrv_drain_all();assert(g.checks==10002 && !g.calls);
    Gate ordinary={0};
    lr_disk_submit(&backends[1],0,provider_complete,&ordinary);
    assert(lr_disk_pending==1 && lr_disk_waiting==1);
    assert(lr_disk_poll() && ordinary.calls==1 && !g.calls);
    assert(!lr_disk_pending && lr_disk_waiting==1);
    g.ready=true;assert(lr_disk_poll());
    assert(g.calls==1 && g.received==-EIO && !g.abandons);
    assert(!lr_disk_pending && !lr_disk_waiting && !backends[0].in_flight);
    for(unsigned async=0;async<2;async++){
        g=(Gate){.provider={provider_ready,provider_abandon}};a=lr_disk_submit_deferred(&backends[0],&g.provider,provider_complete,&g);
        if(async)blk_aio_cancel_async(a);else blk_aio_cancel(a);
        assert(g.calls==1 && g.received==-ECANCELED && g.abandons==1 && !g.checks);
        assert(!lr_disk_pending && !lr_disk_waiting && !backends[0].in_flight);
        g.ready=true;assert(!lr_disk_poll());blk_aio_cancel(a);
        assert(g.calls==1 && g.abandons==1);
    }
    Gate next={.provider={provider_ready,provider_abandon}};
    g=(Gate){.provider={provider_ready,provider_abandon},.ready=true,.chain=&next};
    lr_disk_submit_deferred(&backends[0],&g.provider,provider_complete,&g);
    assert(lr_disk_poll() && g.calls==1 && !next.calls && !next.checks);
    assert(!lr_disk_pending && lr_disk_waiting==1 && !lr_disk_poll());
    assert(next.checks==1 && !next.calls);
    next.ready=true;assert(lr_disk_poll() && next.calls==1);
    assert(!lr_disk_pending && !lr_disk_waiting && !backends[1].in_flight);
    puts("block provider: bounded readiness, no frame-busy hint, nonwaiting drain, cancellation and callback snapshots PASS");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    mode = argv[1];
    for (unsigned b = 0; b < 2; b++) {
        BlockBackend *blk = &backends[b];
        blk->name = "completion-test";
        for (unsigned i = 0; i < LR_DISK_REQS; i++) {
            LRDiskReq *r = &blk->req[i];
            r->blk = blk;
            r->common.aiocb_info = &lr_disk_aiocb_info;

        }
        QTAILQ_INSERT_TAIL(&lr_disks, blk, link);
    }
    if(!strcmp(mode,"provider")) {provider_test();return 0;}
    if(!strcmp(mode,"empty-progress")) { empty_progress(); return 0; }
    if(!strcmp(mode,"stress")) {
        bool live[2*LR_DISK_REQS+8]={0};unsigned rng=42,submitted=0,finished=0;
        for(unsigned n=0;n<100000;n++) {
            rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;unsigned op=rng%5,id=(rng>>8)%(2*LR_DISK_REQS+8),b=(rng>>16)&1;
            if(op<2 && !live[id] && backends[b].in_flight<LR_DISK_REQS) {payload[id].calls=0;submit(id,b,(rng&1)?-EIO:0);live[id]=true;submitted++;}
            else if(op==2 && live[id])blk_aio_cancel(payload[id].acb);
            else if(op==3)lr_disk_poll();
            else if(op==4)bdrv_drain_all();
            unsigned pending=0,per_drive[2]={0};
            for(unsigned i=0;i<2*LR_DISK_REQS+8;i++)if(live[i]) {
                if(payload[i].calls){assert(payload[i].calls==1);live[i]=false;finished++;}
                else {pending++;per_drive[payload[i].blk==&backends[1]]++;}
            }
            assert(lr_disk_pending==(int)pending && backends[0].in_flight==(int)per_drive[0] && backends[1].in_flight==(int)per_drive[1]);
        }
        bdrv_drain_all();for(unsigned i=0;i<2*LR_DISK_REQS+8;i++)if(live[i]){assert(payload[i].calls==1);finished++;}
        assert(finished==submitted && kicks==submitted && !lr_disk_pending && !backends[0].in_flight && !backends[1].in_flight);
        printf("block completion stress: 100000 schedules, %u callbacks, cancel/reuse/poll/drain counts balanced PASS\n",finished);return 0;
    }
    count = !strcmp(mode, "slot-hold") ? LR_DISK_REQS - 1 :
            (!strcmp(mode, "cancel-other") || !strcmp(mode,"poll-reuse")) ? 2 :
            (!strcmp(mode, "ordinary") || !strcmp(mode, "drain-others") ||
             !strcmp(mode, "nested-drain") || !strcmp(mode, "global-drain") || !strcmp(mode,"poll-nested")) ? 3 : 1;
    for (unsigned i = 0; i < count; i++) {
        submit(i, !strcmp(mode, "global-drain") && i == 2 ? 1 : 0, i % 2 ? -EIO : 0);
    }
    for(unsigned i=0;i<count;i++)assert(!payload[i].calls);
    if (!strcmp(mode,"ordinary") || !strncmp(mode,"poll-",5)) {
        assert(lr_disk_poll());
        if(!strcmp(mode,"poll-deferred") || !strcmp(mode,"poll-cross") || !strcmp(mode,"poll-reuse")) {
            assert(!payload[count-1].calls && lr_disk_pending==1);
            assert(lr_disk_poll());
        }
    } else {
        LRDiskReq *r = container_of(payload[0].acb, LRDiskReq, common);
        lr_disk_req_done(r);
    }
    assert(kicks==count && !lr_disk_poll());
    for (unsigned i = 0; i < count; i++) {
        assert(payload[i].calls == 1);
    }
    assert(lr_disk_pending == 0);
    for (unsigned b = 0; b < 2; b++) {
        assert(backends[b].in_flight == 0);
        for (unsigned i = 0; i < LR_DISK_REQS; i++) {
            assert(!backends[b].req[i].busy && !backends[b].req[i].completing);
            assert(!backends[b].req[i].dispatch);
        }
    }
    printf("block completion %s: callbacks once, counters balanced, slots retained: PASS\n", mode);
    return 0;
}
