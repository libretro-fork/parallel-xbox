/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009-2015 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
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
#include "hw/boards.h"
#include "net/net.h"
#include "migration.h"
#include "migration/snapshot.h"
#include "migration-stats.h"
#include "migration/vmstate.h"
#include "migration/misc.h"
#include "migration/register.h"
#include "migration/global_state.h"
#include "ram.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-builtin-visit.h"
#include "qemu/error-report.h"
#include "system/cpus.h"
#include "system/memory.h"
#include "exec/target_page.h"
#include "exec/page-vary.h"
#include "trace.h"
#include "qemu/iov.h"
#include "qemu/job.h"
#include "qemu/main-loop.h"
#include "block/snapshot.h"
#include "block/thread-pool.h"
#include "qemu/cutils.h"
#include "system/runstate.h"
#include "system/system.h"
#include "system/xen.h"
#include "migration/colo.h"
#include "qemu/bitmap.h"
#include "net/announce.h"
#include "system/qtest.h"
#include "options.h"

#include "ui/xemu-snapshots.h"

const unsigned int postcopy_ram_discard_version;

/* Subcommands for QEMU_VM_COMMAND */
enum qemu_vm_cmd {
    MIG_CMD_INVALID = 0,   /* Must be 0 */
    MIG_CMD_OPEN_RETURN_PATH,  /* Tell the dest to open the Return path */
    MIG_CMD_PING,              /* Request a PONG on the RP */

    MIG_CMD_POSTCOPY_ADVISE,       /* Prior to any page transfers, just
                                      warn we might want to do PC */
    MIG_CMD_POSTCOPY_LISTEN,       /* Start listening for incoming
                                      pages as it's running. */
    MIG_CMD_POSTCOPY_RUN,          /* Start execution */

    MIG_CMD_POSTCOPY_RAM_DISCARD,  /* A list of pages to discard that
                                      were previously sent during
                                      precopy but are dirty. */
    MIG_CMD_PACKAGED,          /* Send a wrapped stream within this stream */
    MIG_CMD_ENABLE_COLO,       /* Enable COLO */
    MIG_CMD_POSTCOPY_RESUME,   /* resume postcopy on dest */
    MIG_CMD_RECV_BITMAP,       /* Request for recved bitmap on dst */
    MIG_CMD_SWITCHOVER_START,  /* Switchover start notification */
    MIG_CMD_MAX
};

/***********************************************************/
/* Optional load threads pool support */

static void qemu_loadvm_thread_pool_create(MigrationIncomingState *mis)
{
    assert(!mis->load_threads);
    mis->load_threads = thread_pool_new();
    retro_atomic_store_relaxed_int(&mis->load_threads_abort, false);
}

static void qemu_loadvm_thread_pool_destroy(MigrationIncomingState *mis)
{
    retro_atomic_store_relaxed_int(&mis->load_threads_abort, true);

    bql_unlock(); /* Load threads might be waiting for BQL */
    g_clear_pointer(&mis->load_threads, thread_pool_free);
    bql_lock();
}

static bool qemu_loadvm_thread_pool_wait(MigrationState *s,
                                         MigrationIncomingState *mis)
{
    bql_unlock(); /* Let load threads do work requiring BQL */
    thread_pool_wait(mis->load_threads);
    bql_lock();

    return !migrate_has_error(s);
}

/***********************************************************/
/* savevm/loadvm support */


/* QEMUFile timer support.
 * Not in qemu-file.c to not add qemu-timer.c as dependency to qemu-file.c
 */

void timer_put(QEMUFile *f, QEMUTimer *ts)
{
    uint64_t expire_time;

    expire_time = timer_expire_time_ns(ts);
    qemu_put_be64(f, expire_time);
}

void timer_get(QEMUFile *f, QEMUTimer *ts)
{
    uint64_t expire_time;

    expire_time = qemu_get_be64(f);
    if (expire_time != -1) {
        timer_mod_ns(ts, expire_time);
    } else {
        timer_del(ts);
    }
}


/* VMState timer support.
 * Not in vmstate.c to not add qemu-timer.c as dependency to vmstate.c
 */

static int get_timer(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field)
{
    QEMUTimer *v = pv;
    timer_get(f, v);
    return 0;
}

static int put_timer(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    QEMUTimer *v = pv;
    timer_put(f, v);

    return 0;
}

const VMStateInfo vmstate_info_timer = {
    .name = "timer",
    .get  = get_timer,
    .put  = put_timer,
};


typedef struct CompatEntry {
    char idstr[256];
    int instance_id;
} CompatEntry;

typedef struct SaveStateEntry {
    QTAILQ_ENTRY(SaveStateEntry) entry;
    char idstr[256];
    uint32_t instance_id;
    int alias_id;
    int version_id;
    /* version id read from the stream */
    int load_version_id;
    int section_id;
    /* section id read from the stream */
    int load_section_id;
    const SaveVMHandlers *ops;
    const VMStateDescription *vmsd;
    void *opaque;
    CompatEntry *compat;
    int is_ram;
} SaveStateEntry;

typedef struct SaveState {
    QTAILQ_HEAD(, SaveStateEntry) handlers;
    SaveStateEntry *handler_pri_head[MIG_PRI_MAX + 1];
    int global_section_id;
    uint32_t len;
    const char *name;
    uint32_t target_page_bits;
    uint32_t caps_count;
    MigrationCapability *capabilities;
    QemuUUID uuid;
} SaveState;

static SaveState savevm_state = {
    .handlers = QTAILQ_HEAD_INITIALIZER(savevm_state.handlers),
    .handler_pri_head = { [0 ... MIG_PRI_MAX] = NULL },
    .global_section_id = 0,
};

static SaveStateEntry *find_se(const char *idstr, uint32_t instance_id);

static bool should_validate_capability(int capability)
{
    assert(capability >= 0 && capability < MIGRATION_CAPABILITY__MAX);
    /* Validate only new capabilities to keep compatibility. */
    switch (capability) {
    case MIGRATION_CAPABILITY_X_IGNORE_SHARED:
    case MIGRATION_CAPABILITY_MAPPED_RAM:
        return true;
    default:
        return false;
    }
}

static uint32_t get_validatable_capabilities_count(void)
{
    MigrationState *s = migrate_get_current();
    uint32_t result = 0;
    int i;
    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (should_validate_capability(i) && s->capabilities[i]) {
            result++;
        }
    }
    return result;
}

static int configuration_pre_save(void *opaque)
{
    SaveState *state = opaque;
    const char *current_name = MACHINE_GET_CLASS(current_machine)->name;
    MigrationState *s = migrate_get_current();
    int i, j;

    state->len = strlen(current_name);
    state->name = current_name;
    state->target_page_bits = qemu_target_page_bits();

    state->caps_count = get_validatable_capabilities_count();
    state->capabilities = g_renew(MigrationCapability, state->capabilities,
                                  state->caps_count);
    for (i = j = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (should_validate_capability(i) && s->capabilities[i]) {
            state->capabilities[j++] = i;
        }
    }
    state->uuid = qemu_uuid;

    return 0;
}

static int configuration_post_save(void *opaque)
{
    SaveState *state = opaque;

    g_free(state->capabilities);
    state->capabilities = NULL;
    state->caps_count = 0;
    return 0;
}

static int configuration_pre_load(void *opaque)
{
    SaveState *state = opaque;

    /* If there is no target-page-bits subsection it means the source
     * predates the variable-target-page-bits support and is using the
     * minimum possible value for this CPU.
     */
    state->target_page_bits = migration_legacy_page_bits();
    return 0;
}

static bool configuration_validate_capabilities(SaveState *state)
{
    bool ret = true;
    MigrationState *s = migrate_get_current();
    unsigned long *source_caps_bm;
    int i;

    source_caps_bm = bitmap_new(MIGRATION_CAPABILITY__MAX);
    for (i = 0; i < state->caps_count; i++) {
        MigrationCapability capability = state->capabilities[i];
        set_bit(capability, source_caps_bm);
    }

    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        bool source_state, target_state;
        if (!should_validate_capability(i)) {
            continue;
        }
        source_state = test_bit(i, source_caps_bm);
        target_state = s->capabilities[i];
        if (source_state != target_state) {
            error_report("Capability %s is %s, but received capability is %s",
                         MigrationCapability_str(i),
                         target_state ? "on" : "off",
                         source_state ? "on" : "off");
            ret = false;
            /* Don't break here to report all failed capabilities */
        }
    }

    g_free(source_caps_bm);
    return ret;
}

static int configuration_post_load(void *opaque, int version_id)
{
    SaveState *state = opaque;
    const char *current_name = MACHINE_GET_CLASS(current_machine)->name;
    int ret = 0;

    if (strncmp(state->name, current_name, state->len) != 0) {
        error_report("Machine type received is '%.*s' and local is '%s'",
                     (int) state->len, state->name, current_name);
        ret = -EINVAL;
        goto out;
    }

    if (state->target_page_bits != qemu_target_page_bits()) {
        error_report("Received TARGET_PAGE_BITS is %d but local is %d",
                     state->target_page_bits, qemu_target_page_bits());
        ret = -EINVAL;
        goto out;
    }

    if (!configuration_validate_capabilities(state)) {
        ret = -EINVAL;
        goto out;
    }

out:
    g_free((void *)state->name);
    state->name = NULL;
    state->len = 0;
    g_free(state->capabilities);
    state->capabilities = NULL;
    state->caps_count = 0;

    return ret;
}

static int get_capability(QEMUFile *f, void *pv, size_t size,
                          const VMStateField *field)
{
    MigrationCapability *capability = pv;
    char capability_str[UINT8_MAX + 1];
    uint8_t len;
    int i;

    len = qemu_get_byte(f);
    qemu_get_buffer(f, (uint8_t *)capability_str, len);
    capability_str[len] = '\0';
    for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
        if (!strcmp(MigrationCapability_str(i), capability_str)) {
            *capability = i;
            return 0;
        }
    }
    error_report("Received unknown capability %s", capability_str);
    return -EINVAL;
}

static int put_capability(QEMUFile *f, void *pv, size_t size,
                          const VMStateField *field, JSONWriter *vmdesc)
{
    MigrationCapability *capability = pv;
    const char *capability_str = MigrationCapability_str(*capability);
    size_t len = strlen(capability_str);
    assert(len <= UINT8_MAX);

    qemu_put_byte(f, len);
    qemu_put_buffer(f, (uint8_t *)capability_str, len);
    return 0;
}

static const VMStateInfo vmstate_info_capability = {
    .name = "capability",
    .get  = get_capability,
    .put  = put_capability,
};

/* The target-page-bits subsection is present only if the
 * target page size is not the same as the default (ie the
 * minimum page size for a variable-page-size guest CPU).
 * If it is present then it contains the actual target page
 * bits for the machine, and migration will fail if the
 * two ends don't agree about it.
 */
static bool vmstate_target_page_bits_needed(void *opaque)
{
    return qemu_target_page_bits() > migration_legacy_page_bits();
}

static const VMStateDescription vmstate_target_page_bits = {
    .name = "configuration/target-page-bits",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_target_page_bits_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(target_page_bits, SaveState),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmstate_capabilites_needed(void *opaque)
{
    return get_validatable_capabilities_count() > 0;
}

static const VMStateDescription vmstate_capabilites = {
    .name = "configuration/capabilities",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_capabilites_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_V(caps_count, SaveState, 1),
        VMSTATE_VARRAY_UINT32_ALLOC(capabilities, SaveState, caps_count, 1,
                                    vmstate_info_capability,
                                    MigrationCapability),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmstate_uuid_needed(void *opaque)
{
    return false;
}

static int vmstate_uuid_post_load(void *opaque, int version_id)
{
    SaveState *state = opaque;
    char uuid_src[UUID_STR_LEN];
    char uuid_dst[UUID_STR_LEN];

    if (!qemu_uuid_set) {
        /*
         * It's warning because user might not know UUID in some cases,
         * e.g. load an old snapshot
         */
        qemu_uuid_unparse(&state->uuid, uuid_src);
        warn_report("UUID is received %s, but local uuid isn't set",
                     uuid_src);
        return 0;
    }
    if (!qemu_uuid_is_equal(&state->uuid, &qemu_uuid)) {
        qemu_uuid_unparse(&state->uuid, uuid_src);
        qemu_uuid_unparse(&qemu_uuid, uuid_dst);
        error_report("UUID received is %s and local is %s", uuid_src, uuid_dst);
        return -EINVAL;
    }
    return 0;
}

static const VMStateDescription vmstate_uuid = {
    .name = "configuration/uuid",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_uuid_needed,
    .post_load = vmstate_uuid_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY_V(uuid.data, SaveState, sizeof(QemuUUID), 1),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_configuration = {
    .name = "configuration",
    .version_id = 1,
    .pre_load = configuration_pre_load,
    .post_load = configuration_post_load,
    .pre_save = configuration_pre_save,
    .post_save = configuration_post_save,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(len, SaveState),
        VMSTATE_VBUFFER_ALLOC_UINT32(name, SaveState, 0, NULL, len),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_target_page_bits,
        &vmstate_capabilites,
        &vmstate_uuid,
        NULL
    }
};


static uint32_t calculate_new_instance_id(const char *idstr)
{
    SaveStateEntry *se;
    uint32_t instance_id = 0;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (strcmp(idstr, se->idstr) == 0
            && instance_id <= se->instance_id) {
            instance_id = se->instance_id + 1;
        }
    }
    /* Make sure we never loop over without being noticed */
    assert(instance_id != VMSTATE_INSTANCE_ID_ANY);
    return instance_id;
}

static int calculate_compat_instance_id(const char *idstr)
{
    SaveStateEntry *se;
    int instance_id = 0;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->compat) {
            continue;
        }

        if (strcmp(idstr, se->compat->idstr) == 0
            && instance_id <= se->compat->instance_id) {
            instance_id = se->compat->instance_id + 1;
        }
    }
    return instance_id;
}

static inline MigrationPriority save_state_priority(SaveStateEntry *se)
{
    if (se->vmsd && se->vmsd->priority) {
        return se->vmsd->priority;
    }
    return MIG_PRI_DEFAULT;
}

static void savevm_state_handler_insert(SaveStateEntry *nse)
{
    MigrationPriority priority = save_state_priority(nse);
    SaveStateEntry *se;
    int i;

    assert(priority <= MIG_PRI_MAX);

    /*
     * This should never happen otherwise migration will probably fail
     * silently somewhere because we can be wrongly applying one
     * object properties upon another one.  Bail out ASAP.
     */
    if (find_se(nse->idstr, nse->instance_id)) {
        error_report("%s: Detected duplicate SaveStateEntry: "
                     "id=%s, instance_id=0x%"PRIx32, __func__,
                     nse->idstr, nse->instance_id);
        exit(EXIT_FAILURE);
    }

    for (i = priority - 1; i >= 0; i--) {
        se = savevm_state.handler_pri_head[i];
        if (se != NULL) {
            assert(save_state_priority(se) < priority);
            break;
        }
    }

    if (i >= 0) {
        QTAILQ_INSERT_BEFORE(se, nse, entry);
    } else {
        QTAILQ_INSERT_TAIL(&savevm_state.handlers, nse, entry);
    }

    if (savevm_state.handler_pri_head[priority] == NULL) {
        savevm_state.handler_pri_head[priority] = nse;
    }
}

static void savevm_state_handler_remove(SaveStateEntry *se)
{
    SaveStateEntry *next;
    MigrationPriority priority = save_state_priority(se);

    if (se == savevm_state.handler_pri_head[priority]) {
        next = QTAILQ_NEXT(se, entry);
        if (next != NULL && save_state_priority(next) == priority) {
            savevm_state.handler_pri_head[priority] = next;
        } else {
            savevm_state.handler_pri_head[priority] = NULL;
        }
    }
    QTAILQ_REMOVE(&savevm_state.handlers, se, entry);
}

/* TODO: Individual devices generally have very little idea about the rest
   of the system, so instance_id should be removed/replaced.
   Meanwhile pass -1 as instance_id if you do not already have a clearly
   distinguishing id for all instances of your device class. */
int register_savevm_live(const char *idstr,
                         uint32_t instance_id,
                         int version_id,
                         const SaveVMHandlers *ops,
                         void *opaque)
{
    SaveStateEntry *se;

    se = g_new0(SaveStateEntry, 1);
    se->version_id = version_id;
    se->section_id = savevm_state.global_section_id++;
    se->ops = ops;
    se->opaque = opaque;
    se->vmsd = NULL;
    /* if this is a live_savem then set is_ram */
    if (ops->save_setup != NULL) {
        se->is_ram = 1;
    }

    pstrcat(se->idstr, sizeof(se->idstr), idstr);

    if (instance_id == VMSTATE_INSTANCE_ID_ANY) {
        se->instance_id = calculate_new_instance_id(se->idstr);
    } else {
        se->instance_id = instance_id;
    }
    assert(!se->compat || se->instance_id == 0);
    savevm_state_handler_insert(se);
    return 0;
}

void unregister_savevm(VMStateIf *obj, const char *idstr, void *opaque)
{
    SaveStateEntry *se, *new_se;
    char id[256] = "";

    if (obj) {
        char *oid = vmstate_if_get_id(obj);
        if (oid) {
            pstrcpy(id, sizeof(id), oid);
            pstrcat(id, sizeof(id), "/");
            g_free(oid);
        }
    }
    pstrcat(id, sizeof(id), idstr);

    QTAILQ_FOREACH_SAFE(se, &savevm_state.handlers, entry, new_se) {
        if (strcmp(se->idstr, id) == 0 && se->opaque == opaque) {
            savevm_state_handler_remove(se);
            g_free(se->compat);
            g_free(se);
        }
    }
}

/*
 * Perform some basic checks on vmsd's at registration
 * time.
 */
static void vmstate_check(const VMStateDescription *vmsd)
{
    const VMStateField *field = vmsd->fields;
    const VMStateDescription * const *subsection = vmsd->subsections;

    if (field) {
        while (field->name) {
            if (field->flags & (VMS_STRUCT | VMS_VSTRUCT)) {
                /* Recurse to sub structures */
                vmstate_check(field->vmsd);
            }
            /* Carry on */
            field++;
        }
        /* Check for the end of field list canary */
        if (field->flags != VMS_END) {
            error_report("VMSTATE not ending with VMS_END: %s", vmsd->name);
            g_assert_not_reached();
        }
    }

    while (subsection && *subsection) {
        /*
         * The name of a subsection should start with the name of the
         * current object.
         */
        assert(!strncmp(vmsd->name, (*subsection)->name, strlen(vmsd->name)));
        vmstate_check(*subsection);
        subsection++;
    }
}


int vmstate_register_with_alias_id(VMStateIf *obj, uint32_t instance_id,
                                   const VMStateDescription *vmsd,
                                   void *opaque, int alias_id,
                                   int required_for_version,
                                   Error **errp)
{
    SaveStateEntry *se;

    /* If this triggers, alias support can be dropped for the vmsd. */
    assert(alias_id == -1 || required_for_version >= vmsd->minimum_version_id);

    se = g_new0(SaveStateEntry, 1);
    se->version_id = vmsd->version_id;
    se->section_id = savevm_state.global_section_id++;
    se->opaque = opaque;
    se->vmsd = vmsd;
    se->alias_id = alias_id;

    if (obj) {
        char *id = vmstate_if_get_id(obj);
        if (id) {
            if (snprintf(se->idstr, sizeof(se->idstr), "%s/", id) >=
                sizeof(se->idstr)) {
                error_setg(errp, "Path too long for VMState (%s)", id);
                g_free(id);
                g_free(se);

                return -1;
            }
            g_free(id);

            se->compat = g_new0(CompatEntry, 1);
            pstrcpy(se->compat->idstr, sizeof(se->compat->idstr), vmsd->name);
            se->compat->instance_id = instance_id == VMSTATE_INSTANCE_ID_ANY ?
                         calculate_compat_instance_id(vmsd->name) : instance_id;
            instance_id = VMSTATE_INSTANCE_ID_ANY;
        }
    }
    pstrcat(se->idstr, sizeof(se->idstr), vmsd->name);

    if (instance_id == VMSTATE_INSTANCE_ID_ANY) {
        se->instance_id = calculate_new_instance_id(se->idstr);
    } else {
        se->instance_id = instance_id;
    }

    /* Perform a recursive sanity check during the test runs */
    if (qtest_enabled()) {
        vmstate_check(vmsd);
    }
    assert(!se->compat || se->instance_id == 0);
    savevm_state_handler_insert(se);
    return 0;
}

void vmstate_unregister(VMStateIf *obj, const VMStateDescription *vmsd,
                        void *opaque)
{
    SaveStateEntry *se, *new_se;

    QTAILQ_FOREACH_SAFE(se, &savevm_state.handlers, entry, new_se) {
        if (se->vmsd == vmsd && se->opaque == opaque) {
            savevm_state_handler_remove(se);
            g_free(se->compat);
            g_free(se);
        }
    }
}

static int vmstate_load(QEMUFile *f, SaveStateEntry *se, Error **errp)
{
    int ret;
    trace_vmstate_load(se->idstr, se->vmsd ? se->vmsd->name : "(old)");
    if (!se->vmsd) {         /* Old style */
        ret = se->ops->load_state(f, se->opaque, se->load_version_id);
        if (ret < 0) {
            error_setg(errp, "Failed to load vmstate version_id: %d, ret: %d",
                       se->load_version_id, ret);
        }
        return ret;
    }
    return vmstate_load_state(f, se->vmsd, se->opaque, se->load_version_id,
                              errp);
}

static void vmstate_save_old_style(QEMUFile *f, SaveStateEntry *se,
                                   JSONWriter *vmdesc)
{
    uint64_t old_offset = qemu_file_transferred(f);
    se->ops->save_state(f, se->opaque);
    uint64_t size = qemu_file_transferred(f) - old_offset;

    if (vmdesc) {
        json_writer_int64(vmdesc, "size", size);
        json_writer_start_array(vmdesc, "fields");
        json_writer_start_object(vmdesc, NULL);
        json_writer_str(vmdesc, "name", "data");
        json_writer_int64(vmdesc, "size", size);
        json_writer_str(vmdesc, "type", "buffer");
        json_writer_end_object(vmdesc);
        json_writer_end_array(vmdesc);
    }
}

/*
 * Write the header for device section (QEMU_VM_SECTION START/END/PART/FULL)
 */
static void save_section_header(QEMUFile *f, SaveStateEntry *se,
                                uint8_t section_type)
{
    qemu_put_byte(f, section_type);
    qemu_put_be32(f, se->section_id);

    if (section_type == QEMU_VM_SECTION_FULL ||
        section_type == QEMU_VM_SECTION_START) {
        /* ID string */
        size_t len = strlen(se->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)se->idstr, len);

        qemu_put_be32(f, se->instance_id);
        qemu_put_be32(f, se->version_id);
    }
}

/*
 * Write a footer onto device sections that catches cases misformatted device
 * sections.
 */
static void save_section_footer(QEMUFile *f, SaveStateEntry *se)
{
    if (migrate_get_current()->send_section_footer) {
        qemu_put_byte(f, QEMU_VM_SECTION_FOOTER);
        qemu_put_be32(f, se->section_id);
    }
}

static int vmstate_save(QEMUFile *f, SaveStateEntry *se, JSONWriter *vmdesc,
                        Error **errp)
{
    int ret;

    if ((!se->ops || !se->ops->save_state) && !se->vmsd) {
        return 0;
    }
    if (se->vmsd && !vmstate_section_needed(se->vmsd, se->opaque)) {
        trace_savevm_section_skip(se->idstr, se->section_id);
        return 0;
    }

    trace_savevm_section_start(se->idstr, se->section_id);
    save_section_header(f, se, QEMU_VM_SECTION_FULL);
    if (vmdesc) {
        json_writer_start_object(vmdesc, NULL);
        json_writer_str(vmdesc, "name", se->idstr);
        json_writer_int64(vmdesc, "instance_id", se->instance_id);
    }

    trace_vmstate_save(se->idstr, se->vmsd ? se->vmsd->name : "(old)");
    if (!se->vmsd) {
        vmstate_save_old_style(f, se, vmdesc);
    } else {
        ret = vmstate_save_state(f, se->vmsd, se->opaque, vmdesc,
                                 errp);
        if (ret) {
            return ret;
        }
    }

    trace_savevm_section_end(se->idstr, se->section_id, 0);
    save_section_footer(f, se);
    if (vmdesc) {
        json_writer_end_object(vmdesc);
    }
    return 0;
}
/**
 * qemu_savevm_command_send: Send a 'QEMU_VM_COMMAND' type element with the
 *                           command and associated data.
 *
 * @f: File to send command on
 * @command: Command type to send
 * @len: Length of associated data
 * @data: Data associated with command.
 */
static void qemu_savevm_command_send(QEMUFile *f,
                                     enum qemu_vm_cmd command,
                                     uint16_t len,
                                     uint8_t *data)
{
    trace_savevm_command_send(command, len);
    qemu_put_byte(f, QEMU_VM_COMMAND);
    qemu_put_be16(f, (uint16_t)command);
    qemu_put_be16(f, len);
    qemu_put_buffer(f, data, len);
    qemu_fflush(f);
}

static void qemu_savevm_send_switchover_start(QEMUFile *f)
{
    trace_savevm_send_switchover_start();
    qemu_savevm_command_send(f, MIG_CMD_SWITCHOVER_START, 0, NULL);
}

void qemu_savevm_maybe_send_switchover_start(QEMUFile *f)
{
    if (migrate_send_switchover_start()) {
        qemu_savevm_send_switchover_start(f);
    }
}

bool qemu_savevm_state_blocked(Error **errp)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->unmigratable) {
            error_setg(errp, "State blocked by non-migratable device '%s'",
                       se->idstr);
            return true;
        }
    }
    return false;
}

void qemu_savevm_state_header(QEMUFile *f)
{
    MigrationState *s = migrate_get_current();
    JSONWriter *vmdesc = s->vmdesc;
    Error *local_err = NULL;

    trace_savevm_state_header();
    qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
    qemu_put_be32(f, QEMU_VM_FILE_VERSION);

    if (s->send_configuration) {
        qemu_put_byte(f, QEMU_VM_CONFIGURATION);

        if (vmdesc) {
            /*
             * This starts the main json object and is paired with the
             * json_writer_end_object in
             * qemu_savevm_state_complete_precopy_non_iterable
             */
            json_writer_start_object(vmdesc, NULL);
            json_writer_start_object(vmdesc, "configuration");
        }

        vmstate_save_state(f, &vmstate_configuration, &savevm_state,
                           vmdesc, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }

        if (vmdesc) {
            json_writer_end_object(vmdesc);
        }
    }
}

int qemu_savevm_state_prepare(Error **errp)
{
    SaveStateEntry *se;
    int ret;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->save_prepare) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }

        ret = se->ops->save_prepare(se->opaque, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

int qemu_savevm_state_setup(QEMUFile *f, Error **errp)
{
    ERRP_GUARD();
    MigrationState *ms = migrate_get_current();
    JSONWriter *vmdesc = ms->vmdesc;
    SaveStateEntry *se;
    int ret = 0;

    if (vmdesc) {
        json_writer_int64(vmdesc, "page_size", qemu_target_page_size());
        json_writer_start_array(vmdesc, "devices");
    }

    trace_savevm_state_setup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->early_setup) {
            ret = vmstate_save(f, se, vmdesc, errp);
            if (ret) {
                migrate_set_error(ms, *errp);
                qemu_file_set_error(f, ret);
                break;
            }
            continue;
        }

        if (!se->ops || !se->ops->save_setup) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }
        save_section_header(f, se, QEMU_VM_SECTION_START);

        ret = se->ops->save_setup(f, se->opaque, errp);
        save_section_footer(f, se);
        if (ret < 0) {
            qemu_file_set_error(f, ret);
            break;
        }
    }

    if (ret) {
        return ret;
    }

    return 0;
}

/*
 * this function has three return values:
 *   negative: there was one error, and we have -errno.
 *   0 : We haven't finished, caller have to go again
 *   1 : We have finished, we can go to complete phase
 */
int qemu_savevm_state_iterate(QEMUFile *f, bool postcopy)
{
    SaveStateEntry *se;
    bool all_finished = true;
    int ret;

    trace_savevm_state_iterate();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->save_live_iterate) {
            continue;
        }
        if (se->ops->is_active &&
            !se->ops->is_active(se->opaque)) {
            continue;
        }
        if (se->ops->is_active_iterate &&
            !se->ops->is_active_iterate(se->opaque)) {
            continue;
        }
        /*
         * In the postcopy phase, any device that doesn't know how to
         * do postcopy should have saved it's state in the _complete
         * call that's already run, it might get confused if we call
         * iterate afterwards.
         */
        if (postcopy &&
            !(se->ops->has_postcopy && se->ops->has_postcopy(se->opaque))) {
            continue;
        }
        if (migration_rate_exceeded(f)) {
            return 0;
        }
        trace_savevm_section_start(se->idstr, se->section_id);

        save_section_header(f, se, QEMU_VM_SECTION_PART);

        ret = se->ops->save_live_iterate(f, se->opaque);
        trace_savevm_section_end(se->idstr, se->section_id, ret);
        save_section_footer(f, se);

        if (ret < 0) {
            error_report("failed to save SaveStateEntry with id(name): "
                         "%d(%s): %d",
                         se->section_id, se->idstr, ret);
            qemu_file_set_error(f, ret);
            return ret;
        } else if (!ret) {
            all_finished = false;
        }
    }
    return all_finished;
}

bool should_send_vmdesc(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    return !machine->suppress_vmdesc;
}

static bool qemu_savevm_complete_exists(SaveStateEntry *se)
{
    return se->ops && se->ops->save_complete;
}

/*
 * Invoke the ->save_complete() if necessary.
 * Returns: 0 if skip the current SE or succeeded, <0 if error happened.
 */
static int qemu_savevm_complete(SaveStateEntry *se, QEMUFile *f)
{
    int ret;

    if (se->ops->is_active) {
        if (!se->ops->is_active(se->opaque)) {
            return 0;
        }
    }

    trace_savevm_section_start(se->idstr, se->section_id);
    save_section_header(f, se, QEMU_VM_SECTION_END);
    ret = se->ops->save_complete(f, se->opaque);
    trace_savevm_section_end(se->idstr, se->section_id, ret);
    save_section_footer(f, se);

    if (ret < 0) {
        qemu_file_set_error(f, ret);
    }

    return ret;
}

int qemu_savevm_state_complete_precopy_iterable(QEMUFile *f, bool in_postcopy)
{
    int64_t start_ts_each, end_ts_each;
    SaveStateEntry *se;
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!qemu_savevm_complete_exists(se)) {
            continue;
        }

        if (in_postcopy && se->ops->has_postcopy &&
            se->ops->has_postcopy(se->opaque)) {
            /*
             * If postcopy will start soon, and if the SE supports
             * postcopy, then we can skip the SE for the postcopy phase.
             */
            continue;
        }

        start_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        if (qemu_savevm_complete(se, f) < 0) {
            return -1;
        }
        end_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);

        trace_vmstate_downtime_save("iterable", se->idstr, se->instance_id,
                                    end_ts_each - start_ts_each);
    }

    trace_vmstate_downtime_checkpoint("src-iterable-saved");

    return 0;
}

int qemu_savevm_state_complete_precopy_non_iterable(QEMUFile *f,
                                                    bool in_postcopy)
{
    MigrationState *ms = migrate_get_current();
    int64_t start_ts_each, end_ts_each;
    JSONWriter *vmdesc = ms->vmdesc;
    int vmdesc_len;
    SaveStateEntry *se;
    Error *local_err = NULL;
    int ret;

    /* Making sure cpu states are synchronized before saving non-iterable */
    cpu_synchronize_all_states();

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->vmsd && se->vmsd->early_setup) {
            /* Already saved during qemu_savevm_state_setup(). */
            continue;
        }

        start_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);

        ret = vmstate_save(f, se, vmdesc, &local_err);
        if (ret) {
            migrate_set_error(ms, local_err);
            error_report_err(local_err);
            qemu_file_set_error(f, ret);
            return ret;
        }

        end_ts_each = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_vmstate_downtime_save("non-iterable", se->idstr, se->instance_id,
                                    end_ts_each - start_ts_each);
    }

    if (!in_postcopy) {
        /* Postcopy stream will still be going */
        qemu_put_byte(f, QEMU_VM_EOF);

        if (vmdesc) {
            json_writer_end_array(vmdesc);
            json_writer_end_object(vmdesc);
            vmdesc_len = strlen(json_writer_get(vmdesc));

            qemu_put_byte(f, QEMU_VM_VMDESCRIPTION);
            qemu_put_be32(f, vmdesc_len);
            qemu_put_buffer(f, (uint8_t *)json_writer_get(vmdesc), vmdesc_len);
        }
    }

    trace_vmstate_downtime_checkpoint("src-non-iterable-saved");

    return 0;
}

int qemu_savevm_state_complete_precopy(QEMUFile *f, bool iterable_only)
{
    int ret;

    ret = qemu_savevm_state_complete_precopy_iterable(f, false);
    if (ret) {
        return ret;
    }

    if (!iterable_only) {
        ret = qemu_savevm_state_complete_precopy_non_iterable(f, false);
        if (ret) {
            return ret;
        }
    }

    return qemu_fflush(f);
}

void qemu_savevm_state_cleanup(void)
{
    SaveStateEntry *se;

    trace_savevm_state_cleanup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->ops && se->ops->save_cleanup) {
            se->ops->save_cleanup(se->opaque);
        }
    }
}

static int qemu_savevm_state(QEMUFile *f, Error **errp)
{
    int ret;
    MigrationState *ms = migrate_get_current();
    MigrationStatus status;

    if (migration_is_running()) {
        error_setg(errp, "There's a migration process in progress");
        return -EINVAL;
    }

    ret = migrate_init(ms, errp);
    if (ret) {
        return ret;
    }
    ms->to_dst_file = f;

    xemu_snapshots_save_extra_data(f);

    qemu_savevm_state_header(f);
    ret = qemu_savevm_state_setup(f, errp);
    if (ret) {
        goto cleanup;
    }

    while (qemu_file_get_error(f) == 0) {
        if (qemu_savevm_state_iterate(f, false) > 0) {
            break;
        }
    }

    ret = qemu_file_get_error(f);
    if (ret == 0) {
        qemu_savevm_maybe_send_switchover_start(f);
        qemu_savevm_state_complete_precopy(f, false);
        ret = qemu_file_get_error(f);
    }
    if (ret != 0) {
        error_setg_errno(errp, -ret, "Error while writing VM state");
    }
cleanup:
    qemu_savevm_state_cleanup();

    if (ret != 0) {
        status = MIGRATION_STATUS_FAILED;
    } else {
        status = MIGRATION_STATUS_COMPLETED;
    }
    migrate_set_state(&ms->state, MIGRATION_STATUS_SETUP, status);

    /* f is outer parameter, it should not stay in global migration state after
     * this function finished */
    ms->to_dst_file = NULL;

    return ret;
}

/* The libretro core's savestates (ui/libretro-state.c): the snapshot's
 * stream, into a buffer of the frontend's instead of the HDD image */
int qemu_savevm_state_libretro(QEMUFile *f, Error **errp)
{
    return qemu_savevm_state(f, errp);
}

static SaveStateEntry *find_se(const char *idstr, uint32_t instance_id)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!strcmp(se->idstr, idstr) &&
            (instance_id == se->instance_id ||
             instance_id == se->alias_id))
            return se;
        /* Migrating from an older version? */
        if (strstr(se->idstr, idstr) && se->compat) {
            if (!strcmp(se->compat->idstr, idstr) &&
                (instance_id == se->compat->instance_id ||
                 instance_id == se->alias_id))
                return se;
        }
    }
    return NULL;
}

enum LoadVMExitCodes {
    /* Allow a command to quit all layers of nested loadvm loops */
    LOADVM_QUIT     =  1,
};

/* ------ incoming postcopy messages ------ */
/* 'advise' arrives before any transfers just to tell us that a postcopy
 * *might* happen - it might be skipped if precopy transferred everything
 * quickly.
 */

/*
 * Process an incoming 'QEMU_VM_COMMAND'
 * 0           just a normal return
 * LOADVM_QUIT All good, but exit the loop
 * <0          Error
 */
/*
 * A savestate is written by qemu_savevm_state_libretro(), whose only
 * command is SWITCHOVER_START (send-switchover-start is on by default),
 * just before the devices' final state; no device of this core has a
 * switchover_start handler, so it does nothing here.  The return path,
 * postcopy, COLO, packaged and ping commands only pass between two
 * running QEMUs: a stream that carries one is not a savestate of this core.
 */
static int loadvm_process_command(QEMUFile *f, Error **errp)
{
    uint16_t cmd;
    uint16_t len;
    int ret;

    cmd = qemu_get_be16(f);
    len = qemu_get_be16(f);

    ret = qemu_file_get_error(f);
    if (ret) {
        error_setg(errp,
                   "Failed to load VM process command: stream error: %d",
                   ret);
        return ret;
    }

    if (cmd == MIG_CMD_SWITCHOVER_START && len == 0) {
        return 0;
    }

    error_setg(errp, "MIG_CMD 0x%x (len 0x%x) is not part of a savestate",
               cmd, len);
    return -EINVAL;
}

/*
 * Read a footer off the wire and check that it matches the expected section
 *
 * Returns: true if the footer was good
 *          false if there is a problem (and calls error_report to say why)
 */
static bool check_section_footer(QEMUFile *f, SaveStateEntry *se)
{
    int ret;
    uint8_t read_mark;
    uint32_t read_section_id;

    if (!migrate_get_current()->send_section_footer) {
        /* No footer to check */
        return true;
    }

    read_mark = qemu_get_byte(f);

    ret = qemu_file_get_error(f);
    if (ret) {
        error_report("%s: Read section footer failed: %d",
                     __func__, ret);
        return false;
    }

    if (read_mark != QEMU_VM_SECTION_FOOTER) {
        error_report("Missing section footer for %s", se->idstr);
        return false;
    }

    read_section_id = qemu_get_be32(f);
    if (read_section_id != se->load_section_id) {
        error_report("Mismatched section id in footer for %s -"
                     " read 0x%x expected 0x%x",
                     se->idstr, read_section_id, se->load_section_id);
        return false;
    }

    /* All good */
    return true;
}

static int
qemu_loadvm_section_start_full(QEMUFile *f, uint8_t type, Error **errp)
{
    ERRP_GUARD();
    bool trace_downtime = (type == QEMU_VM_SECTION_FULL);
    uint32_t instance_id, version_id, section_id;
    int64_t start_ts, end_ts;
    SaveStateEntry *se;
    char idstr[256];
    int ret;

    /* Read section start */
    section_id = qemu_get_be32(f);
    if (!qemu_get_counted_string(f, idstr)) {
        error_setg(errp, "Unable to read ID string for section %u",
                   section_id);
        return -EINVAL;
    }
    instance_id = qemu_get_be32(f);
    version_id = qemu_get_be32(f);

    ret = qemu_file_get_error(f);
    if (ret) {
        error_setg(errp, "Failed to read instance/version ID: %d", ret);
        return ret;
    }

    trace_qemu_loadvm_state_section_startfull(section_id, idstr,
            instance_id, version_id);
    /* Find savevm section */
    se = find_se(idstr, instance_id);
    if (se == NULL) {
        error_setg(errp, "Unknown section or instance '%s' %"PRIu32". "
                   "Make sure that your current VM setup matches your "
                   "saved VM setup, including any hotplugged devices",
                   idstr, instance_id);
        return -EINVAL;
    }

    /* Validate version */
    if (version_id > se->version_id) {
        error_setg(errp, "unsupported version %d for '%s' v%d",
                   version_id, idstr, se->version_id);
        return -EINVAL;
    }
    se->load_version_id = version_id;
    se->load_section_id = section_id;

    /* Validate if it is a device's state */
    if (xen_enabled() && se->is_ram) {
        error_setg(errp, "loadvm: %s RAM loading not allowed on Xen", idstr);
        return -EINVAL;
    }

    if (trace_downtime) {
        start_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    }

    ret = vmstate_load(f, se, errp);
    if (ret < 0) {
        error_prepend(errp,
                      "error while loading state for instance 0x%"PRIx32" of"
                      " device '%s': ", instance_id, idstr);
        return ret;
    }

    if (trace_downtime) {
        end_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_vmstate_downtime_load("non-iterable", se->idstr,
                                    se->instance_id, end_ts - start_ts);
    }

    if (!check_section_footer(f, se)) {
        error_setg(errp, "Section footer error, section_id: %d",
                   section_id);
        return -EINVAL;
    }

    return 0;
}

static int
qemu_loadvm_section_part_end(QEMUFile *f, uint8_t type, Error **errp)
{
    bool trace_downtime = (type == QEMU_VM_SECTION_END);
    int64_t start_ts, end_ts;
    uint32_t section_id;
    SaveStateEntry *se;
    int ret;

    section_id = qemu_get_be32(f);

    ret = qemu_file_get_error(f);
    if (ret) {
        error_setg(errp, "Failed to read section ID: %d", ret);
        return ret;
    }

    trace_qemu_loadvm_state_section_partend(section_id);
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->load_section_id == section_id) {
            break;
        }
    }
    if (se == NULL) {
        error_setg(errp, "Unknown section %d", section_id);
        return -EINVAL;
    }

    if (trace_downtime) {
        start_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    }

    ret = vmstate_load(f, se, errp);
    if (ret < 0) {
        return ret;
    }

    if (trace_downtime) {
        end_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        trace_vmstate_downtime_load("iterable", se->idstr,
                                    se->instance_id, end_ts - start_ts);
    }

    if (!check_section_footer(f, se)) {
        error_setg(errp, "Section footer error, section_id: %d",
                   section_id);
        return -EINVAL;
    }

    return 0;
}

static int qemu_loadvm_state_header(QEMUFile *f, Error **errp)
{
    unsigned int v;
    int ret;

    v = qemu_get_be32(f);
    if (v != QEMU_VM_FILE_MAGIC) {
        error_setg(errp, "Not a migration stream, magic: %x != %x",
                   v, QEMU_VM_FILE_MAGIC);
        return -EINVAL;
    }

    v = qemu_get_be32(f);
    if (v == QEMU_VM_FILE_VERSION_COMPAT) {
        error_setg(errp,
                   "SaveVM v2 format is obsolete and no longer supported");

        return -ENOTSUP;
    }
    if (v != QEMU_VM_FILE_VERSION) {
        error_setg(errp, "Unsupported migration stream version, "
                   "file version %x != %x",
                   v, QEMU_VM_FILE_VERSION);
        return -ENOTSUP;
    }

    if (migrate_get_current()->send_configuration) {
        v = qemu_get_byte(f);
        if (v != QEMU_VM_CONFIGURATION) {
            error_setg(errp, "Configuration section missing, %x != %x",
                       v, QEMU_VM_CONFIGURATION);
            return -EINVAL;
        }

        ret = vmstate_load_state(f, &vmstate_configuration, &savevm_state, 0,
                                 errp);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

static int qemu_loadvm_state_setup(QEMUFile *f, Error **errp)
{
    ERRP_GUARD();
    SaveStateEntry *se;
    int ret;

    trace_loadvm_state_setup();
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (!se->ops || !se->ops->load_setup) {
            continue;
        }
        if (se->ops->is_active) {
            if (!se->ops->is_active(se->opaque)) {
                continue;
            }
        }

        ret = se->ops->load_setup(f, se->opaque, errp);
        if (ret < 0) {
            error_prepend(errp, "Load state of device %s failed: ",
                          se->idstr);
            qemu_file_set_error(f, ret);
            return ret;
        }
    }
    return 0;
}

struct LoadThreadData {
    MigrationLoadThread function;
    void *opaque;
};

void qemu_loadvm_state_cleanup(MigrationIncomingState *mis)
{
    SaveStateEntry *se;

    trace_loadvm_state_cleanup();

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        if (se->ops && se->ops->load_cleanup) {
            se->ops->load_cleanup(se->opaque);
        }
    }

    qemu_loadvm_thread_pool_destroy(mis);
}

int qemu_loadvm_state_main(QEMUFile *f, MigrationIncomingState *mis,
                           Error **errp)
{
    ERRP_GUARD();
    uint8_t section_type;
    int ret = 0;

    while (true) {
        section_type = qemu_get_byte(f);

        ret = qemu_file_get_error_obj_any(f, mis->postcopy_qemufile_dst, errp);
        if (ret) {
            error_prepend(errp,
                          "Failed to load section ID: stream error: %d: ",
                          ret);
            break;
        }

        trace_qemu_loadvm_state_section(section_type);
        switch (section_type) {
        case QEMU_VM_SECTION_START:
        case QEMU_VM_SECTION_FULL:
            ret = qemu_loadvm_section_start_full(f, section_type, errp);
            if (ret < 0) {
                goto out;
            }
            break;
        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            ret = qemu_loadvm_section_part_end(f, section_type, errp);
            if (ret < 0) {
                goto out;
            }
            break;
        case QEMU_VM_COMMAND:
            ret = loadvm_process_command(f, errp);
            trace_qemu_loadvm_state_section_command(ret);
            if ((ret < 0) || (ret == LOADVM_QUIT)) {
                goto out;
            }
            break;
        case QEMU_VM_EOF:
            /* This is the end of migration */
            goto out;
        default:
            error_setg(errp, "Unknown section type %d", section_type);
            ret = -EINVAL;
            goto out;
        }
    }

out:
    if (ret < 0) {
        qemu_file_set_error(f, ret);

        /* Cancel bitmaps incoming regardless of recovery */
        dirty_bitmap_mig_cancel_incoming();

    }
    return ret;
}

int qemu_loadvm_state(QEMUFile *f, Error **errp)
{
    MigrationState *s = migrate_get_current();
    MigrationIncomingState *mis = migration_incoming_get_current();
    int ret;

    if (qemu_savevm_state_blocked(errp)) {
        return -EINVAL;
    }

    if (!xemu_snapshots_offset_extra_data(f)) {
        return -EINVAL;
    }

    qemu_loadvm_thread_pool_create(mis);

    ret = qemu_loadvm_state_header(f, errp);
    if (ret) {
        return ret;
    }

    if (qemu_loadvm_state_setup(f, errp) != 0) {
        return -EINVAL;
    }

    cpu_synchronize_all_pre_loadvm();

    ret = qemu_loadvm_state_main(f, mis, errp);

    trace_qemu_loadvm_state_post_main(ret);

    if (mis->have_listen_thread) {
        /*
         * Postcopy listen thread still going, don't synchronize the
         * cpus yet.
         */
        return ret;
    }

    /* When reaching here, it must be precopy */
    if (ret == 0) {
        if (migrate_has_error(migrate_get_current()) ||
            !qemu_loadvm_thread_pool_wait(s, mis)) {
            ret = -EINVAL;
            error_setg(errp,
                       "Error while loading vmstate");
        } else {
            ret = qemu_file_get_error(f);
            if (ret < 0) {
                error_setg(errp,
                           "Error while loading vmstate: stream error: %d",
                           ret);
            }
        }
    }
    /*
     * Set this flag unconditionally so we'll catch further attempts to
     * start additional threads via an appropriate assert()
     */
    retro_atomic_store_relaxed_int(&mis->load_threads_abort, true);

    /*
     * Try to read in the VMDESC section as well, so that dumping tools that
     * intercept our migration stream have the chance to see it.
     */

    /* We've got to be careful; if we don't read the data and just shut the fd
     * then the sender can error if we close while it's still sending.
     * We also mustn't read data that isn't there; some transports (RDMA)
     * will stall waiting for that data when the source has already closed.
     */
    if (ret == 0 && should_send_vmdesc()) {
        uint8_t *buf;
        uint32_t size;
        uint8_t  section_type = qemu_get_byte(f);

        if (section_type != QEMU_VM_VMDESCRIPTION) {
            error_report("Expected vmdescription section, but got %d",
                         section_type);
            /*
             * It doesn't seem worth failing at this point since
             * we apparently have an otherwise valid VM state
             */
        } else {
            buf = g_malloc(0x1000);
            size = qemu_get_be32(f);

            while (size > 0) {
                uint32_t read_chunk = MIN(size, 0x1000);
                qemu_get_buffer(f, buf, read_chunk);
                size -= read_chunk;
            }
            g_free(buf);
        }
    }

    cpu_synchronize_all_post_init();

    return ret;
}

void vmstate_register_ram(MemoryRegion *mr, DeviceState *dev)
{
    qemu_ram_set_idstr(mr->ram_block,
                       memory_region_name(mr), dev);
    qemu_ram_set_migratable(mr->ram_block);
    ram_block_add_cpr_blocker(mr->ram_block, &error_fatal);
}

void vmstate_unregister_ram(MemoryRegion *mr, DeviceState *dev)
{
    qemu_ram_unset_idstr(mr->ram_block);
    qemu_ram_unset_migratable(mr->ram_block);
    ram_block_del_cpr_blocker(mr->ram_block);
}

void vmstate_register_ram_global(MemoryRegion *mr)
{
    vmstate_register_ram(mr, NULL);
}

bool vmstate_check_only_migratable(const VMStateDescription *vmsd)
{
    /* check needed if --only-migratable is specified */
    if (!only_migratable) {
        return true;
    }

    return !(vmsd && vmsd->unmigratable);
}

typedef struct SnapshotJob {
    Job common;
    char *tag;
    char *vmstate;
    strList *devices;
    Coroutine *co;
    Error **errp;
    bool ret;
} SnapshotJob;




