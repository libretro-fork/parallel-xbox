/*
 * iommufd container backend declaration
 *
 * Copyright (C) 2024 Intel Corporation.
 * Copyright Red Hat, Inc. 2024
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *          Eric Auger <eric.auger@redhat.com>
 *          Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSTEM_IOMMUFD_H
#define SYSTEM_IOMMUFD_H

#include "qom/object.h"
#include "exec/hwaddr.h"
#include "exec/cpu-common.h"
#include "system/host_iommu_device.h"

#define TYPE_IOMMUFD_BACKEND "iommufd"
OBJECT_DECLARE_TYPE(IOMMUFDBackend, IOMMUFDBackendClass, IOMMUFD_BACKEND)

struct IOMMUFDBackendClass {
    ObjectClass parent_class;
};

struct IOMMUFDBackend {
    Object parent;

    /*< protected >*/
    int fd;            /* /dev/iommu file descriptor */
    bool owned;        /* is the /dev/iommu opened internally */
    Error *cpr_blocker;/* set if be does not support CPR */
    uint32_t users;

    /*< public >*/
};




#define TYPE_HOST_IOMMU_DEVICE_IOMMUFD TYPE_HOST_IOMMU_DEVICE "-iommufd"
OBJECT_DECLARE_TYPE(HostIOMMUDeviceIOMMUFD, HostIOMMUDeviceIOMMUFDClass,
                    HOST_IOMMU_DEVICE_IOMMUFD)

/* Overload of the host IOMMU device for the iommufd backend */
struct HostIOMMUDeviceIOMMUFD {
    HostIOMMUDevice parent_obj;

    IOMMUFDBackend *iommufd;
    uint32_t devid;
    uint32_t hwpt_id;
};

struct HostIOMMUDeviceIOMMUFDClass {
    HostIOMMUDeviceClass parent_class;

    /**
     * @attach_hwpt: attach host IOMMU device to IOMMUFD hardware page table.
     * VFIO and VDPA device can have different implementation.
     *
     * Mandatory callback.
     *
     * @idev: host IOMMU device backed by IOMMUFD backend.
     *
     * @hwpt_id: ID of IOMMUFD hardware page table.
     *
     * @errp: pass an Error out when attachment fails.
     *
     * Returns: true on success, false on failure.
     */
    bool (*attach_hwpt)(HostIOMMUDeviceIOMMUFD *idev, uint32_t hwpt_id,
                        Error **errp);
    /**
     * @detach_hwpt: detach host IOMMU device from IOMMUFD hardware page table.
     * VFIO and VDPA device can have different implementation.
     *
     * Mandatory callback.
     *
     * @idev: host IOMMU device backed by IOMMUFD backend.
     *
     * @errp: pass an Error out when attachment fails.
     *
     * Returns: true on success, false on failure.
     */
    bool (*detach_hwpt)(HostIOMMUDeviceIOMMUFD *idev, Error **errp);
};

#endif
