/*
 * QEMU System Emulator block driver
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef BLOCK_INT_IO_H
#define BLOCK_INT_IO_H

#include "block/block_int-common.h"
#include "qemu/hbitmap.h"
#include "qemu/main-loop.h"

/*
 * I/O API functions. These functions are thread-safe.
 *
 * See include/block/block-io.h for more information about
 * the I/O API.
 */

int coroutine_fn GRAPH_RDLOCK bdrv_co_preadv_snapshot(BdrvChild *child,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov, size_t qiov_offset);
int coroutine_fn GRAPH_RDLOCK bdrv_co_snapshot_block_status(
    BlockDriverState *bs, unsigned int mode, int64_t offset,
    int64_t bytes, int64_t *pnum, int64_t *map, BlockDriverState **file);
int coroutine_fn GRAPH_RDLOCK bdrv_co_pdiscard_snapshot(BlockDriverState *bs,
    int64_t offset, int64_t bytes);


int coroutine_fn GRAPH_RDLOCK bdrv_co_preadv(BdrvChild *child,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags);
int coroutine_fn GRAPH_RDLOCK bdrv_co_preadv_part(BdrvChild *child,
    int64_t offset, int64_t bytes,
    QEMUIOVector *qiov, size_t qiov_offset, BdrvRequestFlags flags);
int coroutine_fn GRAPH_RDLOCK bdrv_co_pwritev(BdrvChild *child,
    int64_t offset, int64_t bytes, QEMUIOVector *qiov,
    BdrvRequestFlags flags);
int coroutine_fn GRAPH_RDLOCK bdrv_co_pwritev_part(BdrvChild *child,
    int64_t offset, int64_t bytes,
    QEMUIOVector *qiov, size_t qiov_offset, BdrvRequestFlags flags);

static inline int coroutine_fn GRAPH_RDLOCK bdrv_co_pread(BdrvChild *child,
    int64_t offset, int64_t bytes, void *buf, BdrvRequestFlags flags)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, bytes);
    IO_CODE();
    assert_bdrv_graph_readable();

    return bdrv_co_preadv(child, offset, bytes, &qiov, flags);
}

static inline int coroutine_fn GRAPH_RDLOCK bdrv_co_pwrite(BdrvChild *child,
    int64_t offset, int64_t bytes, const void *buf, BdrvRequestFlags flags)
{
    QEMUIOVector qiov = QEMU_IOVEC_INIT_BUF(qiov, buf, bytes);
    IO_CODE();
    assert_bdrv_graph_readable();

    return bdrv_co_pwritev(child, offset, bytes, &qiov, flags);
}



bool blk_dev_is_tray_open(BlockBackend *blk);



int coroutine_fn GRAPH_RDLOCK
bdrv_co_copy_range_from(BdrvChild *src, int64_t src_offset,
                        BdrvChild *dst, int64_t dst_offset,
                        int64_t bytes, BdrvRequestFlags read_flags,
                        BdrvRequestFlags write_flags);
int coroutine_fn GRAPH_RDLOCK
bdrv_co_copy_range_to(BdrvChild *src, int64_t src_offset,
                      BdrvChild *dst, int64_t dst_offset,
                      int64_t bytes, BdrvRequestFlags read_flags,
                      BdrvRequestFlags write_flags);

int coroutine_fn GRAPH_RDLOCK
bdrv_co_refresh_total_sectors(BlockDriverState *bs, int64_t hint);

int co_wrapper_mixed_bdrv_rdlock
bdrv_refresh_total_sectors(BlockDriverState *bs, int64_t hint);

BdrvChild * GRAPH_RDLOCK bdrv_cow_child(BlockDriverState *bs);
BdrvChild * GRAPH_RDLOCK bdrv_filter_child(BlockDriverState *bs);
BdrvChild * GRAPH_RDLOCK bdrv_filter_or_cow_child(BlockDriverState *bs);
BdrvChild * GRAPH_RDLOCK bdrv_primary_child(BlockDriverState *bs);

static inline BlockDriverState * GRAPH_RDLOCK
bdrv_cow_bs(BlockDriverState *bs)
{
    IO_CODE();
    return child_bs(bdrv_cow_child(bs));
}

static inline BlockDriverState * GRAPH_RDLOCK
bdrv_filter_bs(BlockDriverState *bs)
{
    IO_CODE();
    return child_bs(bdrv_filter_child(bs));
}

static inline BlockDriverState * GRAPH_RDLOCK
bdrv_filter_or_cow_bs(BlockDriverState *bs)
{
    IO_CODE();
    return child_bs(bdrv_filter_or_cow_child(bs));
}

static inline BlockDriverState * GRAPH_RDLOCK
bdrv_primary_bs(BlockDriverState *bs)
{
    IO_CODE();
    return child_bs(bdrv_primary_child(bs));
}

/**
 * Check whether the given offset is in the cached block-status data
 * region.
 *
 * If it is, and @pnum is not NULL, *pnum is set to
 * `bsc.data_end - offset`, i.e. how many bytes, starting from
 * @offset, are data (according to the cache).
 * Otherwise, *pnum is not touched.
 */

/**
 * If [offset, offset + bytes) overlaps with the currently cached
 * block-status region, invalidate the cache.
 *
 * (To be used by I/O paths that cause data regions to be zero or
 * holes.)
 */

/**
 * Mark the range [offset, offset + bytes) as a data region.
 */

/*
 * Notify all parents that the size of the child changed.
 */
void coroutine_fn GRAPH_RDLOCK
bdrv_co_parent_cb_resize(BlockDriverState *bs);

#endif /* BLOCK_INT_IO_H */
