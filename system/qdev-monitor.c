/*
 *  Dynamic device configuration and creation.
 *
 *  Copyright (c) 2009 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "monitor/qdev.h"
#include "system/arch_init.h"
#include "system/runstate.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qdev.h"
#include "qapi/qmp-registry.h"
#include "qobject/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qobject/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qemu/option_int.h"
#include "system/block-backend.h"
#include "migration/misc.h"
#include "qemu/cutils.h"
#include "hw/qdev-properties.h"
#include "hw/clock.h"
#include "hw/boards.h"

/*
 * Aliases were a bad idea from the start.  Let's keep them
 * from spreading further.
 */
typedef struct QDevAlias
{
    const char *typename;
    const char *alias;
    uint32_t arch_mask;
} QDevAlias;

/* default virtio transport per architecture */
#define QEMU_ARCH_VIRTIO_PCI (QEMU_ARCH_ALPHA | \
                              QEMU_ARCH_ARM | \
                              QEMU_ARCH_HPPA | \
                              QEMU_ARCH_I386 | \
                              QEMU_ARCH_LOONGARCH | \
                              QEMU_ARCH_MIPS | \
                              QEMU_ARCH_OPENRISC | \
                              QEMU_ARCH_PPC | \
                              QEMU_ARCH_RISCV | \
                              QEMU_ARCH_SH4 | \
                              QEMU_ARCH_SPARC | \
                              QEMU_ARCH_XTENSA)
#define QEMU_ARCH_VIRTIO_CCW (QEMU_ARCH_S390X)
#define QEMU_ARCH_VIRTIO_MMIO (QEMU_ARCH_M68K)

/* Please keep this table sorted by typename. */
static const QDevAlias qdev_alias_table[] = {
    { "AC97", "ac97" },
    { "e1000", "e1000-82540em" },
    { "ES1370", "es1370" },
    { "ich9-ahci", "ahci" },
    { "lsi53c895a", "lsi" },
    { "virtio-9p-device", "virtio-9p", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-9p-ccw", "virtio-9p", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-9p-pci", "virtio-9p", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-balloon-device", "virtio-balloon", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-balloon-ccw", "virtio-balloon", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-balloon-pci", "virtio-balloon", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-blk-device", "virtio-blk", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-blk-ccw", "virtio-blk", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-blk-pci", "virtio-blk", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-gpu-device", "virtio-gpu", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-gpu-ccw", "virtio-gpu", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-gpu-pci", "virtio-gpu", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-gpu-gl-device", "virtio-gpu-gl", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-gpu-gl-pci", "virtio-gpu-gl", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-gpu-rutabaga-device", "virtio-gpu-rutabaga",
      QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-gpu-rutabaga-pci", "virtio-gpu-rutabaga", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-input-host-device", "virtio-input-host", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-input-host-ccw", "virtio-input-host", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-input-host-pci", "virtio-input-host", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-iommu-pci", "virtio-iommu", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-keyboard-device", "virtio-keyboard", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-keyboard-ccw", "virtio-keyboard", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-keyboard-pci", "virtio-keyboard", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-mouse-device", "virtio-mouse", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-mouse-ccw", "virtio-mouse", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-mouse-pci", "virtio-mouse", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-net-device", "virtio-net", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-net-ccw", "virtio-net", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-net-pci", "virtio-net", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-rng-device", "virtio-rng", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-rng-ccw", "virtio-rng", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-rng-pci", "virtio-rng", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-scsi-device", "virtio-scsi", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-scsi-ccw", "virtio-scsi", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-scsi-pci", "virtio-scsi", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-serial-device", "virtio-serial", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-serial-ccw", "virtio-serial", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-serial-pci", "virtio-serial", QEMU_ARCH_VIRTIO_PCI},
    { "virtio-sound-device", "virtio-sound", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-sound-pci", "virtio-sound", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-tablet-device", "virtio-tablet", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-tablet-ccw", "virtio-tablet", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-tablet-pci", "virtio-tablet", QEMU_ARCH_VIRTIO_PCI },
    { }
};

static const char *qdev_class_get_alias(DeviceClass *dc)
{
    const char *typename = object_class_get_name(OBJECT_CLASS(dc));
    int i;

    for (i = 0; qdev_alias_table[i].typename; i++) {
        if (qdev_alias_table[i].arch_mask &&
            !qemu_arch_available(qdev_alias_table[i].arch_mask)) {
            continue;
        }

        if (strcmp(qdev_alias_table[i].typename, typename) == 0) {
            return qdev_alias_table[i].alias;
        }
    }

    return NULL;
}

static bool qdev_class_has_alias(DeviceClass *dc)
{
    return (qdev_class_get_alias(dc) != NULL);
}

static void qdev_print_devinfo(DeviceClass *dc)
{
    qemu_printf("name \"%s\"", object_class_get_name(OBJECT_CLASS(dc)));
    if (dc->bus_type) {
        qemu_printf(", bus %s", dc->bus_type);
    }
    if (qdev_class_has_alias(dc)) {
        qemu_printf(", alias \"%s\"", qdev_class_get_alias(dc));
    }
    if (dc->desc) {
        qemu_printf(", desc \"%s\"", dc->desc);
    }
    if (!dc->user_creatable) {
        qemu_printf(", no-user");
    }
    qemu_printf("\n");
}

static void qdev_print_devinfos(bool show_no_user)
{
    static const char *cat_name[DEVICE_CATEGORY_MAX + 1] = {
        [DEVICE_CATEGORY_BRIDGE]  = "Controller/Bridge/Hub",
        [DEVICE_CATEGORY_USB]     = "USB",
        [DEVICE_CATEGORY_STORAGE] = "Storage",
        [DEVICE_CATEGORY_NETWORK] = "Network",
        [DEVICE_CATEGORY_INPUT]   = "Input",
        [DEVICE_CATEGORY_DISPLAY] = "Display",
        [DEVICE_CATEGORY_SOUND]   = "Sound",
        [DEVICE_CATEGORY_MISC]    = "Misc",
        [DEVICE_CATEGORY_CPU]     = "CPU",
        [DEVICE_CATEGORY_WATCHDOG]= "Watchdog",
        [DEVICE_CATEGORY_MAX]     = "Uncategorized",
    };
    GSList *list, *elt;
    int i;
    bool cat_printed;

    module_load_qom_all();
    list = object_class_get_list_sorted(TYPE_DEVICE, false);

    for (i = 0; i <= DEVICE_CATEGORY_MAX; i++) {
        cat_printed = false;
        for (elt = list; elt; elt = elt->next) {
            DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data,
                                                 TYPE_DEVICE);
            if ((i < DEVICE_CATEGORY_MAX
                 ? !test_bit(i, dc->categories)
                 : !bitmap_empty(dc->categories, DEVICE_CATEGORY_MAX))
                || (!show_no_user
                    && !dc->user_creatable)) {
                continue;
            }
            if (!cat_printed) {
                qemu_printf("%s%s devices:\n", i ? "\n" : "", cat_name[i]);
                cat_printed = true;
            }
            qdev_print_devinfo(dc);
        }
    }

    g_slist_free(list);
}

static const char *find_typename_by_alias(const char *alias)
{
    int i;

    for (i = 0; qdev_alias_table[i].alias; i++) {
        if (qdev_alias_table[i].arch_mask &&
            !qemu_arch_available(qdev_alias_table[i].arch_mask)) {
            continue;
        }

        if (strcmp(qdev_alias_table[i].alias, alias) == 0) {
            return qdev_alias_table[i].typename;
        }
    }

    return NULL;
}

static DeviceClass *qdev_get_device_class(const char **driver, Error **errp)
{
    ObjectClass *oc;
    DeviceClass *dc;
    const char *original_name = *driver;

    oc = module_object_class_by_name(*driver);
    if (!oc) {
        const char *typename = find_typename_by_alias(*driver);

        if (typename) {
            *driver = typename;
            oc = module_object_class_by_name(*driver);
        }
    }

    if (!object_class_dynamic_cast(oc, TYPE_DEVICE)) {
        if (*driver != original_name) {
            error_setg(errp, "'%s' (alias '%s') is not a valid device model"
                       " name", original_name, *driver);
        } else {
            error_setg(errp, "'%s' is not a valid device model name", *driver);
        }
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "a non-abstract device type");
        return NULL;
    }

    dc = DEVICE_CLASS(oc);
    if (!dc->user_creatable) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "a pluggable device type");
        return NULL;
    }

    if (object_class_dynamic_cast(oc, TYPE_SYS_BUS_DEVICE)) {
        /* sysbus devices need to be allowed by the machine */
        MachineClass *mc = MACHINE_CLASS(object_get_class(qdev_get_machine()));
        if (!device_type_is_dynamic_sysbus(mc, *driver)) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                       "a dynamic sysbus device type for the machine");
            return NULL;
        }
    }

    return dc;
}

int qdev_device_help(QemuOpts *opts)
{
    const char *driver;

    driver = qemu_opt_get(opts, "driver");
    if (driver && is_help_option(driver)) {
        qdev_print_devinfos(false);
        return 1;
    }

    if (!driver || !qemu_opt_has_help_opt(opts)) {
        return 0;
    }

    /* the core has no QMP to list a device's properties with */
    qemu_printf("%s: no option help in the libretro core\n", driver);
    return 1;
}

static Object *qdev_get_peripheral(void)
{
    static Object *dev;

    if (dev == NULL) {
        dev = machine_get_container("peripheral");
    }

    return dev;
}

static Object *qdev_get_peripheral_anon(void)
{
    static Object *dev;

    if (dev == NULL) {
        dev = machine_get_container("peripheral-anon");
    }

    return dev;
}

static void qbus_error_append_bus_list_hint(DeviceState *dev,
                                            Error *const *errp)
{
    BusState *child;
    const char *sep = " ";

    error_append_hint(errp, "child buses at \"%s\":",
                      dev->id ? dev->id : object_get_typename(OBJECT(dev)));
    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        error_append_hint(errp, "%s\"%s\"", sep, child->name);
        sep = ", ";
    }
    error_append_hint(errp, "\n");
}

static void qbus_error_append_dev_list_hint(BusState *bus,
                                            Error *const *errp)
{
    BusChild *kid;
    const char *sep = " ";

    error_append_hint(errp, "devices at \"%s\":", bus->name);
    QTAILQ_FOREACH_RCU(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        error_append_hint(errp, "%s\"%s\"", sep,
                          object_get_typename(OBJECT(dev)));
        if (dev->id) {
            error_append_hint(errp, "/\"%s\"", dev->id);
        }
        sep = ", ";
    }
    error_append_hint(errp, "\n");
}

static BusState *qbus_find_bus(DeviceState *dev, char *elem)
{
    BusState *child;

    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        if (strcmp(child->name, elem) == 0) {
            return child;
        }
    }
    return NULL;
}

static DeviceState *qbus_find_dev(BusState *bus, char *elem)
{
    BusChild *kid;

    /*
     * try to match in order:
     *   (1) instance id, if present
     *   (2) driver name
     *   (3) driver alias, if present
     */
    QTAILQ_FOREACH_RCU(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        if (dev->id  &&  strcmp(dev->id, elem) == 0) {
            return dev;
        }
    }
    QTAILQ_FOREACH_RCU(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        if (strcmp(object_get_typename(OBJECT(dev)), elem) == 0) {
            return dev;
        }
    }
    QTAILQ_FOREACH_RCU(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        DeviceClass *dc = DEVICE_GET_CLASS(dev);

        if (qdev_class_has_alias(dc) &&
            strcmp(qdev_class_get_alias(dc), elem) == 0) {
            return dev;
        }
    }
    return NULL;
}

static inline bool qbus_is_full(BusState *bus)
{
    BusClass *bus_class;

    if (bus->full) {
        return true;
    }
    bus_class = BUS_GET_CLASS(bus);
    return bus_class->max_dev && bus->num_children >= bus_class->max_dev;
}

/*
 * Search the tree rooted at @bus for a bus.
 * If @name, search for a bus with that name.  Note that bus names
 * need not be unique.  Yes, that's screwed up.
 * Else search for a bus that is a subtype of @bus_typename.
 * If more than one exists, prefer one that can take another device.
 * Return the bus if found, else %NULL.
 */
static BusState *qbus_find_recursive(BusState *bus, const char *name,
                                     const char *bus_typename)
{
    BusChild *kid;
    BusState *pick, *child, *ret;
    bool match;

    assert(name || bus_typename);
    if (name) {
        match = !strcmp(bus->name, name);
    } else {
        match = !!object_dynamic_cast(OBJECT(bus), bus_typename);
    }

    if (match && !qbus_is_full(bus)) {
        return bus;             /* root matches and isn't full */
    }

    pick = match ? bus : NULL;

    QTAILQ_FOREACH_RCU(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qbus_find_recursive(child, name, bus_typename);
            if (ret && !qbus_is_full(ret)) {
                return ret;     /* a descendant matches and isn't full */
            }
            if (ret && !pick) {
                pick = ret;
            }
        }
    }

    /* root or a descendant matches, but is full */
    return pick;
}

static BusState *qbus_find(const char *path, Error **errp)
{
    DeviceState *dev;
    BusState *bus;
    char elem[128];
    int pos, len;

    /* find start element */
    if (path[0] == '/') {
        bus = sysbus_get_default();
        pos = 0;
    } else {
        if (sscanf(path, "%127[^/]%n", elem, &len) != 1) {
            assert(!path[0]);
            elem[0] = len = 0;
        }
        bus = qbus_find_recursive(sysbus_get_default(), elem, NULL);
        if (!bus) {
            error_setg(errp, "Bus '%s' not found", elem);
            return NULL;
        }
        pos = len;
    }

    for (;;) {
        assert(path[pos] == '/' || !path[pos]);
        while (path[pos] == '/') {
            pos++;
        }
        if (path[pos] == '\0') {
            break;
        }

        /* find device */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            g_assert_not_reached();
            elem[0] = len = 0;
        }
        pos += len;
        dev = qbus_find_dev(bus, elem);
        if (!dev) {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", elem);
            qbus_error_append_dev_list_hint(bus, errp);
            return NULL;
        }

        assert(path[pos] == '/' || !path[pos]);
        while (path[pos] == '/') {
            pos++;
        }
        if (path[pos] == '\0') {
            /* last specified element is a device.  If it has exactly
             * one child bus accept it nevertheless */
            if (dev->num_child_bus == 1) {
                bus = QLIST_FIRST(&dev->child_bus);
                break;
            }
            if (dev->num_child_bus) {
                error_setg(errp, "Device '%s' has multiple child buses",
                           elem);
                qbus_error_append_bus_list_hint(dev, errp);
            } else {
                error_setg(errp, "Device '%s' has no child bus", elem);
            }
            return NULL;
        }

        /* find bus */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            g_assert_not_reached();
            elem[0] = len = 0;
        }
        pos += len;
        bus = qbus_find_bus(dev, elem);
        if (!bus) {
            error_setg(errp, "Bus '%s' not found", elem);
            qbus_error_append_bus_list_hint(dev, errp);
            return NULL;
        }
    }

    if (qbus_is_full(bus)) {
        error_setg(errp, "Bus '%s' is full", path);
        return NULL;
    }
    return bus;
}

/* Takes ownership of @id, will be freed when deleting the device */
const char *qdev_set_id(DeviceState *dev, char *id, Error **errp)
{
    ObjectProperty *prop;

    assert(!dev->id && !qdev_is_realized(dev));

    /*
     * object_property_[try_]add_child() below will assert the device
     * has no parent
     */
    if (id) {
        prop = object_property_try_add_child(qdev_get_peripheral(), id,
                                             OBJECT(dev), NULL);
        if (prop) {
            dev->id = id;
        } else {
            error_setg(errp, "Duplicate device ID '%s'", id);
            g_free(id);
            return NULL;
        }
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        prop = object_property_add_child(qdev_get_peripheral_anon(), name,
                                         OBJECT(dev));
        g_free(name);
    }

    return prop->name;
}

BusState *qdev_find_default_bus(DeviceClass *dc, Error **errp)
{
    BusState *bus = NULL;

    assert(dc->bus_type != NULL);
    bus = qbus_find_recursive(sysbus_get_default(), NULL, dc->bus_type);
    if (!bus) {
        error_setg(errp, "No '%s' bus found for device '%s'",
                   dc->bus_type, object_class_get_name(OBJECT_CLASS(dc)));
        return NULL;
    }
    if (qbus_is_full(bus)) {
        error_setg(errp, "A '%s' bus was found but is full", dc->bus_type);
        return NULL;
    }

    return bus;
}

DeviceState *qdev_device_add_from_qdict(const QDict *opts,
                                        bool from_json, Error **errp)
{
    ERRP_GUARD();
    DeviceClass *dc;
    const char *driver, *path;
    char *id;
    DeviceState *dev;
    BusState *bus = NULL;
    QDict *properties;

    driver = qdict_get_try_str(opts, "driver");
    if (!driver) {
        error_setg(errp, QERR_MISSING_PARAMETER, "driver");
        return NULL;
    }

    /* find driver */
    dc = qdev_get_device_class(&driver, errp);
    if (!dc) {
        return NULL;
    }

    /* find bus */
    path = qdict_get_try_str(opts, "bus");
    if (path != NULL) {
        bus = qbus_find(path, errp);
        if (!bus) {
            return NULL;
        }
        if (!object_dynamic_cast(OBJECT(bus), dc->bus_type)) {
            error_setg(errp, "Device '%s' can't go on %s bus",
                       driver, object_get_typename(OBJECT(bus)));
            return NULL;
        }
    } else if (dc->bus_type != NULL) {
        bus = qdev_find_default_bus(dc, errp);
        if (!bus) {
            return NULL;
        }
    }

    if (qdev_should_hide_device(opts, from_json, errp)) {
        if (bus && !qbus_is_hotpluggable(bus)) {
            error_setg(errp, "Bus '%s' does not support hotplugging",
                       bus->name);
        }
        return NULL;
    } else if (*errp) {
        return NULL;
    }

    if (migration_is_running()) {
        error_setg(errp, "device_add not allowed while migrating");
        return NULL;
    }

    /* create device */
    dev = qdev_new(driver);

    /* Check whether the hotplug is allowed by the machine */
    if (phase_check(PHASE_MACHINE_READY) &&
        !qdev_hotplug_allowed(dev, bus, errp)) {
        goto err_del_dev;
    }

    /*
     * set dev's parent and register its id.
     * If it fails it means the id is already taken.
     */
    id = g_strdup(qdict_get_try_str(opts, "id"));
    if (!qdev_set_id(dev, id, errp)) {
        goto err_del_dev;
    }

    /* set properties */
    properties = qdict_clone_shallow(opts);
    qdict_del(properties, "driver");
    qdict_del(properties, "bus");
    qdict_del(properties, "id");

    object_set_properties_from_keyval(&dev->parent_obj, properties, from_json,
                                      errp);
    qobject_unref(properties);
    if (*errp) {
        goto err_del_dev;
    }

    if (!qdev_realize(dev, bus, errp)) {
        goto err_del_dev;
    }
    return dev;

err_del_dev:
    object_unparent(OBJECT(dev));
    object_unref(OBJECT(dev));

    return NULL;
}

/* Takes ownership of @opts on success */
DeviceState *qdev_device_add(QemuOpts *opts, Error **errp)
{
    QDict *qdict = qemu_opts_to_qdict(opts, NULL);
    DeviceState *ret;

    ret = qdev_device_add_from_qdict(qdict, false, errp);
    if (ret) {
        qemu_opts_del(opts);
    }
    qobject_unref(qdict);
    return ret;
}

void qmp_device_add(QDict *qdict, QObject **ret_data, Error **errp)
{
    DeviceState *dev;

    dev = qdev_device_add_from_qdict(qdict, true, errp);
    if (!dev) {
        /*
         * Drain all pending RCU callbacks. This is done because
         * some bus related operations can delay a device removal
         * (in this case this can happen if device is added and then
         * removed due to a configuration error)
         * to a RCU callback, but user might expect that this interface
         * will finish its job completely once qmp command returns result
         * to the user
         */
        drain_call_rcu();
    }
    object_unref(OBJECT(dev));
}

QemuOptsList qemu_device_opts = {
    .name = "device",
    .implied_opt_name = "driver",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_device_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting device properties
         */
        { /* end of list */ }
    },
};

