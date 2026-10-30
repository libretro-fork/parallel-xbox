/*
 * The libretro core has no monitor: no HMP, no QMP, no command table (the
 * frontend is the only user interface, and nothing in the core speaks to
 * the monitor). What the rest of QEMU still calls on it ends here: the
 * "info" printers HMP would have called, the fd= parameters monitors pass
 * file descriptors through, and the start and end of QEMU's run, which set
 * the monitors up and take them down. -monitor, -qmp, -mon, -add-fd and
 * -action are refused.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "monitor/hmp-target.h"
#include "hw/core/cpu.h"
#include "hw/qdev-core.h"
#include "qapi/qapi-commands-qdev.h"
#include "qapi/qapi-commands-run-state.h"
#include "hw/pci/pci-internal.h"

QemuOptsList qemu_mon_opts = {
    .name = "mon",
    .implied_opt_name = "chardev",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_mon_opts.head),
    .desc = {
        { /* end of list */ }
    },
};

void monitor_init_globals(void)
{
}

void monitor_cleanup(void)
{
}

int monitor_init_opts(QemuOpts *opts, Error **errp)
{
    error_setg(errp, "the libretro core has no monitor");
    return -1;
}

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                const char *opaque, Error **errp)
{
    error_setg(errp, "the libretro core has no monitor to add a file "
               "descriptor to");
    return NULL;
}

int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp)
{
    error_setg(errp, "the libretro core has no monitor to pass file "
               "descriptor '%s'", fdname);
    return -1;
}

int monitor_printf(Monitor *mon, const char *fmt, ...)
{
    return 0;
}

int monitor_get_cpu_index(Monitor *mon)
{
    return UNASSIGNED_CPU_INDEX;
}

CPUState *mon_get_cpu(Monitor *mon)
{
    return NULL;
}

bool hmp_handle_error(Monitor *mon, Error *err)
{
    if (err) {
        error_report_err(err);
        return true;
    }
    return false;
}

void monitor_register_hmp_info_hrt(const char *name,
                                   HumanReadableText *(*handler)(Error **errp))
{
}

void pcibus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
}

ObjectPropertyInfoList *qmp_device_list_properties(const char *typename,
                                                   Error **errp)
{
    error_setg(errp, "the libretro core lists no device properties");
    return NULL;
}

void qmp_marshal_set_action(QDict *args, QObject **ret, Error **errp)
{
    error_setg(errp, "the libretro core takes no -action");
}
