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
#ifndef BLOCK_GLOBAL_STATE_H
#define BLOCK_GLOBAL_STATE_H

#include "block/block-common.h"
#include "qemu/transactions.h"

/*
 * Global state (GS) API. These functions run under the BQL.
 *
 * If a function modifies the graph, it also uses the graph lock to be sure it
 * has unique access. The graph lock is needed together with BQL because of the
 * thread-safe I/O API that concurrently runs and accesses the graph without
 * the BQL.
 *
 * It is important to note that not all of these functions are
 * necessarily limited to running under the BQL, but they would
 * require additional auditing and many small thread-safety changes
 * to move them into the I/O API. Often it's not worth doing that
 * work since the APIs are only used with the BQL held at the
 * moment, so they have been placed in the GS API (for now).
 *
 * These functions can call any function from this and other categories
 * (I/O, "I/O or GS", Common), but must be invoked only by other GS APIs.
 *
 * All functions in this header must use the macro
 * GLOBAL_STATE_CODE();
 * to catch when they are accidentally called without the BQL.
 */

BlockDriver *bdrv_find_format(const char *format_name);

int GRAPH_UNLOCKED
bdrv_co_create(BlockDriver *drv, const char *filename, QemuOpts *opts,
               Error **errp);

int GRAPH_UNLOCKED
bdrv_co_create_file(const char *filename, QemuOpts *opts,
                    bool allow_protocol_prefix, Error **errp);


int GRAPH_WRLOCK
bdrv_replace_node(BlockDriverState *from, BlockDriverState *to, Error **errp);

int GRAPH_UNLOCKED
bdrv_replace_child_bs(BdrvChild *child, BlockDriverState *new_bs, Error **errp);
BlockDriverState * GRAPH_UNLOCKED
bdrv_insert_node(BlockDriverState *bs, QDict *node_options, int flags,
                 Error **errp);

BdrvChild * GRAPH_UNLOCKED
bdrv_open_child(const char *filename, QDict *options, const char *bdref_key,
                BlockDriverState *parent, const BdrvChildClass *child_class,
                BdrvChildRole child_role, bool allow_none, Error **errp);

BdrvChild *
bdrv_co_open_child(const char *filename, QDict *options, const char *bdref_key,
                BlockDriverState *parent, const BdrvChildClass *child_class,
                BdrvChildRole child_role, bool allow_none, Error **errp);

int GRAPH_UNLOCKED
bdrv_open_file_child(const char *filename, QDict *options,
                     const char *bdref_key, BlockDriverState *parent,
                     Error **errp);

BlockDriverState * bdrv_open_blockdev_ref(BlockdevRef *ref, Error **errp);

BlockDriverState *
bdrv_co_open_blockdev_ref(BlockdevRef *ref, Error **errp);

int GRAPH_WRLOCK
bdrv_set_backing_hd(BlockDriverState *bs, BlockDriverState *backing_hd,
                    Error **errp);

BlockDriverState * bdrv_open(const char *filename, const char *reference, QDict *options,
          int flags, Error **errp);

BlockDriverState *
bdrv_co_open(const char *filename, const char *reference,
             QDict *options, int flags, Error **errp);

BlockReopenQueue * GRAPH_UNLOCKED
bdrv_reopen_queue(BlockReopenQueue *bs_queue, BlockDriverState *bs,
                  QDict *options, bool keep_old_opts);
int GRAPH_UNLOCKED
bdrv_reopen_multiple(BlockReopenQueue *bs_queue, Error **errp);
void GRAPH_RDLOCK bdrv_refresh_filename(BlockDriverState *bs);

void GRAPH_RDLOCK
bdrv_refresh_limits(BlockDriverState *bs, Transaction *tran, Error **errp);

int GRAPH_RDLOCK bdrv_make_empty(BdrvChild *c, Error **errp);

int GRAPH_UNLOCKED
bdrv_drop_intermediate(BlockDriverState *top, BlockDriverState *base,
                       const char *backing_file_str,
                       bool backing_mask_protocol);

BlockDriverState * GRAPH_RDLOCK
bdrv_find_overlay(BlockDriverState *active, BlockDriverState *bs);

int GRAPH_RDLOCK
bdrv_freeze_backing_chain(BlockDriverState *bs, BlockDriverState *base,
                          Error **errp);
void GRAPH_RDLOCK
bdrv_unfreeze_backing_chain(BlockDriverState *bs, BlockDriverState *base);

/*
 * The units of offset and total_work_size may be chosen arbitrarily by the
 * block driver; total_work_size may change during the course of the amendment
 * operation
 */
typedef void BlockDriverAmendStatusCB(BlockDriverState *bs, int64_t offset,
                                      int64_t total_work_size, void *opaque);
int GRAPH_RDLOCK
bdrv_amend_options(BlockDriverState *bs_new, QemuOpts *opts,
                   BlockDriverAmendStatusCB *status_cb, void *cb_opaque,
                   bool force, Error **errp);

/* check if a named node can be replaced when doing drive-mirror */
BlockDriverState * GRAPH_RDLOCK
check_to_replace_node(BlockDriverState *parent_bs, const char *node_name,
                      Error **errp);


int GRAPH_RDLOCK
bdrv_activate(BlockDriverState *bs, Error **errp);

int
bdrv_co_activate(BlockDriverState *bs, Error **errp);

int GRAPH_RDLOCK
bdrv_inactivate(BlockDriverState *bs, Error **errp);

void bdrv_activate_all(Error **errp);

int bdrv_flush_all(void);
void GRAPH_UNLOCKED bdrv_close_all(void);
void GRAPH_UNLOCKED bdrv_drain_all_begin(void);
void bdrv_drain_all_end(void);
void GRAPH_UNLOCKED bdrv_drain_all(void);

int GRAPH_RDLOCK bdrv_has_zero_init(BlockDriverState *bs);
BlockDriverState *bdrv_lookup_bs(const char *device,
                                 const char *node_name,
                                 Error **errp);
bool GRAPH_RDLOCK
bdrv_chain_contains(BlockDriverState *top, BlockDriverState *base);

BlockDriverState *bdrv_next_all_states(BlockDriverState *bs);

typedef struct BdrvNextIterator {
    enum {
        BDRV_NEXT_BACKEND_ROOTS,
        BDRV_NEXT_MONITOR_OWNED,
    } phase;
    BlockBackend *blk;
    BlockDriverState *bs;
} BdrvNextIterator;



char * GRAPH_RDLOCK
bdrv_get_full_backing_filename(BlockDriverState *bs, Error **errp);

char * GRAPH_RDLOCK bdrv_dirname(BlockDriverState *bs, Error **errp);


void GRAPH_WRLOCK
bdrv_unref_child(BlockDriverState *parent, BdrvChild *child);

void
bdrv_co_unref_child(BlockDriverState *parent, BdrvChild *child);

BdrvChild * GRAPH_WRLOCK
bdrv_attach_child(BlockDriverState *parent_bs,
                  BlockDriverState *child_bs,
                  const char *child_name,
                  const BdrvChildClass *child_class,
                  BdrvChildRole child_role,
                  Error **errp);

bool GRAPH_RDLOCK
bdrv_op_is_blocked(BlockDriverState *bs, BlockOpType op, Error **errp);


int bdrv_debug_breakpoint(BlockDriverState *bs, const char *event,
                           const char *tag);
int bdrv_debug_remove_breakpoint(BlockDriverState *bs, const char *tag);
int bdrv_debug_resume(BlockDriverState *bs, const char *tag);
bool bdrv_debug_is_suspended(BlockDriverState *bs, const char *tag);

bool GRAPH_RDLOCK
bdrv_child_change_aio_context(BdrvChild *c, AioContext *ctx,
                              GHashTable *visited, Transaction *tran,
                              Error **errp);
int GRAPH_UNLOCKED
bdrv_try_change_aio_context(BlockDriverState *bs, AioContext *ctx,
                            BdrvChild *ignore_child, Error **errp);
int GRAPH_RDLOCK
bdrv_try_change_aio_context_locked(BlockDriverState *bs, AioContext *ctx,
                                   BdrvChild *ignore_child, Error **errp);

int GRAPH_RDLOCK bdrv_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz);
int bdrv_probe_geometry(BlockDriverState *bs, HDGeometry *geo);

void GRAPH_WRLOCK
bdrv_add_child(BlockDriverState *parent, BlockDriverState *child, Error **errp);

void GRAPH_WRLOCK
bdrv_del_child(BlockDriverState *parent, BdrvChild *child, Error **errp);

/**
 *
 * bdrv_register_buf/bdrv_unregister_buf:
 *
 * Register/unregister a buffer for I/O. For example, VFIO drivers are
 * interested to know the memory areas that would later be used for I/O, so
 * that they can prepare IOMMU mapping etc., to get better performance.
 *
 * Buffers must not overlap and they must be unregistered with the same <host,
 * size> values that they were registered with.
 *
 * Returns: true on success, false on failure
 */
bool bdrv_register_buf(BlockDriverState *bs, void *host, size_t size,
                       Error **errp);
void bdrv_unregister_buf(BlockDriverState *bs, void *host, size_t size);

void bdrv_cancel_in_flight(BlockDriverState *bs);

#endif /* BLOCK_GLOBAL_STATE_H */
