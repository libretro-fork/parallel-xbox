#ifndef QEMU_LOG_H
#define QEMU_LOG_H

/* A small part of this API is split into its own header */
#include "qemu/log-for-trace.h"

/* 
 * The new API:
 */

#define CPU_LOG_TB_OUT_ASM (1u << 0)
#define CPU_LOG_TB_IN_ASM  (1u << 1)
#define CPU_LOG_TB_OP      (1u << 2)
#define CPU_LOG_TB_OP_OPT  (1u << 3)
#define CPU_LOG_INT        (1u << 4)
#define CPU_LOG_EXEC       (1u << 5)
#define CPU_LOG_PCALL      (1u << 6)
#define CPU_LOG_TB_CPU     (1u << 8)
#define CPU_LOG_RESET      (1u << 9)
#define LOG_UNIMP          (1u << 10)
#define LOG_GUEST_ERROR    (1u << 11)
#define CPU_LOG_MMU        (1u << 12)
#define CPU_LOG_TB_NOCHAIN (1u << 13)
#define CPU_LOG_PAGE       (1u << 14)
/* LOG_TRACE (1 << 15) is defined in log-for-trace.h */
#define CPU_LOG_TB_OP_IND  (1u << 16)
#define CPU_LOG_TB_FPU     (1u << 17)
#define CPU_LOG_PLUGIN     (1u << 18)
/* LOG_STRACE is used for user-mode strace logging. */
#define LOG_STRACE         (1u << 19)
#define LOG_PER_THREAD     (1u << 20)
#define CPU_LOG_TB_VPU     (1u << 21)
#define LOG_TB_OP_PLUGIN   (1u << 22)
#define LOG_INVALID_MEM    (1u << 23)

/* The standalone debug-file logger is disabled in this core. Preserve
 * the shared translation helpers' empty-output contract without a FILE,
 * mutex, TLS state or stream lock. Frontend/error logging is separate. */
static inline FILE *qemu_log_trylock(void)
{
    return NULL;
}

static inline void qemu_log_unlock(FILE *fd)
{
    (void)fd;
}

static inline bool qemu_log_in_addr_range(uint64_t addr)
{
    (void)addr;
    return true;
}

/* Logging functions: */

/* log only if a bit is set on the current loglevel mask:
 * @mask: bit to check in the mask
 * @fmt: printf-style format string
 * @args: optional arguments for format string
 */
#define qemu_log_mask(MASK, FMT, ...)                   \
    do {                                                \
        if (unlikely(qemu_loglevel_mask(MASK))) {       \
            qemu_log(FMT, ## __VA_ARGS__);              \
        }                                               \
    } while (0)

/* log only if a bit is set on the current loglevel mask
 * and we are in the address range we care about:
 * @mask: bit to check in the mask
 * @addr: address to check in dfilter
 * @fmt: printf-style format string
 * @args: optional arguments for format string
 */
#define qemu_log_mask_and_addr(MASK, ADDR, FMT, ...)    \
    do {                                                \
        if (unlikely(qemu_loglevel_mask(MASK)) &&       \
                     qemu_log_in_addr_range(ADDR)) {    \
            qemu_log(FMT, ## __VA_ARGS__);              \
        }                                               \
    } while (0)

#endif
