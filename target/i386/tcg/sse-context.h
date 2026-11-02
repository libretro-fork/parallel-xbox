/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_I386_SSE_CONTEXT_H
#define XEMU_I386_SSE_CONTEXT_H

#if defined(XBOX) && defined(__x86_64__)
/* The saved caller state lives on cpu_exec's stack, outside the guest-reset
 * region. Longjmp cleanup stays inside cpu_exec_setjmp, so normal and fault
 * exits both return through the matching restore. Do not harvest flags here:
 * device helpers and x87 conversions may have used the host FP unit too. */
static inline uint32_t xemu_sse_context_enter(CPUX86State *env)
{
    uint32_t saved;
    uint32_t control = 0x1f80;
    __asm__ volatile("stmxcsr %0" : "=m"(saved) : : "memory");
    if ((saved & ~UINT32_C(0x3f)) != control) {
        __asm__ volatile("ldmxcsr %0" : : "m"(control) : "memory");
    }
    env->xemu_host_mxcsr = control;
    return saved;
}

static inline void xemu_sse_context_exit(CPUX86State *env, uint32_t saved)
{
    __asm__ volatile("ldmxcsr %0" : : "m"(saved) : "memory");
    /* Host code can change MXCSR before the next run. A helper invoked
     * outside the execution scope must probe instead of trusting a cache. */
    env->xemu_host_mxcsr = 0;
}
#endif
#endif
