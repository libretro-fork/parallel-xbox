/*
 * The guest's RAM in a savestate.
 *
 * QEMU's migration/ram.c migrates RAM while the guest runs: a dirty bitmap
 * per block, rounds of resending what was written meanwhile, compression,
 * postcopy, multifd channels, COLO.  A savestate of this core is taken
 * while the machine is stopped and loaded whole (ui/libretro-state.c), so
 * this writes every page once and reads them back, in ram.c's stream
 * format (section version 4, precopy, no capabilities): states written by
 * either load with the other.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"
#include "qemu/rcu_queue.h"
#include "qapi/error.h"
#include "exec/target_page.h"
#include "system/ramblock.h"
#include "system/ram_addr.h"
#include "migration/misc.h"
#include "migration/register.h"
#include "migration-stats.h"
#include "qemu-file.h"
#include "savevm.h"
#include "ram.h"

/* The block the last page header of a section named, for CONTINUE */
static RAMBlock *last_sent_block;
static RAMBlock *last_recv_block;

bool migrate_ram_is_ignored(RAMBlock *block)
{
    return !qemu_ram_is_migratable(block);
}

int foreach_not_ignored_block(RAMBlockIterFunc func, void *opaque)
{
    RAMBlock *block;
    int ret = 0;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH_NOT_IGNORED(block) {
        ret = func(block, opaque);
        if (ret) {
            break;
        }
    }
    return ret;
}

void ram_handle_zero(void *host, uint64_t size)
{
    if (!buffer_is_zero(host, size)) {
        memset(host, 0, size);
    }
}

static uint64_t ram_bytes_migratable(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    RAMBLOCK_FOREACH_MIGRATABLE(block) {
        total += block->used_length;
    }
    return total;
}

/* A page header: the offset with its flags, then the block's name unless
 * it is the block of the previous header */
static void save_page_header(QEMUFile *f, RAMBlock *block, ram_addr_t offset)
{
    if (block == last_sent_block) {
        qemu_put_be64(f, offset | RAM_SAVE_FLAG_CONTINUE);
        return;
    }
    qemu_put_be64(f, offset);
    qemu_put_byte(f, strlen(block->idstr));
    qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
    last_sent_block = block;
}

/* The block list: what a load checks the machine's blocks against */
static int ram_save_setup(QEMUFile *f, void *opaque, Error **errp)
{
    RAMBlock *block;

    WITH_RCU_READ_LOCK_GUARD() {
        qemu_put_be64(f, ram_bytes_migratable() | RAM_SAVE_FLAG_MEM_SIZE);

        RAMBLOCK_FOREACH_MIGRATABLE(block) {
            qemu_put_byte(f, strlen(block->idstr));
            qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
            qemu_put_be64(f, block->used_length);
        }
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    return qemu_fflush(f);
}

/* Every page, once: the machine is stopped while a savestate is taken */
static int ram_save_complete(QEMUFile *f, void *opaque)
{
    RAMBlock *block;

    last_sent_block = NULL;

    WITH_RCU_READ_LOCK_GUARD() {
        RAMBLOCK_FOREACH_NOT_IGNORED(block) {
            ram_addr_t offset;

            for (offset = 0; offset < block->used_length;
                 offset += TARGET_PAGE_SIZE) {
                uint8_t *p = block->host + offset;

                if (buffer_is_zero(p, TARGET_PAGE_SIZE)) {
                    save_page_header(f, block, offset | RAM_SAVE_FLAG_ZERO);
                    qemu_put_byte(f, 0);
                    stat64_add(&mig_stats.zero_pages, 1);
                } else {
                    save_page_header(f, block, offset | RAM_SAVE_FLAG_PAGE);
                    qemu_put_buffer(f, p, TARGET_PAGE_SIZE);
                    stat64_add(&mig_stats.normal_pages, 1);
                }
            }
        }
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);
    return qemu_fflush(f);
}

static int parse_ramblocks(QEMUFile *f, ram_addr_t total_ram_bytes)
{
    Error *local_err = NULL;
    int ret = 0;

    while (!ret && total_ram_bytes) {
        RAMBlock *block;
        char id[256];
        ram_addr_t length;
        int len = qemu_get_byte(f);

        qemu_get_buffer(f, (uint8_t *)id, len);
        id[len] = 0;
        length = qemu_get_be64(f);

        block = qemu_ram_block_by_name(id);
        if (!block) {
            error_report("Unknown ramblock \"%s\", cannot accept "
                         "migration", id);
            return -EINVAL;
        }
        if (!qemu_ram_is_migratable(block)) {
            error_report("block %s should not be migrated !", block->idstr);
            return -EINVAL;
        }
        if (length != block->used_length) {
            ret = qemu_ram_resize(block, length, &local_err);
            if (local_err) {
                error_report_err(local_err);
                return ret;
            }
        }
        total_ram_bytes -= length;
    }

    return ret;
}

static RAMBlock *ram_block_from_stream(QEMUFile *f, int flags)
{
    RAMBlock *block;
    char id[256];
    uint8_t len;

    if (flags & RAM_SAVE_FLAG_CONTINUE) {
        if (!last_recv_block) {
            error_report("Ack, bad migration stream!");
        }
        return last_recv_block;
    }

    len = qemu_get_byte(f);
    qemu_get_buffer(f, (uint8_t *)id, len);
    id[len] = 0;

    block = qemu_ram_block_by_name(id);
    if (!block) {
        error_report("Can't find block %s", id);
        return NULL;
    }
    if (migrate_ram_is_ignored(block)) {
        error_report("block %s should not be migrated !", id);
        return NULL;
    }

    last_recv_block = block;
    return block;
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    int flags = 0, ret = 0;

    if (version_id != 4) {
        return -EINVAL;
    }

    RCU_READ_LOCK_GUARD();

    while (!ret && !(flags & RAM_SAVE_FLAG_EOS)) {
        ram_addr_t addr = qemu_get_be64(f);
        void *host = NULL;
        uint8_t ch;

        ret = qemu_file_get_error(f);
        if (ret) {
            error_report("Getting RAM address failed");
            break;
        }

        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE)) {
            RAMBlock *block = ram_block_from_stream(f, flags);

            if (block && offset_in_ramblock(block, addr)) {
                host = block->host + addr;
            }
            if (!host) {
                error_report("Illegal RAM offset " RAM_ADDR_FMT, addr);
                ret = -EINVAL;
                break;
            }
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_MEM_SIZE:
            ret = parse_ramblocks(f, addr);
            break;

        case RAM_SAVE_FLAG_ZERO:
            ch = qemu_get_byte(f);
            if (ch != 0) {
                error_report("Found a zero page with value %d", ch);
                ret = -EINVAL;
                break;
            }
            ram_handle_zero(host, TARGET_PAGE_SIZE);
            break;

        case RAM_SAVE_FLAG_PAGE:
            qemu_get_buffer(f, host, TARGET_PAGE_SIZE);
            break;

        case RAM_SAVE_FLAG_EOS:
            break;

        default:
            /* XBZRLE, RDMA hooks and multifd flushes need capabilities a
             * savestate is never written with */
            error_report("Unknown combination of migration flags: 0x%x",
                         flags);
            ret = -EINVAL;
        }
        if (!ret) {
            ret = qemu_file_get_error(f);
        }
    }

    return ret;
}

static int ram_load_setup(QEMUFile *f, void *opaque, Error **errp)
{
    last_recv_block = NULL;
    return 0;
}

static SaveVMHandlers savevm_ram_handlers = {
    .save_setup = ram_save_setup,
    .save_complete = ram_save_complete,
    .load_state = ram_load,
    .load_setup = ram_load_setup,
};

void ram_mig_init(void)
{
    register_savevm_live("ram", 0, 4, &savevm_ram_handlers, NULL);
}
