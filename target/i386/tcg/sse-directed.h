/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_I386_SSE_DIRECTED_H
#define XEMU_I386_SSE_DIRECTED_H
#if defined(__x86_64__)
/* For exponents [-60,60] (f32) or [-500,500] (f64), products cannot
 * overflow/underflow and add/sub cancellation cannot become subnormal.
 * Signed zero is also safe. Only inexact can be raised, regardless of
 * guest flushing/tininess policy. Other inputs retain SoftFloat. */
static inline bool xemu_sse_bounded32(float32 v)
{
    unsigned exponent = (v >> 23) & 255;
    return exponent - 67u <= 120u || float32_is_zero(v);
}
static inline bool xemu_sse_bounded64(float64 v)
{
    unsigned exponent = (v >> 52) & 2047;
    return exponent - 523u <= 1000u || float64_is_zero(v);
}
static inline bool xemu_sse_directed_mode(const float_status *s)
{
    return (unsigned)s->float_rounding_mode - 1u < 3u;
}

/* Keep loads of the control word, arithmetic and flag capture ordered.
 * Restore the entire original MXCSR, including the host's sticky flags. */
#define XEMU_SSE_DIRECTED_SCOPE(value, operand, status, insn) do {       \
    struct {uint32_t saved, raised, control;} csr;                       \
    csr.control = 0x1f80u |                                             \
                  ((unsigned)(status)->float_rounding_mode << 13);      \
    __asm__ volatile("stmxcsr %1\n\t"                               \
                     "ldmxcsr %3\n\t"                               \
                     insn " %4, %0\n\t"                             \
                     "stmxcsr %2\n\t"                               \
                     "ldmxcsr %1"                                   \
                     : "+x"(value), "=m"(csr.saved), "=m"(csr.raised) \
                     : "m"(csr.control), "x"(operand)                \
                     : "memory");                                    \
    if (csr.raised & 0x20u) {                                           \
        float_raise(float_flag_inexact, status);                        \
    }                                                                  \
} while (0)

#define XEMU_SSE_DIRECTED_SCALAR(bits, type, name, insn)                 \
static __attribute__((noinline)) float##bits xemu_sse_directed_##name##bits(                \
    float##bits a, float##bits b, float_status *status)                  \
{                                                                      \
    if (xemu_sse_directed_mode(status) &&                               \
        xemu_sse_bounded##bits(a) && xemu_sse_bounded##bits(b)) {         \
        union {float##bits bits_value; type host;} x = {.bits_value=a}; \
        union {float##bits bits_value; type host;} y = {.bits_value=b}; \
        XEMU_SSE_DIRECTED_SCOPE(x.host, y.host, status, insn);           \
        return x.bits_value;                                           \
    }                                                                  \
    return float##bits##_##name(a, b, status);                          \
}
#define XEMU_SSE_DIRECTED_GROUP(bits, type, count, name, insn)           \
static inline bool xemu_sse_directed_##name##bits##_group(               \
    void *dst, const void *left, const void *right, float_status *status) \
{                                                                      \
    if (!xemu_sse_directed_mode(status)) {                              \
        return false;                                                  \
    }                                                                  \
    _Pragma("GCC unroll 4")                                             \
    for (unsigned i=0; i<count; i++) {                                  \
        float##bits a, b;                                              \
        memcpy(&a, (const char *)left+i*sizeof(a), sizeof(a));           \
        memcpy(&b, (const char *)right+i*sizeof(b), sizeof(b));          \
        if (!xemu_sse_bounded##bits(a) || !xemu_sse_bounded##bits(b)) {   \
            return false;                                              \
        }                                                              \
    }                                                                  \
    type a __attribute__((vector_size(16)));                            \
    type b __attribute__((vector_size(16)));                            \
    memcpy(&a, left, 16); memcpy(&b, right, 16);                         \
    XEMU_SSE_DIRECTED_SCOPE(a, b, status, insn);                         \
    memcpy(dst, &a, 16);                                                \
    return true;                                                       \
}
#define XEMU_SSE_DIRECTED_VECTOR(bits, name)                             \
static __attribute__((noinline)) void                                   \
xemu_sse_directed_##name##bits##_vector(                                \
    void *dst, const void *left, const void *right, float_status *status, \
    unsigned lanes)                                                    \
{                                                                      \
    for (unsigned lane=0; lane<lanes; lane+=128/bits) {                  \
        char *d=(char *)dst+lane*(bits/8);                              \
        const char *a=(const char *)left+lane*(bits/8);                  \
        const char *b=(const char *)right+lane*(bits/8);                 \
        if (!xemu_sse_directed_##name##bits##_group(d,a,b,status)) {     \
            for (unsigned i=0; i<128/bits; i++) {                       \
                float##bits x,y,z;                                     \
                memcpy(&x,a+i*sizeof(x),sizeof(x));                    \
                memcpy(&y,b+i*sizeof(y),sizeof(y));                    \
                z=xemu_sse_directed_##name##bits(x,y,status);           \
                memcpy(d+i*sizeof(z),&z,sizeof(z));                    \
            }                                                          \
        }                                                              \
    }                                                                  \
}
#define XEMU_SSE_DIRECTED_OP(name)                                      \
    XEMU_SSE_DIRECTED_SCALAR(32,float,name,#name "ss")                   \
    XEMU_SSE_DIRECTED_SCALAR(64,double,name,#name "sd")                  \
    XEMU_SSE_DIRECTED_GROUP(32,float,4,name,#name "ps")                  \
    XEMU_SSE_DIRECTED_GROUP(64,double,2,name,#name "pd")                  \
    XEMU_SSE_DIRECTED_VECTOR(32,name)                                    \
    XEMU_SSE_DIRECTED_VECTOR(64,name)
XEMU_SSE_DIRECTED_OP(add)
XEMU_SSE_DIRECTED_OP(sub)
XEMU_SSE_DIRECTED_OP(mul)
#undef XEMU_SSE_DIRECTED_VECTOR
#undef XEMU_SSE_DIRECTED_OP
#undef XEMU_SSE_DIRECTED_GROUP
#undef XEMU_SSE_DIRECTED_SCALAR
#undef XEMU_SSE_DIRECTED_SCOPE
#endif
#endif
