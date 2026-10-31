/*
 * An very simplified iova tree implementation based on GTree.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */
#ifndef IOVA_TREE_H
#define IOVA_TREE_H

/*
 * Currently the iova tree will only allow to keep ranges
 * information, and no extra user data is allowed for each element.  A
 * benefit is that we can merge adjacent ranges internally within the
 * tree.  It can save a lot of memory when the ranges are split but
 * mostly continuous.
 *
 * Note that current implementation does not provide any thread
 * protections.  Callers of the iova tree should be responsible
 * for the thread safety issue.
 */

#include "system/memory.h"
#include "exec/hwaddr.h"

#define  IOVA_OK           (0)
#define  IOVA_ERR_INVALID  (-1) /* Invalid parameters */
#define  IOVA_ERR_OVERLAP  (-2) /* IOVA range overlapped */
#define  IOVA_ERR_NOMEM    (-3) /* Cannot allocate */

typedef struct IOVATree IOVATree;
typedef struct DMAMap {
    hwaddr iova;
    hwaddr translated_addr;
    hwaddr size;                /* Inclusive */
    IOMMUAccessFlags perm;
} QEMU_PACKED DMAMap;
typedef gboolean (*iova_tree_iterator)(DMAMap *map);





#endif
