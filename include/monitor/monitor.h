#ifndef MONITOR_H
#define MONITOR_H

#include "block/block.h"
#include "qapi/qapi-types-misc.h"
#include "qemu/readline.h"
#include "exec/hwaddr.h"

typedef struct MonitorHMP MonitorHMP;
typedef struct MonitorOptions MonitorOptions;

#define QMP_REQ_QUEUE_LEN_MAX 8

extern QemuOptsList qemu_mon_opts;

Monitor *monitor_cur(void);
Monitor *monitor_set_cur(Coroutine *co, Monitor *mon);




int monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);



int monitor_fdset_dup_fd_add(int64_t fdset_id, int flags, Error **errp);
void monitor_fdset_dup_fd_remove(int dup_fd);


int error_vprintf_unless_qmp(const char *fmt, va_list ap) G_GNUC_PRINTF(1, 0);

#endif /* MONITOR_H */
