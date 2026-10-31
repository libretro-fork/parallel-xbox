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

#ifndef BLOCK_INT_GLOBAL_STATE_H
#define BLOCK_INT_GLOBAL_STATE_H

#include "block/blockjob.h"
#include "block/block_int-common.h"
#include "qemu/hbitmap.h"
#include "qemu/main-loop.h"

/*
 * Global state (GS) API. These functions run under the BQL.
 *
 * See include/block/block-global-state.h for more information about
 * the GS API.
 */


BdrvChild * GRAPH_WRLOCK
bdrv_root_attach_child(BlockDriverState *child_bs, const char *child_name,
                       const BdrvChildClass *child_class,
                       BdrvChildRole child_role,
                       uint64_t perm, uint64_t shared_perm,
                       void *opaque, Error **errp);

/**
 * Sets a BdrvChild's permissions.  Avoid if the parent is a BDS; use
 * bdrv_child_refresh_perms() instead and make the parent's
 * .bdrv_child_perm() implementation return the correct values.
 */
int GRAPH_RDLOCK
bdrv_child_try_set_perm(BdrvChild *c, uint64_t perm, uint64_t shared,
                        Error **errp);

/**
 * Calls bs->drv->bdrv_child_perm() and updates the child's permission
 * masks with the result.
 * Drivers should invoke this function whenever an event occurs that
 * makes their .bdrv_child_perm() implementation return different
 * values than before, but which will not result in the block layer
 * automatically refreshing the permissions.
 */
int GRAPH_RDLOCK
bdrv_child_refresh_perms(BlockDriverState *bs, BdrvChild *c, Error **errp);

bool GRAPH_RDLOCK bdrv_recurse_can_replace(BlockDriverState *bs,
                                           BlockDriverState *to_replace);

/*
 * Default implementation for BlockDriver.bdrv_child_perm() that can
 * be used by block filters and image formats, as long as they use the
 * child_of_bds child class and set an appropriate BdrvChildRole.
 */



/**
 * Simple implementation of bdrv_co_create_opts for protocol drivers
 * which only support creation via opening a file
 * (usually existing raw storage device)
 */



BlockDriverState * GRAPH_RDLOCK
bdrv_skip_implicit_filters(BlockDriverState *bs);


/**
 * End all quiescent sections started by bdrv_drain_all_begin(). This is
 * needed when deleting a BDS before bdrv_drain_all_end() is called.
 *
 * NOTE: this is an internal helper for bdrv_close() *only*. No one else
 * should call it.
 */

#endif /* BLOCK_INT_GLOBAL_STATE_H */
