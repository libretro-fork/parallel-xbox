#ifndef QEMU_HW_XEN_PVDEV_H
#define QEMU_HW_XEN_PVDEV_H

#include "hw/sysbus.h"
#include "hw/xen/xen_backend_ops.h"

/* ------------------------------------------------------------- */

#define XEN_BUFSIZE 1024

struct XenLegacyDevice;

/* driver uses grant tables  ->  open gntdev device (xendev->gnttabdev) */
#define DEVOPS_FLAG_NEED_GNTDEV   1
/* don't expect frontend doing correct state transitions (aka console quirk) */
#define DEVOPS_FLAG_IGNORE_STATE  2

struct XenDevOps {
    size_t    size;
    uint32_t  flags;
    void      (*alloc)(struct XenLegacyDevice *xendev);
    int       (*init)(struct XenLegacyDevice *xendev);
    int       (*initialise)(struct XenLegacyDevice *xendev);
    void      (*connected)(struct XenLegacyDevice *xendev);
    void      (*event)(struct XenLegacyDevice *xendev);
    void      (*disconnect)(struct XenLegacyDevice *xendev);
    int       (*free)(struct XenLegacyDevice *xendev);
    void      (*backend_changed)(struct XenLegacyDevice *xendev,
                                 const char *node);
    void      (*frontend_changed)(struct XenLegacyDevice *xendev,
                                  const char *node);
};

struct XenLegacyDevice {
    SysBusDevice parent_obj;

    const char         *type;
    int                dom;
    int                dev;
    char               name[64];
    int                debug;

    struct qemu_xs_watch *watch;
    enum xenbus_state  be_state;
    enum xenbus_state  fe_state;
    int                online;
    char               be[XEN_BUFSIZE];
    char               *fe;
    char               *protocol;
    int                remote_port;
    int                local_port;

    xenevtchn_handle   *evtchndev;
    xengnttab_handle   *gnttabdev;

    const struct XenDevOps *ops;
    QTAILQ_ENTRY(XenLegacyDevice) next;
};

/* ------------------------------------------------------------- */

/* xenstore helper functions */



#endif /* QEMU_HW_XEN_PVDEV_H */
