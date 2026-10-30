/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2011-2015 Red Hat Inc
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

#ifndef QEMU_MIGRATION_RAM_H
#define QEMU_MIGRATION_RAM_H

#include "qapi/qapi-types-migration.h"
#include "exec/cpu-common.h"
#include "io/channel.h"

/*
 * RAM_SAVE_FLAG_ZERO used to be named RAM_SAVE_FLAG_COMPRESS, it
 * worked for pages that were filled with the same char.  We switched
 * it to only search for the zero value.  And to avoid confusion with
 * RAM_SAVE_FLAG_COMPRESS_PAGE just rename it.
 *
 * RAM_SAVE_FLAG_FULL (0x01) was obsoleted in 2009.
 *
 * RAM_SAVE_FLAG_COMPRESS_PAGE (0x100) was removed in QEMU 9.1.
 *
 * RAM_SAVE_FLAG_HOOK is only used in RDMA. Whenever this is found in the
 * data stream, the flags will be passed to rdma functions in the
 * incoming-migration side.
 *
 * We can't use any flag that is bigger than 0x200, because the flags are
 * always assumed to be encoded in a ramblock address offset, which is
 * multiple of PAGE_SIZE.  Here it means QEMU supports migration with any
 * architecture that has PAGE_SIZE>=1K (0x400).
 */
#define RAM_SAVE_FLAG_ZERO                    0x002
#define RAM_SAVE_FLAG_MEM_SIZE                0x004
#define RAM_SAVE_FLAG_PAGE                    0x008
#define RAM_SAVE_FLAG_EOS                     0x010
#define RAM_SAVE_FLAG_CONTINUE                0x020
#define RAM_SAVE_FLAG_XBZRLE                  0x040
#define RAM_SAVE_FLAG_HOOK                    0x080
#define RAM_SAVE_FLAG_MULTIFD_FLUSH           0x200

extern XBZRLECacheStats xbzrle_counters;

/* Should be holding either ram_list.mutex, or the RCU lock. */
#define RAMBLOCK_FOREACH_NOT_IGNORED(block)            \
    INTERNAL_RAMBLOCK_FOREACH(block)                   \
        if (migrate_ram_is_ignored(block)) {} else

#define RAMBLOCK_FOREACH_MIGRATABLE(block)             \
    INTERNAL_RAMBLOCK_FOREACH(block)                   \
        if (!qemu_ram_is_migratable(block)) {} else

void ram_mig_init(void);

/* For outgoing discard bitmap */
/* For incoming postcopy discard */




/* ram cache */

/* Background snapshot */

#endif
