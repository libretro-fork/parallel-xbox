/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "qemu/help-texts.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/target-info.h"
#include "exec/cpu-common.h"
#include "exec/page-vary.h"
#include "hw/qdev-properties.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qobject/qstring.h"
#include "qobject/qjson.h"
#include "qemu-version.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "qemu/hw-version.h"
#include "qemu/uuid.h"
#include "qemu/target-info.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/runstate-action.h"
#include "system/seccomp.h"
#include "system/tcg.h"
#include "system/xen.h"

#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include "qemu/accel.h"
#include "qemu/async-teardown.h"
#include "qemu/exit-with-parent.h"
#include "hw/usb.h"
#include "hw/isa/isa.h"
#include "hw/scsi/scsi.h"
#include "hw/sd/sd.h"
#include "hw/display/vga.h"
#include "hw/firmware/smbios.h"
#include "hw/acpi/acpi.h"
#include "hw/xen/xen.h"
#include "hw/loader.h"
#include "monitor/qdev.h"
#include "net/net.h"
#include "net/slirp.h"
#include "block/libretro-disk.h"
#include "ui/console.h"
#include "ui/input.h"
#include "system/system.h"
#include "system/numa.h"
#include "system/hostmem.h"
#include "qemu/timer.h"
#include "chardev/char.h"
#include "qemu/bitmap.h"
#include "qemu/log.h"
#include "system/blockdev.h"
#include "hw/block/block.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "migration/misc.h"
#include "migration/snapshot.h"
#include "system/tpm.h"
#include "system/dma.h"
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "exec/icount.h"
#include "migration/colo.h"
#include "migration/postcopy-ram.h"
#include "system/kvm.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/main-loop.h"
#include "system/qtest.h"
#ifdef CONFIG_TCG
#include "tcg/perf.h"
#endif

#include "disas/disas.h"

#include "trace.h"
#include "trace/control.h"
#include "qemu/plugin.h"
#include "qemu/queue.h"
#include "system/arch_init.h"
#include "system/confidential-guest-support.h"

#include "ui/qemu-spice.h"
#include "qapi/string-input-visitor.h"
#include "qapi/opts-visitor.h"
#include "qapi/clone-visitor.h"
#include "qom/object_interfaces.h"
#include "semihosting/semihost.h"
#include "crypto/init.h"
#include "qapi/qapi-events-run-state.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qapi-visit-compat.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/qapi-visit-ui.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-visit-qom.h"
#include "qapi/qapi-commands-ui.h"
#include "block/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qemu/guest-random.h"
#include "qemu/keyval.h"

#include <glib/gstdio.h>
#include "ui/xemu-settings.h"
#include "qapi/qapi-commands-block.h"
#include "hw/xbox/smbus.h"
#include "ui/xemu-notifications.h"
#include "ui/xemu-net.h"
#include "ui/xemu-input.h"
#include "hw/xbox/eeprom_generation.h"
#include "hw/xbox/chihiro/chihiro.h"
#include "block/block-global-state.h"

#define MAX_VIRTIO_CONSOLES 1

typedef struct BlockdevOptionsQueueEntry {
    BlockdevOptions *bdo;
    Location loc;
    QSIMPLEQ_ENTRY(BlockdevOptionsQueueEntry) entry;
} BlockdevOptionsQueueEntry;

typedef QSIMPLEQ_HEAD(, BlockdevOptionsQueueEntry) BlockdevOptionsQueue;

typedef struct ObjectOption {
    ObjectOptions *opts;
    QTAILQ_ENTRY(ObjectOption) next;
} ObjectOption;

typedef struct DeviceOption {
    QDict *opts;
    Location loc;
    QTAILQ_ENTRY(DeviceOption) next;
} DeviceOption;

static const char *accelerators;
static bool have_custom_ram_size;
static const char *ram_memdev_id;
static QDict *machine_opts_dict;
static QTAILQ_HEAD(, ObjectOption) object_opts = QTAILQ_HEAD_INITIALIZER(object_opts);
static QTAILQ_HEAD(, DeviceOption) device_opts = QTAILQ_HEAD_INITIALIZER(device_opts);
static int snapshot;
static QemuPluginList plugin_list = QTAILQ_HEAD_INITIALIZER(plugin_list);
static BlockdevOptionsQueue bdo_queue = QSIMPLEQ_HEAD_INITIALIZER(bdo_queue);
static const char *vga_model = NULL;
static DisplayOptions dpy;
static int num_serial_hds;
static Chardev **serial_hds;

static int default_serial = 1;
static int default_parallel = 1;
static int default_floppy = 1;
static int default_cdrom = 1;
static bool auto_create_sdcard = true;
static int default_vga = 1;
static int default_net = 1;

static const struct {
    const char *driver;
    int *flag;
} default_list[] = {
    { .driver = "xen-console",          .flag = &default_serial    },
    { .driver = "isa-serial",           .flag = &default_serial    },
    { .driver = "isa-parallel",         .flag = &default_parallel  },
    { .driver = "isa-fdc",              .flag = &default_floppy    },
    { .driver = "floppy",               .flag = &default_floppy    },
    { .driver = "ide-cd",               .flag = &default_cdrom     },
    { .driver = "ide-hd",               .flag = &default_cdrom     },
    { .driver = "scsi-cd",              .flag = &default_cdrom     },
    { .driver = "scsi-hd",              .flag = &default_cdrom     },
    { .driver = "VGA",                  .flag = &default_vga       },
    { .driver = "isa-vga",              .flag = &default_vga       },
    { .driver = "cirrus-vga",           .flag = &default_vga       },
    { .driver = "isa-cirrus-vga",       .flag = &default_vga       },
    { .driver = "vmware-svga",          .flag = &default_vga       },
    { .driver = "qxl-vga",              .flag = &default_vga       },
    { .driver = "virtio-vga",           .flag = &default_vga       },
    { .driver = "ati-vga",              .flag = &default_vga       },
    { .driver = "vhost-user-vga",       .flag = &default_vga       },
    { .driver = "virtio-vga-gl",        .flag = &default_vga       },
    { .driver = "virtio-vga-rutabaga",  .flag = &default_vga       },
};

static QemuOptsList qemu_rtc_opts = {
    .name = "rtc",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .merge_lists = true,
    .desc = {
        {
            .name = "base",
            .type = QEMU_OPT_STRING,
        },{
            .name = "clock",
            .type = QEMU_OPT_STRING,
        },{
            .name = "driftfix",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_accel_opts = {
    .name = "accel",
    .implied_opt_name = "accel",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_accel_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting accelerator properties
         */
        { }
    },
};

static QemuOptsList qemu_mem_opts = {
    .name = "memory",
    .implied_opt_name = "size",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_mem_opts.head),
    .merge_lists = true,
    .desc = {
        {
            .name = "size",
            .type = QEMU_OPT_SIZE,
        },
        {
            .name = "slots",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "maxmem",
            .type = QEMU_OPT_SIZE,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_icount_opts = {
    .name = "icount",
    .implied_opt_name = "shift",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_icount_opts.head),
    .desc = {
        {
            .name = "shift",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "align",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "sleep",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "rr",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "rrfile",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "rrsnapshot",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static void default_driver_disable(const char *driver)
{
    int i;

    if (!driver) {
        return;
    }

    for (i = 0; i < ARRAY_SIZE(default_list); i++) {
        if (strcmp(default_list[i].driver, driver) != 0)
            continue;
        *(default_list[i].flag) = 0;
    }
}

static int default_driver_check(void *opaque, QemuOpts *opts, Error **errp)
{
    const char *driver = qemu_opt_get(opts, "driver");

    default_driver_disable(driver);
    return 0;
}

static void default_driver_check_json(void)
{
    DeviceOption *opt;

    QTAILQ_FOREACH(opt, &device_opts, next) {
        const char *driver = qdict_get_try_str(opt->opts, "driver");
        default_driver_disable(driver);
    }
}

/***********************************************************/
/* QEMU Block devices */

#define HD_OPTS "media=disk"
#define CDROM_OPTS "media=cdrom"
#define FD_OPTS ""
#define PFLASH_OPTS ""
#define MTD_OPTS ""
#define SD_OPTS ""


static int xemu_check_file(const char *path);
static uint64_t xbox_game_partition_offset(const char *path);

static void configure_blockdev(BlockdevOptionsQueue *bdo_queue,
                               MachineClass *machine_class, int snapshot)
{
    /* The core's own drives (block/libretro-disk.c), from the settings:
     * the hard disk (a raw image, ui/libretro.c) and the DVD drive, empty
     * when there is no disc. A Chihiro has neither: its IDE slave is the
     * media board's interface (chihiro_ide_interface_init). */
    if (!xemu_chihiro_mode()) {
        const char *hdd_path = g_config.sys.files.hdd_path;
        const char *dvd_path = g_config.sys.files.dvd_path;
        Error *err = NULL;

        if (hdd_path[0] && !xemu_check_file(hdd_path)) {
            BlockBackend *hd = lr_disk_new("ide0-hd0", IF_IDE, 0, 0, false,
                                           true);
            if (!lr_disk_open(hd, hdd_path, 0, true, &err)) {
                xemu_queue_error_message(error_get_pretty(err));
                error_free(err);
                err = NULL;
            }
        }
        BlockBackend *cd = lr_disk_new("ide0-cd1", IF_IDE, 0, 1, true, false);
        if (dvd_path[0]) {
            uint64_t offset = xbox_game_partition_offset(dvd_path);
            if (offset) {
                info_report("DVD: Redump image, the game partition from "
                            "offset 0x%" PRIx64, offset);
            }
            if (!lr_disk_open(cd, dvd_path, offset, false, &err)) {
                char *msg = g_strdup_printf("Failed to open DVD image file "
                                            "'%s'. Please check machine "
                                            "settings.", dvd_path);
                warn_report_err(err);
                xemu_queue_error_message(msg);
                g_free(msg);
            } else {
                extern void xemu_xbe_runtime_disc(BlockBackend *blk);
                xemu_xbe_runtime_disc(cd);
            }
        }
    }
}

static void realtime_init(void)
{
    if (should_mlock(mlock_state)) {
        if (os_mlock(is_mlock_on_fault(mlock_state)) < 0) {
            error_report("locking memory failed");
            exit(1);
        }
    }
}



/***********************************************************/
/* USB devices */

static bool usb_parse(const char *cmdline, Error **errp)
{
    g_assert(machine_usb(current_machine));

    if (!usbdevice_create(cmdline)) {
        error_setg(errp, "could not add USB device '%s'", cmdline);
        return false;
    }
    return true;
}

/***********************************************************/
/* machine registration */

static MachineClass *find_machine(const char *name, GSList *machines)
{
    GSList *el;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;

        if (!strcmp(mc->name, name) || !g_strcmp0(mc->alias, name)) {
            return mc;
        }
    }

    return NULL;
}

static MachineClass *find_default_machine(GSList *machines)
{
    GSList *el;
    MachineClass *default_machineclass = NULL;

    for (el = machines; el; el = el->next) {
        MachineClass *mc = el->data;

        if (mc->is_default) {
            assert(default_machineclass == NULL && "Multiple default machines");
            default_machineclass = mc;
        }
    }

    return default_machineclass;
}

typedef struct VGAInterfaceInfo {
    const char *opt_name;    /* option name */
    const char *name;        /* human-readable name */
    /* Class names indicating that support is available.
     * If no class is specified, the interface is always available */
    const char *class_names[2];
} VGAInterfaceInfo;

static const VGAInterfaceInfo vga_interfaces[VGA_TYPE_MAX] = {
    [VGA_NONE] = {
        .opt_name = "none",
        .name = "no graphic card",
    },
    [VGA_STD] = {
        .opt_name = "std",
        .name = "standard VGA",
        .class_names = { "VGA", "isa-vga" },
    },
    [VGA_CIRRUS] = {
        .opt_name = "cirrus",
        .name = "Cirrus VGA",
        .class_names = { "cirrus-vga", "isa-cirrus-vga" },
    },
    [VGA_VMWARE] = {
        .opt_name = "vmware",
        .name = "VMWare SVGA",
        .class_names = { "vmware-svga" },
    },
    [VGA_VIRTIO] = {
        .opt_name = "virtio",
        .name = "Virtio VGA",
        .class_names = { "virtio-vga" },
    },
    [VGA_QXL] = {
        .opt_name = "qxl",
        .name = "QXL VGA",
        .class_names = { "qxl-vga" },
    },
    [VGA_TCX] = {
        .opt_name = "tcx",
        .name = "TCX framebuffer",
        .class_names = { "sun-tcx" },
    },
    [VGA_CG3] = {
        .opt_name = "cg3",
        .name = "CG3 framebuffer",
        .class_names = { "cgthree" },
    },
};

static bool vga_interface_available(VGAInterfaceType t)
{
    const VGAInterfaceInfo *ti = &vga_interfaces[t];

    assert(t < VGA_TYPE_MAX);

    if (!ti->class_names[0] || module_object_class_by_name(ti->class_names[0])) {
        return true;
    }

    if (ti->class_names[1] && module_object_class_by_name(ti->class_names[1])) {
        return true;
    }

    return false;
}

static const char *
get_default_vga_model(const MachineClass *machine_class)
{
    if (machine_class->default_display) {
        for (int t = 0; t < VGA_TYPE_MAX; t++) {
            const VGAInterfaceInfo *ti = &vga_interfaces[t];

            if (ti->opt_name && vga_interface_available(t) &&
                g_str_equal(ti->opt_name, machine_class->default_display)) {
                return machine_class->default_display;
            }
        }

        warn_report_once("Default display '%s' is not available in this binary",
                         machine_class->default_display);
        return NULL;
    } else if (vga_interface_available(VGA_CIRRUS)) {
        return "cirrus";
    } else if (vga_interface_available(VGA_STD)) {
        return "std";
    }

    return NULL;
}

static void select_vgahw(const MachineClass *machine_class, const char *p)
{
    const char *opts;
    int t;

    if (g_str_equal(p, "help")) {
        const char *def = get_default_vga_model(machine_class);

        for (t = 0; t < VGA_TYPE_MAX; t++) {
            const VGAInterfaceInfo *ti = &vga_interfaces[t];

            if (vga_interface_available(t) && ti->opt_name) {
                printf("%-20s %s%s\n", ti->opt_name, ti->name ?: "",
                        (def && g_str_equal(ti->opt_name, def)) ?
                        " (default)" : "");
            }
        }
        exit(0);
    }

    assert(vga_interface_type == VGA_NONE);
    for (t = 0; t < VGA_TYPE_MAX; t++) {
        const VGAInterfaceInfo *ti = &vga_interfaces[t];
        if (ti->opt_name && strstart(p, ti->opt_name, &opts)) {
            if (!vga_interface_available(t)) {
                error_report("%s not available", ti->name);
                exit(1);
            }
            vga_interface_type = t;
            break;
        }
    }
    if (t == VGA_TYPE_MAX) {
    invalid_vga:
        error_report("unknown vga type: %s", p);
        exit(1);
    }
    while (*opts) {
        const char *nextopt;

        if (strstart(opts, ",retrace=", &nextopt)) {
            opts = nextopt;
            if (strstart(opts, "dumb", &nextopt))
                vga_retrace_method = VGA_RETRACE_DUMB;
            else if (strstart(opts, "precise", &nextopt))
                vga_retrace_method = VGA_RETRACE_PRECISE;
            else goto invalid_vga;
        } else goto invalid_vga;
        opts = nextopt;
    }
}

static int device_help_func(void *opaque, QemuOpts *opts, Error **errp)
{
    return qdev_device_help(opts);
}

static int device_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    DeviceState *dev;

    dev = qdev_device_add(opts, errp);
    if (!dev && *errp) {
        error_report_err(*errp);
        return -1;
    } else if (dev) {
        object_unref(OBJECT(dev));
    }
    return 0;
}

static int chardev_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;

    if (!qemu_chr_new_from_opts(opts, NULL, &local_err)) {
        if (local_err) {
            error_propagate(errp, local_err);
            return -1;
        }
        exit(0);
    }
    return 0;
}


struct device_config {
    enum {
        DEV_USB,       /* -usbdevice     */
        DEV_SERIAL,    /* -serial        */
        DEV_PARALLEL,  /* -parallel      */
        DEV_DEBUGCON,  /* -debugcon */
        DEV_SCLP,      /* s390 sclp */
    } type;
    const char *cmdline;
    Location loc;
    QTAILQ_ENTRY(device_config) next;
};

static QTAILQ_HEAD(, device_config) device_configs =
    QTAILQ_HEAD_INITIALIZER(device_configs);

static void add_device_config(int type, const char *cmdline)
{
    struct device_config *conf;

    conf = g_malloc0(sizeof(*conf));
    conf->type = type;
    conf->cmdline = cmdline;
    loc_save(&conf->loc);
    QTAILQ_INSERT_TAIL(&device_configs, conf, next);
}

/**
 * foreach_device_config_or_exit(): process per-device configs
 * @type: device_config type
 * @func: device specific config function, returning pass/fail
 *
 * @func is called with the &error_fatal handler so device specific
 * error messages can be reported on failure.
 */
static void foreach_device_config_or_exit(int type,
                                          bool (*func)(const char *cmdline,
                                                       Error **errp))
{
    struct device_config *conf;

    QTAILQ_FOREACH(conf, &device_configs, next) {
        if (conf->type != type)
            continue;
        loc_push_restore(&conf->loc);
        func(conf->cmdline, &error_fatal);
        loc_pop(&conf->loc);
    }
}

static void qemu_disable_default_devices(void)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(current_machine);

    default_driver_check_json();
    qemu_opts_foreach(qemu_find_opts("device"),
                      default_driver_check, NULL, NULL);

    if (!vga_model && !default_vga) {
        vga_interface_type = VGA_DEVICE;
        vga_interface_created = true;
    }
    if (machine_class->no_serial) {
        default_serial = 0;
    }
    if (machine_class->no_parallel) {
        default_parallel = 0;
    }
    if (machine_class->no_floppy) {
        default_floppy = 0;
    }
    if (machine_class->no_cdrom) {
        default_cdrom = 0;
    }
    if (!machine_class->auto_create_sdcard) {
        auto_create_sdcard = false;
    }
    if (default_net && machine_class->default_nic && !module_object_class_by_name(machine_class->default_nic)) {
        warn_report("Default NIC '%s' is not available in this binary",
                    machine_class->default_nic);
        default_net = 0;
    }
}

static void qemu_setup_display(void)
{
    /* no display: qemu_init sets DISPLAY_TYPE_NONE */
    qemu_display_early_init(&dpy);
}

static void qemu_create_default_devices(void)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(current_machine);
    const char *vc = qemu_display_get_vc(&dpy);

    if (default_serial) {
        add_device_config(DEV_SERIAL, vc ?: "null");
    }
    if (default_parallel) {
        add_device_config(DEV_PARALLEL, vc ?: "null");
    }

    if (default_net) {
        QemuOptsList *net = qemu_find_opts("net");
        qemu_opts_parse(net, "nic", true, &error_abort);
    }

    /* If no default VGA is requested, the default is "none".  */
    if (default_vga) {
        vga_model = get_default_vga_model(machine_class);
    }
    if (vga_model) {
        select_vgahw(machine_class, vga_model);
    }
}

static bool serial_parse(const char *devname, Error **errp)
{
    int index = num_serial_hds;

    serial_hds = g_renew(Chardev *, serial_hds, index + 1);

    if (strcmp(devname, "none") == 0) {
        /* Don't allocate a serial device for this index */
        serial_hds[index] = NULL;
    } else {
        char label[32];
        snprintf(label, sizeof(label), "serial%d", index);

        serial_hds[index] = qemu_chr_new(label, devname, NULL);
        if (!serial_hds[index]) {
            error_setg(errp, "could not connect serial device"
                       " to character backend '%s'", devname);
            return false;
        }
    }
    num_serial_hds++;
    return true;
}

Chardev *serial_hd(int i)
{
    assert(i >= 0);
    if (i < num_serial_hds) {
        return serial_hds[i];
    }
    return NULL;
}

static bool parallel_parse(const char *devname, Error **errp)
{
    static int index = 0;
    char label[32];

    if (strcmp(devname, "none") == 0)
        return true;
    if (index == MAX_PARALLEL_PORTS) {
        error_setg(errp, "too many parallel ports");
        return false;
    }
    snprintf(label, sizeof(label), "parallel%d", index);
    parallel_hds[index] = qemu_chr_new(label, devname, NULL);
    if (!parallel_hds[index]) {
        error_setg(errp, "could not connect parallel device"
                   " to character backend '%s'", devname);
        return false;
    }
    index++;
    return true;
}

static bool debugcon_parse(const char *devname, Error **errp)
{
    QemuOpts *opts;

    if (!qemu_chr_new("debugcon", devname, NULL)) {
        error_setg(errp, "invalid character backend '%s'", devname);
        return false;
    }
    opts = qemu_opts_create(qemu_find_opts("device"), "debugcon", 1, NULL);
    if (!opts) {
        error_setg(errp, "already have a debugcon device");
        return false;
    }
    qemu_opt_set(opts, "driver", "isa-debugcon", &error_abort);
    qemu_opt_set(opts, "chardev", "debugcon", &error_abort);
    return true;
}

struct UnlinkPidfileNotifier {
    Notifier notifier;
    char *pid_file_realpath;
};

static MachineClass *select_machine(QDict *qdict, Error **errp)
{
    ERRP_GUARD();
    const char *machine_type = qdict_get_try_str(qdict, "type");
    g_autoptr(GSList) machines = object_class_get_list(target_machine_typename(),
                                                       false);
    MachineClass *machine_class = NULL;

    if (machine_type) {
        machine_class = find_machine(machine_type, machines);
        if (!machine_class) {
            error_setg(errp, "unsupported machine type: \"%s\"", machine_type);
        }
        qdict_del(qdict, "type");
    } else {
        machine_class = find_default_machine(machines);
        if (!machine_class) {
            error_setg(errp, "No machine specified, and there is no default");
        }
    }

    if (!machine_class) {
        error_append_hint(errp,
                          "Use -machine help to list supported machines\n");
    }
    return machine_class;
}

static int object_parse_property_opt(Object *obj,
                                     const char *name, const char *value,
                                     const char *skip, Error **errp)
{
    if (g_str_equal(name, skip)) {
        return 0;
    }

    if (!object_property_parse(obj, name, value, errp)) {
        return -1;
    }

    return 0;
}

/* *Non*recursively replace underscores with dashes in QDict keys.  */
static void keyval_dashify(QDict *qdict, Error **errp)
{
    const QDictEntry *ent, *next;
    char *p;

    for (ent = qdict_first(qdict); ent; ent = next) {
        g_autofree char *new_key = NULL;

        next = qdict_next(qdict, ent);
        if (!strchr(ent->key, '_')) {
            continue;
        }
        new_key = g_strdup(ent->key);
        for (p = new_key; *p; p++) {
            if (*p == '_') {
                *p = '-';
            }
        }
        if (qdict_haskey(qdict, new_key)) {
            error_setg(errp, "Conflict between '%s' and '%s'", ent->key, new_key);
            return;
        }
        qobject_ref(ent->value);
        qdict_put_obj(qdict, new_key, ent->value);
        qdict_del(qdict, ent->key);
    }
}

static void qemu_apply_legacy_machine_options(QDict *qdict)
{
    const char *value;
    QObject *prop;

    keyval_dashify(qdict, &error_fatal);

    /* Legacy options do not correspond to MachineState properties.  */
    value = qdict_get_try_str(qdict, "accel");
    if (value) {
        accelerators = g_strdup(value);
        qdict_del(qdict, "accel");
    }

    value = qdict_get_try_str(qdict, "igd-passthru");
    if (value) {
        object_register_sugar_prop(ACCEL_CLASS_NAME("xen"), "igd-passthru", value,
                                   false);
        qdict_del(qdict, "igd-passthru");
    }

    value = qdict_get_try_str(qdict, "kvm-shadow-mem");
    if (value) {
        object_register_sugar_prop(ACCEL_CLASS_NAME("kvm"), "kvm-shadow-mem", value,
                                   false);
        qdict_del(qdict, "kvm-shadow-mem");
    }

    value = qdict_get_try_str(qdict, "kernel-irqchip");
    if (value) {
        object_register_sugar_prop(ACCEL_CLASS_NAME("kvm"), "kernel-irqchip", value,
                                   false);
        object_register_sugar_prop(ACCEL_CLASS_NAME("whpx"), "kernel-irqchip", value,
                                   false);
        qdict_del(qdict, "kernel-irqchip");
    }

    value = qdict_get_try_str(qdict, "memory-backend");
    if (value) {
        /* Resolved later.  */
        ram_memdev_id = g_strdup(value);
        qdict_del(qdict, "memory-backend");
    }

    prop = qdict_get(qdict, "memory");
    if (prop) {
        have_custom_ram_size =
            qobject_type(prop) == QTYPE_QDICT &&
            qdict_haskey(qobject_to(QDict, prop), "size");
    }
}

static void object_option_foreach_add(bool (*type_opt_predicate)(const char *))
{
    ObjectOption *opt, *next;

    QTAILQ_FOREACH_SAFE(opt, &object_opts, next, next) {
        const char *type = ObjectType_str(opt->opts->qom_type);
        if (type_opt_predicate(type)) {
            user_creatable_add_qapi(opt->opts, &error_fatal);
            qapi_free_ObjectOptions(opt->opts);
            QTAILQ_REMOVE(&object_opts, opt, next);
            g_free(opt);
        }
    }
}

/*
 * Initial object creation happens before all other
 * QEMU data types are created. The majority of objects
 * can be created at this point. The rng-egd object
 * cannot be created here, as it depends on the chardev
 * already existing.
 */
static bool object_create_early(const char *type)
{
    /*
     * Objects should not be made "delayed" without a reason.  If you
     * add one, state the reason in a comment!
     */

    /* Reason: property "chardev" */
    if (g_str_equal(type, "rng-egd")) {
        return false;
    }


    /* Reason: vhost-user-blk-server property "node-name" */
    if (g_str_equal(type, "vhost-user-blk-server")) {
        return false;
    }
    /*
     * Reason: filter-* property "netdev" etc.
     */
    if (g_str_equal(type, "filter-buffer") ||
        g_str_equal(type, "filter-dump") ||
        g_str_equal(type, "filter-mirror") ||
        g_str_equal(type, "filter-redirector") ||
        g_str_equal(type, "colo-compare") ||
        g_str_equal(type, "filter-rewriter")) {
        return false;
    }

    /*
     * Allocation of large amounts of memory may delay
     * chardev initialization for too long, and trigger timeouts
     * on software that waits for a monitor socket to be created
     */
    if (g_str_has_prefix(type, "memory-backend-")) {
        return false;
    }

    return true;
}

static void qemu_apply_machine_options(QDict *qdict)
{
    object_set_properties_from_keyval(OBJECT(current_machine), qdict, false,
                                      &error_fatal);

    if (semihosting_enabled(false) && !semihosting_get_argc()) {
        /* fall back to the -kernel/-append */
        semihosting_arg_fallback(current_machine->kernel_filename, current_machine->kernel_cmdline);
    }
}

static char *strdup_double_commas(const char *input);

/* Earlier builds kept the Chihiro's snapshot store, chihiro_snapshots.qcow2,
 * in the hard disk setting; an Xbox must not boot from it. (The store held
 * savevm's snapshots, which the core does not take: its states are its own,
 * ui/libretro-state.c. It is no longer made.) */
static void chihiro_snapshot_store_setup(void)
{
    char *hdd_name = g_path_get_basename(g_config.sys.files.hdd_path);
    if (strcmp(hdd_name, "chihiro_snapshots.qcow2") == 0) {
        xemu_settings_set_string(&g_config.sys.files.hdd_path, "");
    }
    g_free(hdd_name);
}

static void qemu_create_early_backends(void)
{
    chihiro_snapshot_store_setup();
    MachineClass *machine_class = MACHINE_GET_CLASS(current_machine);
    const bool use_sdl = false;
    const bool use_gtk = false;

    if (dpy.has_window_close && !use_gtk && !use_sdl) {
        error_report("window-close is only valid for GTK and SDL, "
                     "ignoring option");
    }

    qemu_console_early_init();

    if (dpy.has_gl && dpy.gl != DISPLAY_GL_MODE_OFF && display_opengl == 0) {
#if defined(CONFIG_OPENGL)
        error_report("OpenGL is not supported by display backend '%s'",
                     DisplayType_str(dpy.type));
#else
        error_report("OpenGL support was not enabled in this build of QEMU");
#endif
        exit(1);
    }

    object_option_foreach_add(object_create_early);

    /* spice needs the timers to be initialized by this point */
    /* spice must initialize before chardevs (for spicevmc and spiceport) */
    qemu_spice.init();

    qemu_opts_foreach(qemu_find_opts("chardev"),
                      chardev_init_func, NULL, &error_fatal);

    /*
     * Note: we need to create block backends before
     * setting machine properties, so they can be referred to.
     */
    configure_blockdev(&bdo_queue, machine_class, snapshot);
}


/*
 * The remainder of object creation happens after the
 * creation of chardev, fsdev, net clients and device data types.
 */
static bool object_create_late(const char *type)
{
    return !object_create_early(type);
}

static void qemu_create_late_backends(void)
{
    net_init_clients();

    object_option_foreach_add(object_create_late);

    if (tpm_init() < 0) {
        exit(1);
    }

    foreach_device_config_or_exit(DEV_SERIAL, serial_parse);
    foreach_device_config_or_exit(DEV_PARALLEL, parallel_parse);
    foreach_device_config_or_exit(DEV_DEBUGCON, debugcon_parse);

    /* now chardevs have been created we may have semihosting to connect */
    qemu_semihosting_chardev_init();
}

static void qemu_resolve_machine_memdev(void)
{
    if (ram_memdev_id) {
        Object *backend;
        ram_addr_t backend_size;

        backend = object_resolve_path_type(ram_memdev_id,
                                           TYPE_MEMORY_BACKEND, NULL);
        if (!backend) {
            error_report("Memory backend '%s' not found", ram_memdev_id);
            exit(EXIT_FAILURE);
        }
        if (!have_custom_ram_size) {
            backend_size = object_property_get_uint(backend, "size",  &error_abort);
            current_machine->ram_size = backend_size;
        }
        object_property_set_link(OBJECT(current_machine),
                                 "memory-backend", backend, &error_fatal);
    }
}

static void parse_memory_options(void)
{
    QemuOpts *opts = qemu_find_opts_singleton("memory");
    QDict *dict, *prop;
    const char *mem_str;
    Location loc;

    loc_push_none(&loc);
    qemu_opts_loc_restore(opts);

    prop = qdict_new();

    if (qemu_opt_get_size(opts, "size", 0) != 0) {
        /* Fix up legacy suffix-less format */
        mem_str = qemu_opt_get(opts, "size");
        if (g_ascii_isdigit(mem_str[strlen(mem_str) - 1])) {
            g_autofree char *mib_str = g_strdup_printf("%sM", mem_str);
            qdict_put_str(prop, "size", mib_str);
        } else {
            qdict_put_str(prop, "size", mem_str);
        }
    }

    if (qemu_opt_get(opts, "maxmem")) {
        qdict_put_str(prop, "max-size", qemu_opt_get(opts, "maxmem"));
    }
    if (qemu_opt_get(opts, "slots")) {
        qdict_put_str(prop, "slots", qemu_opt_get(opts, "slots"));
    }

    dict = qdict_new();
    qdict_put(dict, "memory", prop);
    keyval_merge(machine_opts_dict, dict, &error_fatal);
    qobject_unref(dict);
    loc_pop(&loc);
}

static void qemu_create_machine_containers(Object *machine)
{
    static const char *const containers[] = {
        "unattached",
        "peripheral",
        "peripheral-anon",
    };

    for (unsigned i = 0; i < ARRAY_SIZE(containers); i++) {
        object_property_add_new_container(machine, containers[i]);
    }
}

static void qemu_create_machine(QDict *qdict)
{
    MachineClass *machine_class = select_machine(qdict, &error_fatal);
    object_set_machine_compat_props(machine_class->compat_props);

    current_machine = MACHINE(object_new_with_class(OBJECT_CLASS(machine_class)));
    object_property_add_child(object_get_root(), "machine",
                              OBJECT(current_machine));
    qemu_create_machine_containers(OBJECT(current_machine));
    object_property_add_child(machine_get_container("unattached"),
                              "sysbus", OBJECT(sysbus_get_default()));

    if (machine_class->minimum_page_bits) {
        if (!set_preferred_target_page_bits(machine_class->minimum_page_bits)) {
            /* This would be a board error: specifying a minimum smaller than
             * a target's compile-time fixed setting.
             */
            g_assert_not_reached();
        }
    }

    cpu_exec_init_all();

    if (machine_class->hw_version) {
        qemu_set_hw_version(machine_class->hw_version);
    }

    /*
     * Get the default machine options from the machine if they are not
     * already given (qemu_init's machine options).
     */
    if (machine_class->default_machine_opts) {
        QDict *default_opts =
            keyval_parse(machine_class->default_machine_opts, NULL, NULL,
                         &error_abort);
        qemu_apply_legacy_machine_options(default_opts);
        object_set_properties_from_keyval(OBJECT(current_machine), default_opts,
                                          false, &error_abort);
        qobject_unref(default_opts);
    }
}

static int do_configure_icount(void *opaque, QemuOpts *opts, Error **errp)
{
    return !icount_configure(opts, errp);
}

static int accelerator_set_property(void *opaque,
                                const char *name, const char *value,
                                Error **errp)
{
    return object_parse_property_opt(opaque, name, value, "accel", errp);
}

static int do_configure_accelerator(void *opaque, QemuOpts *opts, Error **errp)
{
    bool *p_init_failed = opaque;
    const char *acc = qemu_opt_get(opts, "accel");
    AccelClass *ac = accel_find(acc);
    AccelState *accel;
    int ret;

    if (!acc) {
        error_setg(errp, QERR_MISSING_PARAMETER, "accel");
        goto bad;
    }

    if (!ac) {
        error_report("invalid accelerator %s", acc);
        goto bad;
    }
    accel = ACCEL(object_new_with_class(OBJECT_CLASS(ac)));
    object_apply_compat_props(OBJECT(accel));
    qemu_opt_foreach(opts, accelerator_set_property,
                     accel,
                     &error_fatal);

    ret = accel_init_machine(accel, current_machine);
    if (ret < 0) {
        error_report("failed to initialize %s: %s", acc, strerror(-ret));
        goto bad;
    }

    return 1;

bad:
    *p_init_failed = true;
    return 0;
}

static void configure_accelerators(const char *progname)
{
    bool init_failed = false;

    qemu_opts_foreach(qemu_find_opts("icount"),
                      do_configure_icount, NULL, &error_fatal);

    if (QTAILQ_EMPTY(&qemu_accel_opts.head)) {
        char **accel_list, **tmp;

        if (accelerators == NULL) {
            /* Select the default accelerator */
            bool have_tcg = accel_find("tcg");
            bool have_kvm = accel_find("kvm");
            bool have_hvf = accel_find("hvf");

            if (have_tcg && have_kvm) {
                if (g_str_has_suffix(progname, "kvm")) {
                    /* If the program name ends with "kvm", we prefer KVM */
                    accelerators = "kvm:tcg";
                } else {
                    accelerators = "tcg:kvm";
                }
            } else if (have_kvm) {
                accelerators = "kvm";
            } else if (have_tcg) {
                accelerators = "tcg";
            } else if (have_hvf) {
                accelerators = "hvf";
            } else {
                error_report("No accelerator selected and"
                             " no default accelerator available");
                exit(1);
            }
        }
        accel_list = g_strsplit(accelerators, ":", 0);

        for (tmp = accel_list; *tmp; tmp++) {
            /*
             * Filter invalid accelerators here, to prevent obscenities
             * such as "-machine accel=tcg,,thread=single".
             */
            if (accel_find(*tmp)) {
                qemu_opts_parse_noisily(qemu_find_opts("accel"), *tmp, true);
            } else {
                init_failed = true;
                error_report("invalid accelerator %s", *tmp);
            }
        }
        g_strfreev(accel_list);
    } else {
        if (accelerators != NULL) {
            error_report("The -accel and \"-machine accel=\" options are incompatible");
            exit(1);
        }
    }

    if (!qemu_opts_foreach(qemu_find_opts("accel"),
                           do_configure_accelerator, &init_failed, &error_fatal)) {
        if (!init_failed) {
            error_report("no accelerator found");
        }
        exit(1);
    }

    if (init_failed) {
        error_report("falling back to %s", current_accel_name());
    }

    if (icount_enabled() && !tcg_enabled()) {
        error_report("-icount is not allowed with hardware virtualization");
        exit(1);
    }
}

/* Return 1 if file fails to open */
static int xemu_check_file(const char *path)
{
    FILE *fd = qemu_fopen(path, "rb");
    if (fd == NULL) {
        return 1;
    }
    fclose(fd);
    return 0;
}

// Duplicate commas to escape them
static char *strdup_double_commas(const char *input) {
    size_t length = 0;
    for (const char *i = input; *i; i++) {
        if (*i == ',') {
            length++;
        }
        length++;
    }
    char *output = malloc(length + 1);
    const char *in = input;
    char *out = output;
    while (*in) {
        if (*in == ',') {
                *(out++) = ',';
        }
        *(out++) = *(in++);
    }
    output[length] = 0;
    return output;
}

#include <libretro.h>
#include "block/libretro-vfs-read.h"

/*
 * Where the game partition starts in a disc image. An XISO is the game
 * partition alone; a Redump image is the whole disc, with the video partition
 * first and the game partition at a fixed offset for each disc type (XGD1,
 * XGD2, XGD3). The drive shows the guest only the game partition, as a real
 * Xbox drive does, so the image is attached from that offset. Found by the
 * XDVDFS volume descriptor, 32 sectors into the partition.
 */
/* ui/libretro.c: the frontend's VFS, if it has one - a saf:// or content://
 * path (Android's Play Store RetroArch) can only be read through it */
extern struct retro_vfs_interface *xemu_libretro_vfs;

static const void *xbox_read_at(LRFile *f, struct retro_vfs_file_handle *h,
                                uint64_t offset, void *buf, size_t len)
{
    if (offset > INT64_MAX) {
        return NULL;
    }
    if (h) {
        int64_t cursor = -1;
        LRFileIOV iov = { buf, len };
        return lr_vfs_readv(xemu_libretro_vfs, h, &cursor, offset, &iov, 1) == 0
               ? buf : NULL;
    }
    const uint8_t *view = lr_file_view(f, offset, len);
    return view ? view : (lr_file_pread(f, offset, buf, len) == 0 ? buf : NULL);
}

static uint64_t xbox_game_partition_offset(const char *path)
{
    static const uint64_t offsets[] = {
        0, 0x18300000 /* XGD1 */, 0xFD90000 /* XGD2 */, 0x2080000 /* XGD3 */,
    };
    static const char magic[] = "MICROSOFT*XBOX*MEDIA";
    uint64_t found = 0;
    LRFile *f = NULL;
    struct retro_vfs_file_handle *h = NULL;

    if (!path || !path[0]) {
        return 0;
    }
    if (strstr(path, "://")) {
        if (!xemu_libretro_vfs) {
            return 0;
        }
        h = xemu_libretro_vfs->open(path, RETRO_VFS_FILE_ACCESS_READ,
                                    RETRO_VFS_FILE_ACCESS_HINT_NONE);
        if (!h) {
            return 0;
        }
    } else {
        f = lr_file_open(path, false, false);
        if (!f) {
            return 0;
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(offsets); i++) {
        char buf[sizeof(magic) - 1];
        const void *view = xbox_read_at(f, h, offsets[i] + 32 * 2048,
                                        buf, sizeof(buf));
        if (view && memcmp(view, magic, sizeof(buf)) == 0) {
            found = offsets[i];
            break;
        }
    }
    if (h) {
        xemu_libretro_vfs->close(h);
    } else {
        lr_file_close(f);
    }
    return found;
}

/* Another disc into the built machine's drive, for the content loaded after
 * the first (ui/libretro.c): upstream's xemu_load_disc, with the game
 * partition's offset of a Redump image as at start. An empty path leaves the
 * tray empty. Under the BQL. */
void xemu_libretro_change_disc(const char *path, Error **errp)
{
    BlockBackend *blk = blk_by_name("ide0-cd1");
    uint64_t offset = 0;

    if (!blk) {
        error_setg(errp, "this machine has no DVD drive");
        return;
    }
    if (path && path[0]) {
        offset = xbox_game_partition_offset(path);
        if (offset) {
            info_report("DVD: Redump image, the game partition from offset "
                        "0x%" PRIx64, offset);
        }
    }
    /* a disc that does not open leaves the one in the drive */
    if (lr_disk_change(blk, path, offset, errp)) {
        extern void xemu_xbe_runtime_disc(BlockBackend *blk);
        xemu_xbe_runtime_disc(path && path[0] ? blk : NULL);
        xemu_settings_set_string(&g_config.sys.files.dvd_path,
                                 path ? path : "");
    }
    xbox_smc_update_tray_state();
}

static void qemu_validate_options(const QDict *machine_opts)
{
    const char *kernel_filename = qdict_get_try_str(machine_opts, "kernel");
    const char *shim_filename = qdict_get_try_str(machine_opts, "shim");
    const char *initrd_filename = qdict_get_try_str(machine_opts, "initrd");
    const char *kernel_cmdline = qdict_get_try_str(machine_opts, "append");

    if (kernel_filename == NULL) {
        if (kernel_cmdline != NULL) {
            error_report("-append only allowed with -kernel option");
            exit(1);
        }

        if (shim_filename != NULL) {
            error_report("-shim only allowed with -kernel option");
            exit(1);
        }

        if (initrd_filename != NULL) {
            error_report("-initrd only allowed with -kernel option");
            exit(1);
        }
    }

}

static void qemu_process_early_options(void)
{
    /* No log file and no log mask */
    qemu_set_log_filename_flags(NULL, 0, &error_fatal);

    qemu_add_default_firmwarepath();
}

static void qemu_process_help_options(void)
{

    if (qemu_opts_foreach(qemu_find_opts("device"),
                          device_help_func, NULL, NULL)) {
        exit(0);
    }
}

static void qemu_maybe_daemonize(void)
{
    os_daemonize();
    rcu_disable_atfork();
}

static void qemu_init_displays(void)
{
    DisplayState *ds;

    /* init local displays */
    ds = init_displaystate();
    qemu_display_init(ds, &dpy);

    /* must be after terminal init, SDL library changes signal handlers */
    os_setup_signal_handling();

    if (using_spice) {
        qemu_spice.display_init();
    }
}

static void qemu_init_board(void)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(current_machine);

    /* process plugin before CPUs are created, but once -smp has been parsed */
    qemu_plugin_load_list(&plugin_list, &error_fatal);

    /* From here on we enter MACHINE_PHASE_INITIALIZED.  */
    machine_run_board_init(current_machine, NULL, &error_fatal);

    if (machine_class->auto_create_sdcard) {
        bool ambigous;

        /* Ensure there is a SD bus available to create SD card on */
        Object *obj = object_resolve_path_type("", TYPE_SD_BUS, &ambigous);
        if (!obj && !ambigous) {
            fprintf(stderr, "Can not create sd-card on '%s' machine"
                            " because it lacks a sd-bus\n",
                            machine_class->name);
            abort();
        }
    }

    drive_check_orphaned();

    realtime_init();
}

static void qemu_create_cli_devices(void)
{
    DeviceOption *opt;

    /* init USB devices */
    if (machine_usb(current_machine)) {
        foreach_device_config_or_exit(DEV_USB, usb_parse);
    }

    /* init generic devices */
    qemu_opts_foreach(qemu_find_opts("device"),
                      device_init_func, NULL, &error_fatal);
    QTAILQ_FOREACH(opt, &device_opts, next) {
        QObject *ret_data = NULL;

        loc_push_restore(&opt->loc);
        qmp_device_add(opt->opts, &ret_data, &error_fatal);
        assert(ret_data == NULL); /* error_fatal aborts */
        loc_pop(&opt->loc);
    }
}

static bool qemu_machine_creation_done(Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    /* Did we create any drives that we failed to create a device for? */
    drive_check_orphaned();

    /* Don't warn about the default network setup that you get if
     * no -net or -netdev options are given. There
     * are two cases that we would otherwise complain about:
     * (1) board doesn't support a NIC but the implicit "-net nic"
     * requested one
     * (2) CONFIG_SLIRP not set, in which case the implicit "-net nic"
     * sets up a nic that isn't connected to anything.
     */
    if (!default_net) {
        net_check_clients();
    }

    qdev_prop_check_globals();

    qdev_machine_creation_done();

    if (machine->cgs && !machine->cgs->ready) {
        error_setg(errp, "accelerator does not support confidential guest %s",
                   object_get_typename(OBJECT(machine->cgs)));
        exit(1);
    }

    if (!vga_interface_created && !default_vga &&
        vga_interface_type != VGA_NONE) {
        warn_report("A -vga option was passed but this machine "
                    "type does not use that option; "
                    "No VGA device has been created");
    }
    return true;
}

static void qemu_start_machine(Error **errp)
{
    qemu_init_board();
    qemu_create_cli_devices();

    if (g_config.net.enable) {
        xemu_net_enable();
    } else {
        xemu_net_disable();
    }
    xemu_link_enable();

    if (!qemu_machine_creation_done(errp)) {
        return;
    }

    if (autostart) {
        /* not QMP's cont: at this point nothing has migrated in or stopped
         * for a reset, so what cont adds is nothing */
        vm_start();
    }
}

static const char *get_eeprom_path(void)
{
    /* Each machine has its own EEPROM: the Xbox's is the configured file
     * with a retail key; the Chihiro's carries the debug key its BIOS
     * expects and lives in the data directory like a save. Both are
     * written back when the machine changes them. */
    bool is_chihiro = xemu_chihiro_mode();
    XboxEEPROMVersion needed = is_chihiro ? XBOX_EEPROM_VERSION_D
                                          : XBOX_EEPROM_VERSION_R1;
    static char *chihiro_path;
    const char *path;
    if (is_chihiro) {
        if (!chihiro_path) {
            chihiro_path = g_strdup_printf("%schihiro_eeprom.bin",
                                           xemu_settings_get_base_path());
        }
        path = chihiro_path;
    } else {
        if (strlen(g_config.sys.files.eeprom_path) == 0) {
            xemu_settings_set_string(&g_config.sys.files.eeprom_path,
                                     xemu_settings_get_default_eeprom_path());
        }
        path = g_config.sys.files.eeprom_path;
    }

    /* The kernel decrypts the EEPROM with its own key: a Chihiro (debug)
     * BIOS needs a debug EEPROM, an Xbox BIOS a retail one. One made for
     * the other machine is set aside and regenerated. */
    if (qemu_access(path, F_OK) == 0) {
        FILE *f = qemu_fopen(path, "rb");
        if (f) {
            uint8_t data[256];
            bool valid = fread(data, 1, 256, f) == 256;
            fclose(f);
            bool debug = xbox_eeprom_detect_version(data) == XBOX_EEPROM_VERSION_D;
            if (valid && debug != is_chihiro) {
                /* Kept under a free name: the user's file is never
                 * deleted, nor an older copy overwritten. */
                const char *kind = debug ? "debug" : "retail";
                char *aside = g_strdup_printf("%s.%s", path, kind);
                for (int n = 1; n < 100 && qemu_access(aside, F_OK) == 0; n++) {
                    g_free(aside);
                    aside = g_strdup_printf("%s.%s.%d", path, kind, n);
                }
                if (qemu_access(aside, F_OK) != 0 && g_rename(path, aside) == 0) {
                    printf("EEPROM '%s' has the %s key, kept as '%s' and "
                           "regenerated\n", path, kind, aside);
                } else {
                    char *msg = g_strdup_printf(
                        "EEPROM '%s' has the %s key and could not be set "
                        "aside.\n\nMove it away and restart xemu.", path, kind);
                    xemu_queue_error_message(msg);
                    g_free(msg);
                }
                g_free(aside);
            }
        }
    }

    if (qemu_access(path, F_OK) == -1) {
        if (!xbox_eeprom_generate(path, needed)) {
            char *msg = g_strdup_printf("Failed to generate EEPROM file '%s'."
                                        "\n\nPlease check machine settings.",
                                        path);
            xemu_queue_error_message(msg);
            g_free(msg);
            return NULL;
        }
    }

    int size = get_image_size(path, NULL);

    if (size < 0) {
        char *msg = g_strdup_printf("Failed to open EEPROM file '%s'.\n\n"
                                    "Please check machine settings.", path);
        xemu_queue_error_message(msg);
        g_free(msg);
        return NULL;
    }

    if (size != 256) {
        char *msg = g_strdup_printf(
            "Invalid EEPROM file '%s' size of %d; should be 256 bytes.\n\n"
            "Please check machine settings.", path, size);
        xemu_queue_error_message(msg);
        g_free(msg);
        return NULL;
    }

    return path;
}

void qemu_init(void)
{
    QemuOpts *icount_opts = NULL;
    MachineClass *machine_class;

/*****************************************************************************/

    // init earlier because it's needed for eeprom generation
    qcrypto_init(&error_fatal);

    /*
     * The machine standalone xemu's launcher puts on QEMU's command line,
     * here handed to what those options end in: the machine's properties,
     * its firmware and RAM size, the EEPROM and USB hub devices, no
     * display, a null serial port, and precise icount at the Xbox CPU's
     * pace. The core has no command line of its own.
     */
    char *machine_arg;
    char *eeprom_device = NULL;
    const char *firmware = NULL;

    /* The machine asked for on the command line, else the loaded image's
     * (xemu_chihiro_mode), remembered for a launch with no image. */
    bool chihiro_machine = xemu_chihiro_mode();
    g_config.sys.last_machine = chihiro_machine ? CONFIG_SYS_LAST_MACHINE_CHIHIRO
                                                : CONFIG_SYS_LAST_MACHINE_XBOX;

    char *bootrom_arg = NULL;
    const char *bootrom_path = g_config.sys.files.bootrom_path;

    /* CHIHIRO (not upstream): the Chihiro BIOS is an MCPX X2 image with its
     * boot ROM built in, which xemu boots as is, like a debug BIOS. The MCPX
     * ROM configured for the retail Xbox is not handed to that machine. */
    if (!chihiro_machine && strlen(bootrom_path) > 0) {
        int bootrom_size = get_image_size(bootrom_path, NULL);
        if (bootrom_size < 0) {
            char *msg = g_strdup_printf("Failed to open BootROM file '%s'. "
                                        "Please check machine settings.",
                                        bootrom_path);
            xemu_queue_error_message(msg);
            g_free(msg);
            bootrom_path = "";
        } else if (bootrom_size != 512) {
            char *msg = g_strdup_printf("Invalid BootROM file '%s' size %d. "
                                        "Expected 512 bytes. Please check "
                                        "machine settings.",
                                        bootrom_path, bootrom_size);
            xemu_queue_error_message(msg);
            g_free(msg);
            bootrom_path = "";
        } else {
            char *escaped_bootrom_path = strdup_double_commas(bootrom_path);
            bootrom_arg = g_strdup_printf(",bootrom=%s", escaped_bootrom_path);
            free(escaped_bootrom_path);
        }
    }

    const char *avpack_str = (const char *[]){
        "scart",
        "hdtv",
        "vga",
        "rfu",
        "svideo",
        "composite",
        "none",
    }[g_config.sys.avpack];

    machine_arg = g_strdup_printf("xbox%s%s%s,avpack=%s",
        (bootrom_arg != NULL) ? bootrom_arg : "",
        chihiro_machine ? ",chihiro=on" : "",
        g_config.general.skip_boot_anim ? ",short-animation=on" : "",
        avpack_str
        );

    if (bootrom_arg != NULL) {
        g_free(bootrom_arg);
    }

    const char *eeprom_path = get_eeprom_path();
    if (eeprom_path) {
        char *escaped_eeprom_path = strdup_double_commas(eeprom_path);
        eeprom_device = g_strdup_printf("smbus-storage,file=%s",
                                        escaped_eeprom_path);
        free(escaped_eeprom_path);
    } else {
        autostart = 0;
    }

    const char *flashrom_path = g_config.sys.files.flashrom_path;
    bool chihiro_bios_ok = false;
    /* A Chihiro only boots its own BIOS (Settings > System > Chihiro Files),
     * never an Xbox one. Its other files, if not set, are taken from the BIOS
     * folder. */
    if (chihiro_machine) {
        flashrom_path = g_config.chihiro.roms.bios_path ?: "";
        chihiro_bios_ok = flashrom_path[0] &&
                          !xemu_check_file(flashrom_path) &&
                          chihiro_bios_known(flashrom_path);
    }
    if (g_config.general.show_welcome) {
        // Don't display an error if this is the first boot. Give user a chance
        // to configure the path.
        autostart = 0;
    } else if (chihiro_machine && !chihiro_bios_ok) {
        autostart = 0; /* reported below with the other Chihiro files */
    } else if (xemu_check_file(flashrom_path)) {
        char *msg = g_strdup_printf("Failed to open flash file '%s'. Please check machine settings.", flashrom_path);
        xemu_queue_error_message(msg);
        g_free(msg);
        autostart = 0;
    } else {
        firmware = flashrom_path;
    }

    /* A Chihiro has 128 MiB; the Xbox has what the setting says. */
    int mem = chihiro_machine ? 128 : ((int)g_config.sys.mem_limit + 1) * 64;

    /* Chihiro: the QC and SC USB devices are realized from their EEPROM
     * dumps and a missing dump aborts the whole process from inside the
     * machine build. Load the media board files here, where a missing one
     * becomes a message and a paused machine like a missing BIOS. */
    if (chihiro_machine) {
        chihiro_load_flash_rom(flashrom_path);
        chihiro_load_eeproms(flashrom_path);
        GString *missing = g_string_new(NULL);
        if (!flashrom_path[0] || xemu_check_file(flashrom_path)) {
            g_string_append(missing, "\n- BIOS (chihiro_xbox_bios.bin)");
        }
        if (!chihiro_flash_rom_loaded()) {
            g_string_append(missing, "\n- media board flash (fpr21042_m29w160et.bin)");
        }
        if (!chihiro_ic10_data) {
            g_string_append(missing, "\n- QC EEPROM (ic10_g24lc64.bin)");
        }
        if (!chihiro_pc20_data) {
            g_string_append(missing, "\n- SC EEPROM (pc20_g24lc64.bin)");
        }
        if (!chihiro_ic11_data) {
            g_string_append(missing, "\n- baseboard EEPROM (ic11_24lc024.bin)");
        }
        if (missing->len) {
            char *msg = g_strdup_printf(
                "Chihiro files not found:%s\n\nSet them in Settings > System "
                "> Chihiro Files (or put them next to the BIOS) and restart.",
                missing->str);
            xemu_queue_error_message(msg);
            g_free(msg);
            autostart = 0;
        } else if (!chihiro_bios_ok) {
            char *msg = g_strdup_printf(
                "'%s' is not a Chihiro BIOS.\n\nSelect chihiro_xbox_bios.bin "
                "(from MAME's chihiro set) in Settings > System > Chihiro "
                "Files, then restart.",
                flashrom_path);
            xemu_queue_error_message(msg);
            g_free(msg);
        }
        g_string_free(missing, TRUE);
    }

    if (!chihiro_machine && strlen(g_config.sys.files.hdd_path) > 0 &&
        xemu_check_file(g_config.sys.files.hdd_path)) {
        char *msg = g_strdup_printf("Failed to open hard disk image file '%s'. Please check machine settings.", g_config.sys.files.hdd_path);
        xemu_queue_error_message(msg);
        g_free(msg);
    }
    /* The drives themselves: configure_blockdev, the core's own (a Chihiro
     * has no DVD drive: its IDE slave is the media board's interface,
     * chihiro_ide_interface_init) */

/*****************************************************************************/


    qemu_add_opts(&qemu_chardev_opts);
    qemu_add_opts(&qemu_device_opts);
    qemu_add_opts(&qemu_netdev_opts);
    qemu_add_opts(&qemu_nic_opts);
    qemu_add_opts(&qemu_net_opts);
    qemu_add_opts(&qemu_rtc_opts);
    qemu_add_opts(&qemu_accel_opts);
    qemu_add_opts(&qemu_mem_opts);
    qemu_add_opts(&qemu_numa_opts);
    qemu_add_opts(&qemu_icount_opts);

    error_init("xemu");
    qemu_init_exec_dir("xemu");

    os_setup_limits();


    qemu_init_subsystems();

    machine_opts_dict = qdict_new();

    /* -machine xbox,... */
    keyval_parse_into(machine_opts_dict, machine_arg, "type", NULL,
                      &error_fatal);
    g_free(machine_arg);
    /* -bios */
    if (firmware) {
        qdict_put_str(machine_opts_dict, "firmware", firmware);
    }
    /* -m: a Chihiro has 128 MiB; the Xbox has what the setting says */
    {
        char *size = g_strdup_printf("%d", mem);
        if (!qemu_opts_parse_noisily(qemu_find_opts("memory"), size, true)) {
            exit(1);
        }
        g_free(size);
    }
    /* -device smbus-storage,file=...: the EEPROM */
    if (eeprom_device) {
        if (!qemu_opts_parse_noisily(qemu_find_opts("device"), eeprom_device,
                                     true)) {
            exit(1);
        }
        g_free(eeprom_device);
    }
    /* -display none */
    dpy.type = DISPLAY_TYPE_NONE;
    /* -device usb-hub,port=1,ports=4: the USB daughterboard of a 1.0 Xbox,
     * on port 1 of the root hub. A Chihiro's baseboard USB devices use
     * these ports instead. */
    if (!chihiro_machine &&
        !qemu_opts_parse_noisily(qemu_find_opts("device"),
                                 "usb-hub,port=1,ports=4", true)) {
        exit(1);
    }
    /* -device lpc47m157: the SuperIO */
    if (!qemu_opts_parse_noisily(qemu_find_opts("device"), "lpc47m157",
                                 true)) {
        exit(1);
    }
    /* -serial null */
    add_device_config(DEV_SERIAL, "null");
    default_serial = 0;
    /* -icount shift=0,sleep=off: the guest's time is its instructions,
     * never the host's: precise icount (at the Xbox CPU's pace,
     * accel/tcg/icount-common.c), and sleep=off, so an idle guest's time
     * moves on at once */
    icount_opts = qemu_opts_parse_noisily(qemu_find_opts("icount"),
                                          "shift=0,sleep=off", true);
    if (!icount_opts) {
        exit(1);
    }

    qemu_validate_options(machine_opts_dict);

    /*
     * These options affect everything else and should be processed
     * before daemonizing.
     */
    qemu_process_early_options();

    qemu_process_help_options();
    qemu_maybe_daemonize();

    qemu_init_main_loop(&error_fatal);
    cpu_timers_init();


    configure_rtc(qemu_find_opts_singleton("rtc"));

    /* Transfer QemuOpts options into machine options */
    parse_memory_options();

    qemu_create_machine(machine_opts_dict);

    suspend_mux_open();

    qemu_disable_default_devices();
    qemu_setup_display();
    qemu_create_default_devices();
    qemu_create_early_backends();

    qemu_apply_legacy_machine_options(machine_opts_dict);
    qemu_apply_machine_options(machine_opts_dict);
    qobject_unref(machine_opts_dict);
    phase_advance(PHASE_MACHINE_CREATED);

    /*
     * Note: uses machine properties such as kernel-irqchip, must run
     * after qemu_apply_machine_options.
     */
    configure_accelerators("xemu");
    phase_advance(PHASE_ACCEL_CREATED);

    /*
     * Beware, QOM objects created before this point miss global and
     * compat properties.
     *
     * Global properties get set up by qdev_prop_register_global(), from
     * option desugaring.
     *
     * Machine compat properties: object_set_machine_compat_props().
     * Accelerator compat props: object_set_accelerator_compat_props(),
     * called from do_configure_accelerator().
     */

    machine_class = MACHINE_GET_CLASS(current_machine);
    if (!qtest_enabled() && machine_class->deprecation_reason) {
        warn_report("Machine type '%s' is deprecated: %s",
                     machine_class->name, machine_class->deprecation_reason);
    }

    /*
     * Create backends before creating migration objects, so that it can
     * check against compatibilities on the backend memories (e.g. postcopy
     * over memory-backend-file objects).
     */
    qemu_create_late_backends();
    phase_advance(PHASE_LATE_BACKENDS_CREATED);

    /*
     * Note: creates a QOM object, must run only after global and
     * compat properties have been set up.
     */
    migration_object_init();

    current_machine->cpu_type = machine_default_cpu_type(current_machine);
    /* NB: for machine none cpu_type could STILL be NULL here! */

    qemu_resolve_machine_memdev();
    parse_numa_opts(current_machine);

    qemu_start_machine(&error_fatal);
    qemu_init_displays();
    accel_setup_post(current_machine);
    os_setup_post();
    resume_mux_open();
}
