/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_I386_SSE_NATIVE_H
#define XEMU_I386_SSE_NATIVE_H

#if defined(XBOX) && defined(__x86_64__)
/* Unsigned ranges accept positive normals; signed zeros are separate. */
#define XEMU_SQRT_ORDINARY32(v) \
    (((uint32_t)(v) - UINT32_C(0x00800000)) < UINT32_C(0x7f000000) || \
     float32_is_zero(v))
#define XEMU_SQRT_ORDINARY64(v) \
    (((uint32_t)((v) >> 32) - UINT32_C(0x00100000)) < UINT32_C(0x7fe00000) || \
     float64_is_zero(v))

/* Keep CSR switching and arithmetic in one asm statement: a compiler must
 * not hoist the operation out of its rounding/exception scope. These local
 * scopes also prevent frontend or device FP flags from becoming guest flags.
 * A positive normal square root stays normal; signed zero stays zero. Only
 * inexact can be raised, so flushing and tininess policies are irrelevant. */
#define XEMU_SSE_SQRT(bits, type, insn)                                   \
static inline float##bits xemu_sse_sqrt##bits(float##bits value,           \
                                             float_status *status)      \
{                                                                       \
    if (likely(glue(XEMU_SQRT_ORDINARY, bits)(value)) &&                   \
        likely(status->float_rounding_mode <= float_round_to_zero)) {     \
        union {float##bits bits_value; type host;} result =              \
            {.bits_value = value};                                      \
        struct {uint32_t saved, raised, control;} csr;                    \
        csr.control = 0x1f80u |                                          \
                      ((unsigned)status->float_rounding_mode << 13);      \
        __asm__ volatile("stmxcsr %0" : "=m"(csr.saved) : : "memory");     \
        if ((get_float_exception_flags(status) & float_flag_inexact) &&  \
            (csr.saved & 0x7020u) ==                                    \
                ((csr.control & 0x6000u) | 0x1020u)) {                    \
            __asm__ volatile(insn " %0, %0"                             \
                             : "+x"(result.host) : : "memory");         \
        } else {                                                        \
            __asm__ volatile("ldmxcsr %3\n\t"                           \
                             insn " %0, %0\n\t"                         \
                             "stmxcsr %1\n\t"                           \
                             "ldmxcsr %2"                               \
                             : "+x"(result.host), "=m"(csr.raised)      \
                             : "m"(csr.saved), "m"(csr.control)          \
                             : "memory");                                \
            if (csr.raised & 0x20u) {                                    \
                float_raise(float_flag_inexact, status);                 \
            }                                                           \
        }                                                               \
        return result.bits_value;                                       \
    }                                                                   \
    /* SoftFloat itself may use host FP shortcuts. Give those a clean     \
     * nearest/no-flush environment too, then restore the caller exactly. */ \
    struct {uint32_t saved, control;} fallback_csr = {.control = 0x1f80};  \
    __asm__ volatile("stmxcsr %0\n\tldmxcsr %1"                           \
                     : "=m"(fallback_csr.saved)                           \
                     : "m"(fallback_csr.control) : "memory");            \
    float##bits result = float##bits##_sqrt(value, status);               \
    __asm__ volatile("ldmxcsr %0" : : "m"(fallback_csr.saved) : "memory"); \
    return result;                                                      \
}
XEMU_SSE_SQRT(32, float, "sqrtss")
XEMU_SSE_SQRT(64, double, "sqrtsd")
#undef XEMU_SSE_SQRT

/* Batch native lanes so rounding isolation is paid once per 128-bit group.
 * With inexact already sticky in both states, matching host rounding and
 * masked precision, this operation cannot change either state's flags. */
#define XEMU_SSE_SQRT_GROUP(bits, type, count, insn)                       \
static inline bool xemu_sse_sqrt##bits##_group(                           \
    void *dst, const void *src, float_status *status)                     \
{                                                                       \
    type value __attribute__((vector_size(16)));                        \
    if (status->float_rounding_mode > float_round_to_zero) {              \
        return false;                                                   \
    }                                                                   \
    _Pragma("GCC unroll 4")                                              \
    for (unsigned i = 0; i < count; i++) {                               \
        float##bits element;                                            \
        memcpy(&element, (const char *)src + i * sizeof(element),         \
               sizeof(element));                                        \
        if (!glue(XEMU_SQRT_ORDINARY, bits)(element)) {                    \
            return false;                                               \
        }                                                               \
    }                                                                   \
    memcpy(&value, src, 16);                                             \
    struct {uint32_t saved, raised, control;} csr;                        \
    csr.control = 0x1f80u |                                              \
                  ((unsigned)status->float_rounding_mode << 13);          \
    __asm__ volatile("stmxcsr %0" : "=m"(csr.saved) : : "memory");         \
    if ((get_float_exception_flags(status) & float_flag_inexact) &&      \
        (csr.saved & 0x7020u) == ((csr.control & 0x6000u) | 0x1020u)) {   \
        __asm__ volatile(insn " %0, %0"                                 \
                         : "+x"(value) : : "memory");            \
    } else {                                                            \
        __asm__ volatile("ldmxcsr %3\n\t"                               \
                         insn " %0, %0\n\t"                             \
                         "stmxcsr %1\n\t"                               \
                         "ldmxcsr %2"                                   \
                         : "+x"(value), "=m"(csr.raised)          \
                         : "m"(csr.saved), "m"(csr.control)              \
                         : "memory");                                    \
        if (csr.raised & 0x20u) {                                        \
            float_raise(float_flag_inexact, status);                     \
        }                                                               \
    }                                                                   \
    memcpy(dst, &value, 16);                                             \
    return true;                                                        \
}
XEMU_SSE_SQRT_GROUP(32, float, 4, "sqrtps")
XEMU_SSE_SQRT_GROUP(64, double, 2, "sqrtpd")
#undef XEMU_SSE_SQRT_GROUP
#undef XEMU_SQRT_ORDINARY32
#undef XEMU_SQRT_ORDINARY64

#else
#define xemu_sse_sqrt32(value, status) float32_sqrt(value, status)
#define xemu_sse_sqrt64(value, status) float64_sqrt(value, status)
#endif
#endif
