/*
 * QEMU public migration capabilities
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_CLIENT_OPTIONS_H
#define QEMU_MIGRATION_CLIENT_OPTIONS_H


/* properties */
bool migrate_send_switchover_start(void);

/* capabilities: never on in the core (migration/options.h) */

static inline bool migrate_background_snapshot(void) { return false; }
static inline bool migrate_dirty_limit(void) { return false; }
static inline bool migrate_postcopy_ram(void) { return false; }
static inline bool migrate_switchover_ack(void) { return false; }

/* parameters */

/* only QMP set another mode, and the core has none */
static inline MigMode migrate_mode(void) { return MIG_MODE_NORMAL; }
uint64_t migrate_vcpu_dirty_limit_period(void);

#endif
