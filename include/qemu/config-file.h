#ifndef QEMU_CONFIG_FILE_H
#define QEMU_CONFIG_FILE_H

typedef void QEMUConfigCB(const char *group, QDict *qdict, void *opaque, Error **errp);

void qemu_load_module_for_opts(const char *group);
QemuOptsList *qemu_find_opts(const char *group);
QemuOptsList *qemu_find_opts_err(const char *group, Error **errp);
QemuOpts *qemu_find_opts_singleton(const char *group);

extern QemuOptsList *vm_config_groups[];
extern QemuOptsList *drive_config_groups[];

void qemu_add_opts(QemuOptsList *list);

/* A default callback for qemu_read_config_file().  */
void qemu_config_do_parse(const char *group, QDict *qdict, void *opaque, Error **errp);

int qemu_read_config_file(const char *filename, QEMUConfigCB *f, Error **errp);

#endif /* QEMU_CONFIG_FILE_H */
