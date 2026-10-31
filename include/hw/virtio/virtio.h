/*
 * Virtio Support
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_H
#define QEMU_VIRTIO_H

#include "system/memory.h"
#include "hw/qdev-core.h"
#include "hw/virtio/virtio-features.h"
#include "net/net.h"
#include "migration/vmstate.h"
#include "qemu/event_notifier.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_ring.h"
#include "qom/object.h"
#include "block/aio.h"

/*
 * A guest should never accept this. It implies negotiation is broken
 * between the driver frontend and the device. This bit is re-used for
 * vhost-user to advertise VHOST_USER_F_PROTOCOL_FEATURES between QEMU
 * and a vhost-user backend.
 */
#define VIRTIO_F_BAD_FEATURE 30

#define VIRTIO_LEGACY_FEATURES ((0x1ULL << VIRTIO_F_BAD_FEATURE) | \
                                (0x1ULL << VIRTIO_F_NOTIFY_ON_EMPTY) | \
                                (0x1ULL << VIRTIO_F_ANY_LAYOUT))

struct VirtQueue;

static inline hwaddr vring_align(hwaddr addr,
                                             unsigned long align)
{
    return QEMU_ALIGN_UP(addr, align);
}

typedef struct VirtIOFeature {
    uint64_t flags;
    size_t end;
} VirtIOFeature;

typedef struct VirtIOConfigSizeParams {
    size_t min_size;
    size_t max_size;
    const VirtIOFeature *feature_sizes;
} VirtIOConfigSizeParams;

typedef struct VirtQueue VirtQueue;

#define VIRTQUEUE_MAX_SIZE 1024

typedef struct VirtQueueElement
{
    unsigned int index;
    unsigned int len;
    unsigned int ndescs;
    unsigned int out_num;
    unsigned int in_num;
    /* Element has been processed (VIRTIO_F_IN_ORDER) */
    bool in_order_filled;
    hwaddr *in_addr;
    hwaddr *out_addr;
    struct iovec *in_sg;
    struct iovec *out_sg;
} VirtQueueElement;

#define VIRTIO_QUEUE_MAX 1024

#define VIRTIO_NO_VECTOR 0xffff

/* special index value used internally for config irqs */
#define VIRTIO_CONFIG_IRQ_IDX -1

#define TYPE_VIRTIO_DEVICE "virtio-device"
OBJECT_DECLARE_TYPE(VirtIODevice, VirtioDeviceClass, VIRTIO_DEVICE)

typedef struct {
    int virtio_bit;
    const char *feature_desc;
} qmp_virtio_feature_map_t;

enum virtio_device_endian {
    VIRTIO_DEVICE_ENDIAN_UNKNOWN,
    VIRTIO_DEVICE_ENDIAN_LITTLE,
    VIRTIO_DEVICE_ENDIAN_BIG,
};

/**
 * struct VirtIODevice - common VirtIO structure
 * @name: name of the device
 * @status: VirtIO Device Status field
 *
 */
struct VirtIODevice
{
    DeviceState parent_obj;
    const char *name;
    uint8_t status;
    uint8_t isr;
    uint16_t queue_sel;
    /**
     * These fields represent a set of VirtIO features at various
     * levels of the stack. @host_features indicates the complete
     * feature set the VirtIO device can offer to the driver.
     * @guest_features indicates which features the VirtIO driver has
     * selected by writing to the feature register. Finally
     * @backend_features represents everything supported by the
     * backend (e.g. vhost) and could potentially be a subset of the
     * total feature set offered by QEMU.
     */
    VIRTIO_DECLARE_FEATURES(host_features);
    VIRTIO_DECLARE_FEATURES(guest_features);
    VIRTIO_DECLARE_FEATURES(backend_features);

    size_t config_len;
    void *config;
    uint16_t config_vector;
    uint32_t generation;
    int nvectors;
    VirtQueue *vq;
    MemoryListener listener;
    uint16_t device_id;
    /* @vm_running: current VM running state via virtio_vmstate_change() */
    bool vm_running;
    bool broken; /* device in invalid state, needs reset */
    bool use_disabled_flag; /* allow use of 'disable' flag when needed */
    bool disabled; /* device in temporarily disabled state */
    /**
     * @use_started: true if the @started flag should be used to check the
     * current state of the VirtIO device. Otherwise status bits
     * should be checked for a current status of the device.
     * @use_started is only set via QMP and defaults to true for all
     * modern machines (since 4.1).
     */
    bool use_started;
    bool started;
    bool start_on_kick; /* when virtio 1.0 feature has not been negotiated */
    bool disable_legacy_check;
    bool vhost_started;
    VMChangeStateEntry *vmstate;
    char *bus_name;
    uint8_t device_endian;
    /**
     * @user_guest_notifier_mask: gate usage of ->guest_notifier_mask() callback.
     * This is used to suppress the masking of guest updates for
     * vhost-user devices which are asynchronous by design.
     */
    bool use_guest_notifier_mask;
    AddressSpace *dma_as;
    QLIST_HEAD(, VirtQueue) *vector_queues;
    QTAILQ_ENTRY(VirtIODevice) next;
    /**
     * @config_notifier: the event notifier that handles config events
     */
    EventNotifier config_notifier;
    bool device_iotlb_enabled;
};

struct VirtioDeviceClass {
    /*< private >*/
    DeviceClass parent;
    /*< public >*/

    /* This is what a VirtioDevice must implement */
    DeviceRealize realize;
    DeviceUnrealize unrealize;
    void (*get_features_ex)(VirtIODevice *vdev, uint64_t *requested_features,
                            Error **errp);
    void (*set_features_ex)(VirtIODevice *vdev, const uint64_t *val);
    uint64_t (*get_features)(VirtIODevice *vdev,
                             uint64_t requested_features,
                             Error **errp);
    uint64_t (*bad_features)(VirtIODevice *vdev);
    void (*set_features)(VirtIODevice *vdev, uint64_t val);
    int (*validate_features)(VirtIODevice *vdev);
    void (*get_config)(VirtIODevice *vdev, uint8_t *config);
    void (*set_config)(VirtIODevice *vdev, const uint8_t *config);
    void (*reset)(VirtIODevice *vdev);
    int (*set_status)(VirtIODevice *vdev, uint8_t val);
    /* Device must validate queue_index.  */
    void (*queue_reset)(VirtIODevice *vdev, uint32_t queue_index);
    /* Device must validate queue_index.  */
    void (*queue_enable)(VirtIODevice *vdev, uint32_t queue_index);
    /* For transitional devices, this is a bitmap of features
     * that are only exposed on the legacy interface but not
     * the modern one.
     */
    uint64_t legacy_features;
    /* Test and clear event pending status.
     * Should be called after unmask to avoid losing events.
     * If backend does not support masking,
     * must check in frontend instead.
     */
    bool (*guest_notifier_pending)(VirtIODevice *vdev, int n);
    /* Mask/unmask events from this vq. Any events reported
     * while masked will become pending.
     * If backend does not support masking,
     * must mask in frontend instead.
     */
    void (*guest_notifier_mask)(VirtIODevice *vdev, int n, bool mask);
    int (*start_ioeventfd)(VirtIODevice *vdev);
    void (*stop_ioeventfd)(VirtIODevice *vdev);
    /*
     * Called before loading queues.
     * If the number of queues change at runtime, use @n to know the
     * number and add or remove queues accordingly.
     * Note that this function is called in the middle of loading vmsd;
     * no assumption should be made on states being loaded from vmsd.
     */
    int (*pre_load_queues)(VirtIODevice *vdev, uint32_t n);
    /* Saving and loading of a device; trying to deprecate save/load
     * use vmsd for new devices.
     */
    void (*save)(VirtIODevice *vdev, QEMUFile *f);
    int (*load)(VirtIODevice *vdev, QEMUFile *f, int version_id);
    /* Post load hook in vmsd is called early while device is processed, and
     * when VirtIODevice isn't fully initialized.  Devices should use this instead,
     * unless they specifically want to verify the migration stream as it's
     * processed, e.g. for bounds checking.
     */
    int (*post_load)(VirtIODevice *vdev);
    const VMStateDescription *vmsd;
    bool (*primary_unplug_pending)(void *opaque);
    /* May be called even when vdev->vhost_started is false */
    struct vhost_dev *(*get_vhost)(VirtIODevice *vdev);
    void (*toggle_device_iotlb)(VirtIODevice *vdev);
};


/* Set the child bus name. */

typedef void (*VirtIOHandleOutput)(VirtIODevice *, VirtQueue *);



/**
 * Return <0 on error or an opaque >=0 to pass to
 * virtio_queue_enable_notification_and_check on success.
 */


extern const VMStateInfo virtio_vmstate_info;

#define VMSTATE_VIRTIO_DEVICE \
    {                                         \
        .name = "virtio",                     \
        .info = &virtio_vmstate_info,         \
        .flags = VMS_SINGLE,                  \
    }



/**
 * Enable notification and check whether guest has added some
 * buffers since last call to virtqueue_get_avail_bytes.
 *
 * @opaque: value returned from virtqueue_get_avail_bytes
 */

/* Host binding interface.  */

void virtio_queue_set_align(VirtIODevice *vdev, int n, int align);

/* Base devices.  */
typedef struct VirtIOBlkConf VirtIOBlkConf;
struct virtio_net_conf;
typedef struct virtio_serial_conf virtio_serial_conf;
typedef struct virtio_input_conf virtio_input_conf;
typedef struct VirtIOSCSIConf VirtIOSCSIConf;
typedef struct VirtIORNGConf VirtIORNGConf;

#define DEFINE_VIRTIO_COMMON_FEATURES(_state, _field) \
    DEFINE_PROP_BIT64("indirect_desc", _state, _field,    \
                      VIRTIO_RING_F_INDIRECT_DESC, true), \
    DEFINE_PROP_BIT64("event_idx", _state, _field,        \
                      VIRTIO_RING_F_EVENT_IDX, true),     \
    DEFINE_PROP_BIT64("notify_on_empty", _state, _field,  \
                      VIRTIO_F_NOTIFY_ON_EMPTY, true), \
    DEFINE_PROP_BIT64("any_layout", _state, _field, \
                      VIRTIO_F_ANY_LAYOUT, true), \
    DEFINE_PROP_BIT64("iommu_platform", _state, _field, \
                      VIRTIO_F_IOMMU_PLATFORM, false), \
    DEFINE_PROP_BIT64("packed", _state, _field, \
                      VIRTIO_F_RING_PACKED, false), \
    DEFINE_PROP_BIT64("queue_reset", _state, _field, \
                      VIRTIO_F_RING_RESET, true), \
    DEFINE_PROP_BIT64("in_order", _state, _field, \
                      VIRTIO_F_IN_ORDER, false)


static inline void virtio_add_feature(uint64_t *features, unsigned int fbit)
{
    assert(fbit < 64);
    *features |= (1ULL << fbit);
}

static inline void virtio_clear_feature(uint64_t *features, unsigned int fbit)
{
    assert(fbit < 64);
    *features &= ~(1ULL << fbit);
}

static inline bool virtio_has_feature(uint64_t features, unsigned int fbit)
{
    assert(fbit < 64);
    return !!(features & (1ULL << fbit));
}

static inline bool virtio_vdev_has_feature(const VirtIODevice *vdev,
                                           unsigned int fbit)
{
    return virtio_has_feature(vdev->guest_features, fbit);
}

static inline bool virtio_host_has_feature(VirtIODevice *vdev,
                                           unsigned int fbit)
{
    return virtio_has_feature(vdev->host_features, fbit);
}

static inline bool virtio_is_big_endian(VirtIODevice *vdev)
{
    if (!virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        assert(vdev->device_endian != VIRTIO_DEVICE_ENDIAN_UNKNOWN);
        return vdev->device_endian == VIRTIO_DEVICE_ENDIAN_BIG;
    }
    /* Devices conforming to VIRTIO 1.0 or later are always LE. */
    return false;
}

/**
 * virtio_device_started() - check if device started
 * @vdev - the VirtIO device
 * @status - the devices status bits
 *
 * Check if the device is started. For most modern machines this is
 * tracked via the @vdev->started field (to support migration),
 * otherwise we check for the final negotiated status bit that
 * indicates everything is ready.
 */
static inline bool virtio_device_started(VirtIODevice *vdev, uint8_t status)
{
    if (vdev->use_started) {
        return vdev->started;
    }

    return status & VIRTIO_CONFIG_S_DRIVER_OK;
}

/**
 * virtio_device_should_start() - check if device startable
 * @vdev - the VirtIO device
 * @status - the devices status bits
 *
 * This is similar to virtio_device_started() but ignores vdev->started
 * and also encapsulates a check on the VM status which would prevent a
 * device from starting anyway.
 */
static inline bool virtio_device_should_start(VirtIODevice *vdev, uint8_t status)
{
    if (!vdev->vm_running) {
        return false;
    }

    return status & VIRTIO_CONFIG_S_DRIVER_OK;
}

static inline void virtio_set_started(VirtIODevice *vdev, bool started)
{
    if (started) {
        vdev->start_on_kick = false;
    }

    if (vdev->use_started) {
        vdev->started = started;
    }
}

static inline void virtio_set_disabled(VirtIODevice *vdev, bool disable)
{
    if (vdev->use_disabled_flag) {
        vdev->disabled = disable;
    }
}

static inline bool virtio_device_disabled(VirtIODevice *vdev)
{
    return unlikely(vdev->disabled || vdev->broken);
}


QEMUBH *virtio_bh_new_guarded_full(DeviceState *dev,
                                   QEMUBHFunc *cb, void *opaque,
                                   const char *name);
#define virtio_bh_new_guarded(dev, cb, opaque) \
    virtio_bh_new_guarded_full((dev), (cb), (opaque), (stringify(cb)))

#endif
