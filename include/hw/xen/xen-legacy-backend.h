#ifndef HW_XEN_LEGACY_BACKEND_H
#define HW_XEN_LEGACY_BACKEND_H

#include "hw/xen/xen_backend_ops.h"
#include "hw/xen/xen_pvdev.h"
#include "qom/object.h"

#define TYPE_XENSYSDEV "xen-sysdev"
#define TYPE_XENSYSBUS "xen-sysbus"
#define TYPE_XENBACKEND "xen-backend"

typedef struct XenLegacyDevice XenLegacyDevice;
DECLARE_INSTANCE_CHECKER(XenLegacyDevice, XENBACKEND,
                         TYPE_XENBACKEND)

/* variables */
extern struct qemu_xs_handle *xenstore;
extern const char *xen_protocol;
extern DeviceState *xen_sysdev;
extern BusState *xen_sysbus;


/* xen backend driver bits */
void *xen_be_map_grant_refs(struct XenLegacyDevice *xendev, uint32_t *refs,
                            unsigned int nr_refs, int prot);
void xen_be_unmap_grant_refs(struct XenLegacyDevice *xendev, void *ptr,
                             uint32_t *refs, unsigned int nr_refs);

static inline void *xen_be_map_grant_ref(struct XenLegacyDevice *xendev,
                                         uint32_t ref, int prot)
{
    return xen_be_map_grant_refs(xendev, &ref, 1, prot);
}

static inline void xen_be_unmap_grant_ref(struct XenLegacyDevice *xendev,
                                          void *ptr, uint32_t ref)
{
    return xen_be_unmap_grant_refs(xendev, ptr, &ref, 1);
}

/* configuration (aka xenbus setup) */

#endif /* HW_XEN_LEGACY_BACKEND_H */
