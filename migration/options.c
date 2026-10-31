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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/target_page.h"
#include "qapi/clone-visitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/qmp/qerror.h"
#include "qobject/qnull.h"
#include "system/runstate.h"
#include "migration/colo.h"
#include "migration/cpr.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration-stats.h"
#include "qemu-file.h"
#include "ram.h"
#include "options.h"
#include "system/kvm.h"

/* Maximum migrate downtime set to 2000 seconds */
#define MAX_MIGRATE_DOWNTIME_SECONDS 2000
#define MAX_MIGRATE_DOWNTIME (MAX_MIGRATE_DOWNTIME_SECONDS * 1000)

#define MAX_THROTTLE  (128 << 20)      /* Migration transfer speed throttling */

/* Time in milliseconds we are allowed to stop the source,
 * for sending the last part */
#define DEFAULT_MIGRATE_SET_DOWNTIME 300

/* Define default autoconverge cpu throttle migration parameters */
#define DEFAULT_MIGRATE_THROTTLE_TRIGGER_THRESHOLD 50
#define DEFAULT_MIGRATE_CPU_THROTTLE_INITIAL 20
#define DEFAULT_MIGRATE_CPU_THROTTLE_INCREMENT 10
#define DEFAULT_MIGRATE_MAX_CPU_THROTTLE 99

/* Migration XBZRLE default cache size */
#define DEFAULT_MIGRATE_XBZRLE_CACHE_SIZE (64 * 1024 * 1024)

/* The delay time (in ms) between two COLO checkpoints */
#define DEFAULT_MIGRATE_X_CHECKPOINT_DELAY (200 * 100)
#define DEFAULT_MIGRATE_MULTIFD_CHANNELS 2
#define DEFAULT_MIGRATE_MULTIFD_COMPRESSION MULTIFD_COMPRESSION_NONE
/* 0: means nocompress, 1: best speed, ... 9: best compress ratio */
#define DEFAULT_MIGRATE_MULTIFD_ZLIB_LEVEL 1
/*
 * 1: best speed, ... 9: best compress ratio
 * There is some nuance here. Refer to QATzip documentation to understand
 * the mapping of QATzip levels to standard deflate levels.
 */
#define DEFAULT_MIGRATE_MULTIFD_QATZIP_LEVEL 1

/* 0: means nocompress, 1: best speed, ... 20: best compress ratio */
#define DEFAULT_MIGRATE_MULTIFD_ZSTD_LEVEL 1

/* Background transfer rate for postcopy, 0 means unlimited, note
 * that page requests can still exceed this limit.
 */
#define DEFAULT_MIGRATE_MAX_POSTCOPY_BANDWIDTH 0

/*
 * Parameters for self_announce_delay giving a stream of RARP/ARP
 * packets after migration.
 */
#define DEFAULT_MIGRATE_ANNOUNCE_INITIAL  50
#define DEFAULT_MIGRATE_ANNOUNCE_MAX     550
#define DEFAULT_MIGRATE_ANNOUNCE_ROUNDS    5
#define DEFAULT_MIGRATE_ANNOUNCE_STEP    100

#define DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT_PERIOD     1000    /* milliseconds */
#define DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT            1       /* MB/s */

const Property migration_properties[] = {
    DEFINE_PROP_BOOL("store-global-state", MigrationState,
                     store_global_state, true),
    DEFINE_PROP_BOOL("send-configuration", MigrationState,
                     send_configuration, true),
    DEFINE_PROP_BOOL("send-section-footer", MigrationState,
                     send_section_footer, true),
    DEFINE_PROP_BOOL("send-switchover-start", MigrationState,
                     send_switchover_start, true),
    DEFINE_PROP_BOOL("multifd-flush-after-each-section", MigrationState,
                      multifd_flush_after_each_section, false),
    DEFINE_PROP_UINT8("x-clear-bitmap-shift", MigrationState,
                      clear_bitmap_shift, CLEAR_BITMAP_SHIFT_DEFAULT),
    DEFINE_PROP_BOOL("x-preempt-pre-7-2", MigrationState,
                     preempt_pre_7_2, false),
    DEFINE_PROP_BOOL("multifd-clean-tls-termination", MigrationState,
                     multifd_clean_tls_termination, true),

    /* Migration parameters */
    DEFINE_PROP_UINT8("x-throttle-trigger-threshold", MigrationState,
                      parameters.throttle_trigger_threshold,
                      DEFAULT_MIGRATE_THROTTLE_TRIGGER_THRESHOLD),
    DEFINE_PROP_UINT8("x-cpu-throttle-initial", MigrationState,
                      parameters.cpu_throttle_initial,
                      DEFAULT_MIGRATE_CPU_THROTTLE_INITIAL),
    DEFINE_PROP_UINT8("x-cpu-throttle-increment", MigrationState,
                      parameters.cpu_throttle_increment,
                      DEFAULT_MIGRATE_CPU_THROTTLE_INCREMENT),
    DEFINE_PROP_BOOL("x-cpu-throttle-tailslow", MigrationState,
                      parameters.cpu_throttle_tailslow, false),
    DEFINE_PROP_SIZE("x-max-bandwidth", MigrationState,
                      parameters.max_bandwidth, MAX_THROTTLE),
    DEFINE_PROP_SIZE("avail-switchover-bandwidth", MigrationState,
                      parameters.avail_switchover_bandwidth, 0),
    DEFINE_PROP_UINT64("x-downtime-limit", MigrationState,
                      parameters.downtime_limit,
                      DEFAULT_MIGRATE_SET_DOWNTIME),
    DEFINE_PROP_UINT32("x-checkpoint-delay", MigrationState,
                      parameters.x_checkpoint_delay,
                      DEFAULT_MIGRATE_X_CHECKPOINT_DELAY),
    DEFINE_PROP_UINT8("multifd-channels", MigrationState,
                      parameters.multifd_channels,
                      DEFAULT_MIGRATE_MULTIFD_CHANNELS),
    DEFINE_PROP_MULTIFD_COMPRESSION("multifd-compression", MigrationState,
                      parameters.multifd_compression,
                      DEFAULT_MIGRATE_MULTIFD_COMPRESSION),
    DEFINE_PROP_UINT8("multifd-zlib-level", MigrationState,
                      parameters.multifd_zlib_level,
                      DEFAULT_MIGRATE_MULTIFD_ZLIB_LEVEL),
    DEFINE_PROP_UINT8("multifd-qatzip-level", MigrationState,
                      parameters.multifd_qatzip_level,
                      DEFAULT_MIGRATE_MULTIFD_QATZIP_LEVEL),
    DEFINE_PROP_UINT8("multifd-zstd-level", MigrationState,
                      parameters.multifd_zstd_level,
                      DEFAULT_MIGRATE_MULTIFD_ZSTD_LEVEL),
    DEFINE_PROP_SIZE("xbzrle-cache-size", MigrationState,
                      parameters.xbzrle_cache_size,
                      DEFAULT_MIGRATE_XBZRLE_CACHE_SIZE),
    DEFINE_PROP_SIZE("max-postcopy-bandwidth", MigrationState,
                      parameters.max_postcopy_bandwidth,
                      DEFAULT_MIGRATE_MAX_POSTCOPY_BANDWIDTH),
    DEFINE_PROP_UINT8("max-cpu-throttle", MigrationState,
                      parameters.max_cpu_throttle,
                      DEFAULT_MIGRATE_MAX_CPU_THROTTLE),
    DEFINE_PROP_SIZE("announce-initial", MigrationState,
                      parameters.announce_initial,
                      DEFAULT_MIGRATE_ANNOUNCE_INITIAL),
    DEFINE_PROP_SIZE("announce-max", MigrationState,
                      parameters.announce_max,
                      DEFAULT_MIGRATE_ANNOUNCE_MAX),
    DEFINE_PROP_SIZE("announce-rounds", MigrationState,
                      parameters.announce_rounds,
                      DEFAULT_MIGRATE_ANNOUNCE_ROUNDS),
    DEFINE_PROP_SIZE("announce-step", MigrationState,
                      parameters.announce_step,
                      DEFAULT_MIGRATE_ANNOUNCE_STEP),
    DEFINE_PROP_STRING("tls-creds", MigrationState, parameters.tls_creds),
    DEFINE_PROP_STRING("tls-hostname", MigrationState, parameters.tls_hostname),
    DEFINE_PROP_STRING("tls-authz", MigrationState, parameters.tls_authz),
    DEFINE_PROP_UINT64("x-vcpu-dirty-limit-period", MigrationState,
                       parameters.x_vcpu_dirty_limit_period,
                       DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT_PERIOD),
    DEFINE_PROP_UINT64("vcpu-dirty-limit", MigrationState,
                       parameters.vcpu_dirty_limit,
                       DEFAULT_MIGRATE_VCPU_DIRTY_LIMIT),
    DEFINE_PROP_MIG_MODE("mode", MigrationState,
                      parameters.mode,
                      MIG_MODE_NORMAL),
    DEFINE_PROP_ZERO_PAGE_DETECTION("zero-page-detection", MigrationState,
                       parameters.zero_page_detection,
                       ZERO_PAGE_DETECTION_MULTIFD),
};
const size_t migration_properties_count = ARRAY_SIZE(migration_properties);

bool migrate_send_switchover_start(void)
{
    MigrationState *s = migrate_get_current();

    return s->send_switchover_start;
}

static bool migrate_incoming_started(void)
{
    return !!migration_incoming_get_current()->transport_data;
}


/**
 * @migration_caps_check - check capability compatibility
 *
 * @old_caps: old capability list
 * @new_caps: new capability list
 * @errp: set *errp if the check failed, with reason
 *
 * Returns true if check passed, otherwise false.
 */
bool migrate_caps_check(bool *old_caps, bool *new_caps, Error **errp)
{
    ERRP_GUARD();

    if (new_caps[MIGRATION_CAPABILITY_ZERO_BLOCKS]) {
        warn_report("zero-blocks capability is deprecated");
    }

    if (new_caps[MIGRATION_CAPABILITY_X_COLO]) {
        error_setg(errp, "QEMU compiled without replication module"
                   " can't enable COLO");
        error_append_hint(errp, "Please enable replication before COLO.\n");
        return false;
    }

    if (new_caps[MIGRATION_CAPABILITY_POSTCOPY_RAM]) {
        error_setg(errp, "Postcopy is not supported: a savestate is "
                   "loaded whole");
        return false;
    }

    if (new_caps[MIGRATION_CAPABILITY_BACKGROUND_SNAPSHOT]) {
        error_setg(errp, "Background-snapshot is not supported: a savestate "
                   "is taken while the machine is stopped");
        return false;
    }

#ifdef CONFIG_LINUX
    if (new_caps[MIGRATION_CAPABILITY_ZERO_COPY_SEND] &&
        (!new_caps[MIGRATION_CAPABILITY_MULTIFD] || new_caps[MIGRATION_CAPABILITY_XBZRLE] || migrate_multifd_compression())) {
        error_setg(errp,
                   "Zero copy only available for non-compressed non-TLS multifd migration");
        return false;
    }
#else
    if (new_caps[MIGRATION_CAPABILITY_ZERO_COPY_SEND]) {
        error_setg(errp,
                   "Zero copy currently only available on Linux");
        return false;
    }
#endif

    if (new_caps[MIGRATION_CAPABILITY_POSTCOPY_PREEMPT]) {
        if (!new_caps[MIGRATION_CAPABILITY_POSTCOPY_RAM]) {
            error_setg(errp, "Postcopy preempt requires postcopy-ram");
            return false;
        }

        if (migrate_incoming_started()) {
            error_setg(errp,
                       "Postcopy preempt must be set before incoming starts");
            return false;
        }
    }

    if (new_caps[MIGRATION_CAPABILITY_MULTIFD]) {
        if (migrate_incoming_started()) {
            error_setg(errp, "Multifd must be set before incoming starts");
            return false;
        }
    }

    if (new_caps[MIGRATION_CAPABILITY_SWITCHOVER_ACK]) {
        if (!new_caps[MIGRATION_CAPABILITY_RETURN_PATH]) {
            error_setg(errp, "Capability 'switchover-ack' requires capability "
                             "'return-path'");
            return false;
        }
    }
    if (new_caps[MIGRATION_CAPABILITY_DIRTY_LIMIT]) {
        if (new_caps[MIGRATION_CAPABILITY_AUTO_CONVERGE]) {
            error_setg(errp, "dirty-limit conflicts with auto-converge"
                       " either of then available currently");
            return false;
        }

        if (!kvm_enabled() || !kvm_dirty_ring_enabled()) {
            error_setg(errp, "dirty-limit requires KVM with accelerator"
                   " property 'dirty-ring-size' set");
            return false;
        }
    }

    if (new_caps[MIGRATION_CAPABILITY_MULTIFD]) {
        if (new_caps[MIGRATION_CAPABILITY_XBZRLE]) {
            error_setg(errp, "Multifd is not compatible with xbzrle");
            return false;
        }
    }

    if (new_caps[MIGRATION_CAPABILITY_MAPPED_RAM]) {
        if (new_caps[MIGRATION_CAPABILITY_XBZRLE]) {
            error_setg(errp,
                       "Mapped-ram migration is incompatible with xbzrle");
            return false;
        }

        if (new_caps[MIGRATION_CAPABILITY_POSTCOPY_RAM]) {
            error_setg(errp,
                       "Mapped-ram migration is incompatible with postcopy");
            return false;
        }
    }

    /*
     * On destination side, check the cases that capability is being set
     * after incoming thread has started.
     */
    return true;
}

MultiFDCompression migrate_multifd_compression(void)
{
    MigrationState *s = migrate_get_current();

    assert(s->parameters.multifd_compression < MULTIFD_COMPRESSION__MAX);
    return s->parameters.multifd_compression;
}

void migrate_params_init(MigrationParameters *params)
{
    params->tls_hostname = g_strdup("");
    params->tls_creds = g_strdup("");

    /* Set has_* up only for parameter checks */
    params->has_throttle_trigger_threshold = true;
    params->has_cpu_throttle_initial = true;
    params->has_cpu_throttle_increment = true;
    params->has_cpu_throttle_tailslow = true;
    params->has_max_bandwidth = true;
    params->has_downtime_limit = true;
    params->has_x_checkpoint_delay = true;
    params->has_multifd_channels = true;
    params->has_multifd_compression = true;
    params->has_multifd_zlib_level = true;
    params->has_multifd_qatzip_level = true;
    params->has_multifd_zstd_level = true;
    params->has_xbzrle_cache_size = true;
    params->has_max_postcopy_bandwidth = true;
    params->has_max_cpu_throttle = true;
    params->has_announce_initial = true;
    params->has_announce_max = true;
    params->has_announce_rounds = true;
    params->has_announce_step = true;
    params->has_x_vcpu_dirty_limit_period = true;
    params->has_vcpu_dirty_limit = true;
    params->has_mode = true;
    params->has_zero_page_detection = true;
    params->has_direct_io = true;
    params->has_cpr_exec_command = true;
}

/*
 * Check whether the parameters are valid. Error will be put into errp
 * (if provided). Return true if valid, otherwise false.
 */
bool migrate_params_check(MigrationParameters *params, Error **errp)
{
    ERRP_GUARD();

    if (params->has_throttle_trigger_threshold &&
        (params->throttle_trigger_threshold < 1 ||
         params->throttle_trigger_threshold > 100)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "throttle_trigger_threshold",
                   "an integer in the range of 1 to 100");
        return false;
    }

    if (params->has_cpu_throttle_initial &&
        (params->cpu_throttle_initial < 1 ||
         params->cpu_throttle_initial > 99)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "cpu_throttle_initial",
                   "an integer in the range of 1 to 99");
        return false;
    }

    if (params->has_cpu_throttle_increment &&
        (params->cpu_throttle_increment < 1 ||
         params->cpu_throttle_increment > 99)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "cpu_throttle_increment",
                   "an integer in the range of 1 to 99");
        return false;
    }

    if (params->has_max_bandwidth && (params->max_bandwidth > SIZE_MAX)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "max_bandwidth",
                   "an integer in the range of 0 to "stringify(SIZE_MAX)
                   " bytes/second");
        return false;
    }

    if (params->has_avail_switchover_bandwidth &&
        (params->avail_switchover_bandwidth > SIZE_MAX)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "avail_switchover_bandwidth",
                   "an integer in the range of 0 to "stringify(SIZE_MAX)
                   " bytes/second");
        return false;
    }

    if (params->has_downtime_limit &&
        (params->downtime_limit > MAX_MIGRATE_DOWNTIME)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "downtime_limit",
                   "an integer in the range of 0 to "
                    stringify(MAX_MIGRATE_DOWNTIME)" ms");
        return false;
    }

    /* x_checkpoint_delay is now always positive */

    if (params->has_multifd_channels && (params->multifd_channels < 1)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "multifd_channels",
                   "a value between 1 and 255");
        return false;
    }

    if (params->has_multifd_zlib_level &&
        (params->multifd_zlib_level > 9)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "multifd_zlib_level",
                   "a value between 0 and 9");
        return false;
    }

    if (params->has_multifd_qatzip_level &&
        ((params->multifd_qatzip_level > 9) ||
        (params->multifd_qatzip_level < 1))) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "multifd_qatzip_level",
                   "a value between 1 and 9");
        return false;
    }

    if (params->has_multifd_zstd_level &&
        (params->multifd_zstd_level > 20)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "multifd_zstd_level",
                   "a value between 0 and 20");
        return false;
    }

    if (params->has_xbzrle_cache_size &&
        (params->xbzrle_cache_size < qemu_target_page_size() ||
         !is_power_of_2(params->xbzrle_cache_size))) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "xbzrle_cache_size",
                   "a power of two no less than the target page size");
        return false;
    }

    if (params->has_max_cpu_throttle &&
        (params->max_cpu_throttle < params->cpu_throttle_initial ||
         params->max_cpu_throttle > 99)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "max_cpu_throttle",
                   "an integer in the range of cpu_throttle_initial to 99");
        return false;
    }

    if (params->has_announce_initial &&
        params->announce_initial > 100000) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "announce_initial",
                   "a value between 0 and 100000");
        return false;
    }
    if (params->has_announce_max &&
        params->announce_max > 100000) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "announce_max",
                   "a value between 0 and 100000");
       return false;
    }
    if (params->has_announce_rounds &&
        params->announce_rounds > 1000) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "announce_rounds",
                   "a value between 0 and 1000");
       return false;
    }
    if (params->has_announce_step &&
        (params->announce_step < 1 ||
        params->announce_step > 10000)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "announce_step",
                   "a value between 0 and 10000");
       return false;
    }

    if (params->has_block_bitmap_mapping &&
        !check_dirty_bitmap_mig_alias_map(params->block_bitmap_mapping, errp)) {
        error_prepend(errp, "Invalid mapping given for block-bitmap-mapping: ");
        return false;
    }

#ifdef CONFIG_LINUX
#endif

    if (params->has_x_vcpu_dirty_limit_period &&
        (params->x_vcpu_dirty_limit_period < 1 ||
         params->x_vcpu_dirty_limit_period > 1000)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "x-vcpu-dirty-limit-period",
                   "a value between 1 and 1000");
        return false;
    }

    if (params->has_vcpu_dirty_limit &&
        (params->vcpu_dirty_limit < 1)) {
        error_setg(errp,
                   "Parameter 'vcpu_dirty_limit' must be greater than 1 MB/s");
        return false;
    }

    if (params->has_direct_io && params->direct_io && !qemu_has_direct_io()) {
        error_setg(errp, "No build-time support for direct-io");
        return false;
    }

    return true;
}

