/*
 * VFIO CPR
 *
 * Copyright (c) 2025 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_CPR_H
#define HW_VFIO_VFIO_CPR_H

#include "migration/misc.h"
#include "system/memory.h"

struct VFIOLegacyContainer;
struct VFIOContainer;
struct VFIOGroup;
struct VFIODevice;
struct VFIOPCIDevice;
struct VFIOIOMMUFDContainer;
struct IOMMUFDBackend;

typedef int (*dma_map_fn)(const struct VFIOContainer *bcontainer,
                          hwaddr iova, uint64_t size, void *vaddr,
                          bool readonly, MemoryRegion *mr);

typedef struct VFIOContainerCPR {
    Error *blocker;
    bool vaddr_unmapped;
    NotifierWithReturn transfer_notifier;
    MemoryListener remap_listener;
} VFIOContainerCPR;

typedef struct VFIODeviceCPR {
    Error *mdev_blocker;
    Error *id_blocker;
    uint32_t hwpt_id;
    uint32_t ioas_id;
} VFIODeviceCPR;

typedef struct VFIOPCICPR {
    NotifierWithReturn transfer_notifier;
} VFIOPCICPR;





extern const VMStateDescription vfio_cpr_pci_vmstate;
extern const VMStateDescription vmstate_cpr_vfio_devices;


#endif /* HW_VFIO_VFIO_CPR_H */
