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
static int lr_disk_pending;
static unsigned kicks;
static void *first_cpu=(void *)(uintptr_t)1;
static void lr_vcpu_exit(void *cpu){assert(cpu==first_cpu);kicks++;}
static void *qemu_get_aio_context(void) { return NULL; }
static int aio_bh_poll(void *ctx) { (void)ctx; return 0; }
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
    if (!strcmp(mode,"stress")) return;
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
