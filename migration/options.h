/*
 * QEMU migration capabilities
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *   Orit Wasserman <owasserm@redhat.com>
 *   Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_OPTIONS_H
#define QEMU_MIGRATION_OPTIONS_H

#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/client-options.h"

/* migration properties */

extern const Property migration_properties[];
extern const size_t migration_properties_count;

/*
 * capabilities: the core's savestates are a plain precopy stream to a
 * buffer, so none of them is ever on (no QMP sets them, and the properties
 * that did are gone); they are constants, and what hangs off them folds
 * away
 */

static inline bool migrate_auto_converge(void) { return false; }
static inline bool migrate_colo(void) { return false; }
static inline bool migrate_dirty_bitmaps(void) { return false; }
static inline bool migrate_events(void) { return false; }
static inline bool migrate_mapped_ram(void) { return false; }
static inline bool migrate_ignore_shared(void) { return false; }
static inline bool migrate_late_block_activate(void) { return false; }
static inline bool migrate_multifd(void) { return false; }
static inline bool migrate_pause_before_switchover(void) { return false; }
static inline bool migrate_postcopy_blocktime(void) { return false; }
static inline bool migrate_postcopy_preempt(void) { return false; }
static inline bool migrate_rdma_pin_all(void) { return false; }
static inline bool migrate_release_ram(void) { return false; }
static inline bool migrate_return_path(void) { return false; }
static inline bool migrate_validate_uuid(void) { return false; }
static inline bool migrate_xbzrle(void) { return false; }
static inline bool migrate_zero_copy_send(void) { return false; }

/* pseudo capabilities: not capabilities, used like them */

static inline bool migrate_postcopy(void) { return false; }
static inline bool migrate_rdma(void) { return false; }
static inline bool migrate_tls(void) { return false; }

/* capabilities helpers */

bool migrate_caps_check(bool *old_caps, bool *new_caps, Error **errp);

/* parameters */


uint64_t migrate_downtime_limit(void);
uint64_t migrate_avail_switchover_bandwidth(void);
MultiFDCompression migrate_multifd_compression(void);
uint8_t migrate_throttle_trigger_threshold(void);
ZeroPageDetection migrate_zero_page_detection(void);

/* parameters helpers */

bool migrate_params_check(MigrationParameters *params, Error **errp);
void migrate_params_init(MigrationParameters *params);
#endif
