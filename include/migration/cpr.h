/*
 * Copyright (c) 2021, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

#include "qapi/qapi-types-migration.h"
#include "qemu/queue.h"

#define MIG_MODE_NONE           -1

#define QEMU_CPR_FILE_MAGIC     0x51435052
#define QEMU_CPR_FILE_VERSION   0x00000001
#define CPR_STATE "CprState"

typedef QLIST_HEAD(CprFdList, CprFd) CprFdList;
typedef QLIST_HEAD(CprVFIODeviceList, CprVFIODevice) CprVFIODeviceList;

typedef struct CprState {
    CprFdList fds;
    CprVFIODeviceList vfio_devices;
} CprState;

extern CprState cpr_state;

void cpr_save_fd(const char *name, int id, int fd);
void cpr_delete_fd(const char *name, int id);
int cpr_find_fd(const char *name, int id);

typedef bool (*cpr_walk_fd_cb)(int fd);
bool cpr_walk_fd(cpr_walk_fd_cb cb);

void cpr_set_incoming_mode(MigMode mode);
bool cpr_is_incoming(void);



void cpr_exec_init(void);
void cpr_exec_unpersist_state(void);
void cpr_exec_unpreserve_fds(void);
#endif
