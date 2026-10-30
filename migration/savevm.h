/*
 * QEMU save vm functions
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009-2017 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_SAVEVM_H
#define MIGRATION_SAVEVM_H

#define QEMU_VM_FILE_MAGIC           0x5145564d
#define QEMU_VM_FILE_VERSION_COMPAT  0x00000002
#define QEMU_VM_FILE_VERSION         0x00000003

#define QEMU_VM_EOF                  0x00
#define QEMU_VM_SECTION_START        0x01
#define QEMU_VM_SECTION_PART         0x02
#define QEMU_VM_SECTION_END          0x03
#define QEMU_VM_SECTION_FULL         0x04
#define QEMU_VM_SUBSECTION           0x05
#define QEMU_VM_VMDESCRIPTION        0x06
#define QEMU_VM_CONFIGURATION        0x07
#define QEMU_VM_COMMAND              0x08
#define QEMU_VM_SECTION_FOOTER       0x7e

#define QEMU_VM_PING_PACKAGED_LOADED 0x42

bool qemu_savevm_state_blocked(Error **errp);
int qemu_savevm_state_prepare(Error **errp);
int qemu_savevm_state_setup(QEMUFile *f, Error **errp);
void qemu_savevm_state_header(QEMUFile *f);
int qemu_savevm_state_iterate(QEMUFile *f, bool postcopy);
void qemu_savevm_state_cleanup(void);
int qemu_savevm_state_complete_precopy(QEMUFile *f, bool iterable_only);
int qemu_savevm_state_complete_precopy_iterable(QEMUFile *f, bool in_postcopy);
void qemu_savevm_send_recv_bitmap(QEMUFile *f, char *block_name);
void qemu_savevm_maybe_send_switchover_start(QEMUFile *f);


int qemu_loadvm_state(QEMUFile *f, Error **errp);
int qemu_savevm_state_libretro(QEMUFile *f, Error **errp);
void qemu_loadvm_state_cleanup(MigrationIncomingState *mis);
int qemu_loadvm_state_main(QEMUFile *f, MigrationIncomingState *mis,
                           Error **errp);
int qemu_savevm_state_complete_precopy_non_iterable(QEMUFile *f,
        bool in_postcopy);

#endif
