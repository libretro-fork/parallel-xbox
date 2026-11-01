/*
 * icount - Instruction Counter API
 * CPU timers state API
 *
 * Copyright 2020 SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef EXEC_ICOUNT_H
#define EXEC_ICOUNT_H

/**
 * ICountMode: icount enablement state:
 *
 * @ICOUNT_DISABLED: Disabled - Do not count executed instructions.
 * @ICOUNT_PRECISE: Enabled - Fixed Xbox conversion of instructions to nanoseconds
 * @ICOUNT_ADAPTATIVE: Reserved for legacy VMState metadata; not enabled by the core
 */
typedef enum {
    ICOUNT_DISABLED = 0,
    ICOUNT_PRECISE,
    ICOUNT_ADAPTATIVE,
} ICountMode;

#ifdef CONFIG_TCG
extern ICountMode use_icount;
#define icount_enabled() (use_icount)
#else
#define icount_enabled() ICOUNT_DISABLED
#endif

/* Protect the CONFIG_USER_ONLY test vs poisoning. */
#if defined(COMPILING_PER_TARGET) || defined(COMPILING_SYSTEM_VS_USER)
#endif

/*
 * Update the icount with the executed instructions. Called by
 * cpus-tcg vCPU thread so the main-loop can see time has moved forward.
 */
void icount_update(CPUState *cpu);

/* get raw icount value */
int64_t icount_get_raw(void);

/* return the virtual CPU time in ns, based on the instruction counter. */
int64_t icount_get(void);
/* Convert instructions using the fixed Xbox rate, independent of host time. */
int64_t icount_to_ns(int64_t icount);

/* Initialize the core's fixed Xbox instruction clock. No host pacing or
 * QEMU command-line timing policy participates in frame execution. */
void libretro_instruction_clock_init(void);

/* used by tcg vcpu thread to calc icount budget */
int64_t icount_round(int64_t count);
void icount_skip_to(int64_t vt);
/* A scheduled bottom half ends the current instruction slice. */
void icount_notify_exit(void);

#endif /* EXEC_ICOUNT_H */
