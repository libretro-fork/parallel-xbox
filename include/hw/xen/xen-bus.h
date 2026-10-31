/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BUS_H
#define HW_XEN_BUS_H

#include "hw/qdev-core.h"
#include "hw/xen/xen_backend_ops.h"
#include "qemu/notify.h"
#include "qemu/queue.h"
#include "qom/object.h"

typedef struct XenEventChannel XenEventChannel;

struct XenDevice {
    DeviceState qdev;
    domid_t frontend_id;
    char *name;
    struct qemu_xs_handle *xsh;
    char *backend_path, *frontend_path;
    enum xenbus_state backend_state, frontend_state;
    Notifier exit;
    struct qemu_xs_watch *backend_state_watch, *frontend_state_watch;
    bool backend_online;
    struct qemu_xs_watch *backend_online_watch;
    xengnttab_handle *xgth;
    bool inactive;
    QLIST_HEAD(, XenEventChannel) event_channels;
    QLIST_ENTRY(XenDevice) list;
};
typedef struct XenDevice XenDevice;

typedef char *(*XenDeviceGetFrontendPath)(XenDevice *xendev, Error **errp);
typedef char *(*XenDeviceGetName)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceRealize)(XenDevice *xendev, Error **errp);
typedef void (*XenDeviceFrontendChanged)(XenDevice *xendev,
                                         enum xenbus_state frontend_state,
                                         Error **errp);
typedef void (*XenDeviceUnrealize)(XenDevice *xendev);

struct XenDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *backend;
    const char *device;
    XenDeviceGetFrontendPath get_frontend_path;
    XenDeviceGetName get_name;
    XenDeviceRealize realize;
    XenDeviceFrontendChanged frontend_changed;
    XenDeviceUnrealize unrealize;
};

#define TYPE_XEN_DEVICE "xen-device"
OBJECT_DECLARE_TYPE(XenDevice, XenDeviceClass, XEN_DEVICE)

struct XenBus {
    BusState qbus;
    domid_t backend_id;
    struct qemu_xs_handle *xsh;
    unsigned int backend_types;
    struct qemu_xs_watch **backend_watch;
    QLIST_HEAD(, XenDevice) inactive_devices;
};

struct XenBusClass {
    /*< private >*/
    BusClass parent_class;
};

#define TYPE_XEN_BUS "xen-bus"
OBJECT_DECLARE_TYPE(XenBus, XenBusClass,
                    XEN_BUS)





typedef struct XenDeviceGrantCopySegment {
    union {
        void *virt;
        struct {
            uint32_t ref;
            off_t offset;
        } foreign;
    } source, dest;
    size_t len;
} XenDeviceGrantCopySegment;

typedef bool (*XenEventHandler)(void *opaque);


#endif /* HW_XEN_BUS_H */
