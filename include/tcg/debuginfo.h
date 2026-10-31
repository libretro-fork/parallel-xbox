/*
 * Debug information support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TCG_DEBUGINFO_H
#define TCG_DEBUGINFO_H

#include "qemu/bitops.h"

/*
 * Debuginfo describing a certain address.
 */
struct debuginfo_query {
    uint64_t address;    /* Input: address. */
    int flags;           /* Input: debuginfo subset. */
    const char *symbol;  /* Symbol that the address is part of. */
    uint64_t offset;     /* Offset from the symbol. */
    const char *file;    /* Source file associated with the address. */
    int line;            /* Line number in the source file. */
};

/*
 * Debuginfo subsets.
 */
#define DEBUGINFO_SYMBOL BIT(1)
#define DEBUGINFO_LINE   BIT(2)

static inline void debuginfo_report_elf(const char *image_name, int image_fd,
                                        uint64_t load_bias)
{
}

static inline void debuginfo_lock(void)
{
}

static inline void debuginfo_query(struct debuginfo_query *q, size_t n)
{
}

static inline void debuginfo_unlock(void)
{
}

#endif
