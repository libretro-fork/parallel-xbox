/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYSTEM_DMA_READ_VIEW_H
#define SYSTEM_DMA_READ_VIEW_H
#include <stdbool.h>
typedef struct BlockAIOCB BlockAIOCB;
/* Owner only. Recognize an owned-source DMA read, suppress its device callback
 * and abandon its provider/map retry without waiting. The controller retires
 * its logical command/SG after true. An active copy observes cancellation at
 * the current map boundary; it never dereferences the retired SG afterward. */
bool dma_abandon_read_view(BlockAIOCB *acb);
#endif
