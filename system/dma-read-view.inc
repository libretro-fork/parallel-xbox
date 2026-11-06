/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by dma-helpers.c: immutable storage arrives before guest mapping.
 * Classic read/write DMA keeps its existing request shape and direct-I/O path. */
typedef struct DMAReadViewAIOCB {
    BlockAIOCB common;
    BlockAIOCB *acb;
    BlockBackend *blk;
    QEMUSGList *sg;
    AddressSpace *as;
    const void *data;
    void *owner;
    uint64_t offset, done, total;
    size_t length, position, limit;
    int sg_index;
    dma_addr_t sg_byte;
    AddressSpaceMapClient map_client;
    bool waiting, cancelled, copying, completing;
} DMAReadViewAIOCB;

static void dma_read_view_copy(DMAReadViewAIOCB *r);
static void dma_read_view_cb(void *opaque, int ret);

static void dma_read_view_drop(DMAReadViewAIOCB *r)
{
    void *owner = r->owner;
    r->owner = NULL;
    r->data = NULL;
    if (owner) lr_disk_release_read_view(owner);
}

static void dma_read_view_complete(DMAReadViewAIOCB *r, int ret)
{
    assert(!r->acb && !r->waiting && !r->copying && !r->completing);
    r->completing = true;
    dma_read_view_drop(r);
    if (r->common.cb) r->common.cb(r->common.opaque, ret);
    qemu_aio_unref(&r->common);
}

static bool dma_read_view_submit(DMAReadViewAIOCB *r)
{
    assert(!r->acb && !r->owner && !r->waiting && r->done < r->total);
    r->length = MIN(r->total - r->done, r->limit);
    r->position = 0;
    r->acb = lr_disk_aio_read_view(r->blk, r->offset + r->done, r->length,
                                   &r->data, &r->owner, dma_read_view_cb, r);
    return r->acb != NULL;
}

static void dma_read_view_retry(void *opaque)
{
    DMAReadViewAIOCB *r = opaque;
    assert(r->waiting && !r->acb);
    r->waiting = false;
    dma_read_view_copy(r);
}

static void dma_read_view_copy(DMAReadViewAIOCB *r)
{
    assert(!r->acb && !r->waiting && !r->copying);
    r->copying = true;
    while (r->position < r->length) {
        dma_addr_t n, address, entry_length;
        void *mem;
        if (r->cancelled) goto cancelled;
        while (r->sg_index < r->sg->nsg &&
               r->sg_byte == r->sg->sg[r->sg_index].len) {
            r->sg_index++;
            r->sg_byte = 0;
        }
        if (r->sg_index >= r->sg->nsg) goto invalid;
        entry_length = r->sg->sg[r->sg_index].len;
        if (r->sg_byte > entry_length ||
            r->sg_byte > UINT64_MAX - r->sg->sg[r->sg_index].base) goto invalid;
        address = r->sg->sg[r->sg_index].base + r->sg_byte;
        n = MIN(entry_length - r->sg_byte, r->length - r->position);
        mem = dma_memory_map(r->as, address, &n, DMA_DIRECTION_FROM_DEVICE,
                             MEMTXATTRS_UNSPECIFIED);
        if (r->cancelled) {
            if (mem) dma_memory_unmap(r->as, mem, n,
                                       DMA_DIRECTION_FROM_DEVICE, 0);
            goto cancelled;
        }
        if (!mem) {
            r->copying = false;
            r->waiting = true;
            address_space_register_map_client(r->as, &r->map_client,
                                               dma_read_view_retry, r);
            return;
        }
        assert(n && n <= r->length - r->position && n <= entry_length - r->sg_byte);
        memcpy(mem, (const uint8_t *)r->data + r->position, n);
        dma_memory_unmap(r->as, mem, n, DMA_DIRECTION_FROM_DEVICE, n);
        /* Unmap may notify another owner callback that abandons this read
         * and retires its SG. Never inspect that SG after cancellation. */
        if (r->cancelled) goto cancelled;
        r->sg_byte += n;
        r->position += n;
        r->done += n;
    }
    dma_read_view_drop(r);
    if (r->cancelled) goto cancelled;
    r->copying = false;
    if (r->done == r->total) {
        dma_read_view_complete(r, 0);
    } else if (!dma_read_view_submit(r)) {
        dma_read_view_complete(r, -EIO);
    }
    return;
invalid:
    r->copying = false;
    dma_read_view_complete(r, -EINVAL);
    return;
cancelled:
    r->copying = false;
    dma_read_view_complete(r, -ECANCELED);
}

static void dma_read_view_cb(void *opaque, int ret)
{
    DMAReadViewAIOCB *r = opaque;
    r->acb = NULL;
    if (r->cancelled || ret < 0 || !r->data) {
        dma_read_view_complete(r, r->cancelled ? -ECANCELED : ret < 0 ? ret : -EIO);
        return;
    }
    dma_read_view_copy(r);
}

static void dma_read_view_cancel(BlockAIOCB *acb)
{
    DMAReadViewAIOCB *r = container_of(acb, DMAReadViewAIOCB, common);
    if (r->completing) return;
    r->cancelled = true;
    if (r->copying) return;
    if (r->acb) {
        /* Retained disk reads use fixed owner slots. Retiring one abandons
         * an unfinished provider, or completes ready work, without a wait. */
        blk_aio_cancel(r->acb);
        return;
    }
    if (r->waiting) {
        address_space_unregister_map_client(r->as, &r->map_client);
        r->waiting = false;
    }
    dma_read_view_complete(r, -ECANCELED);
}

static const AIOCBInfo dma_read_view_info = {
    .aiocb_size = sizeof(DMAReadViewAIOCB),
    .cancel_async = dma_read_view_cancel,
};

bool dma_abandon_read_view(BlockAIOCB *acb)
{
    if (!acb || acb->aiocb_info != &dma_read_view_info) return false;
    DMAReadViewAIOCB *r = container_of(acb, DMAReadViewAIOCB, common);
    r->common.cb = NULL;
    if (r->completing) return true;
    dma_read_view_cancel(acb);
    return true;
}

static BlockAIOCB *dma_read_view_start(BlockBackend *blk, QEMUSGList *sg,
                                       uint64_t offset, uint32_t align,
                                       BlockCompletionFunc *cb, void *opaque)
{
    DMAReadViewAIOCB *r;
    size_t limit;
    assert(sg->as->map_client_owner == sthread_get_current_thread_id());
    if (!align || (align & (align - 1)) || sg->size % align || !sg->size ||
        offset > INT64_MAX || sg->size > (uint64_t)INT64_MAX - offset) return NULL;
    limit = lr_disk_read_view_max(blk, offset, sg->size);
    limit -= limit % align;
    if (!limit) return NULL;
    r = blk_aio_get(&dma_read_view_info, NULL, cb, opaque);
    r->acb = NULL;
    r->blk = blk;
    r->sg = sg;
    r->as = sg->as;
    r->data = NULL;
    r->owner = NULL;
    r->offset = offset;
    r->done = 0;
    r->total = sg->size;
    r->length = r->position = 0;
    r->limit = limit;
    r->sg_index = 0;
    r->sg_byte = 0;
    r->map_client = (AddressSpaceMapClient){0};
    r->waiting = r->cancelled = r->copying = r->completing = false;
    if (!dma_read_view_submit(r)) {
        dma_read_view_drop(r);
        qemu_aio_unref(&r->common);
        return NULL;
    }
    return &r->common;
}
