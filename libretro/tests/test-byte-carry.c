/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#if TEST_TARGET_BITS == 64
typedef uint64_t target_ulong;
#else
typedef uint32_t target_ulong;
#endif
typedef int TCGv;
typedef enum { TCG_COND_NE, TCG_COND_NEVER, TCG_COND_TSTNE,
               TCG_COND_LTU } TCGCond;
typedef struct { int cf_entry_class; } DisasContext;
#define CC_C 1
static target_ulong values[8];
static int next_reg, flushes;
static TCGv cpu_cc_dst = 0, cpu_cc_src = 1;
static TCGv tcg_temp_new(void) { assert(next_reg < 8); return next_reg++; }
static void tcg_gen_add_tl(TCGv d, TCGv a, TCGv b) { values[d] = values[a] + values[b]; }
static void tcg_gen_ext8u_tl(TCGv d, TCGv a) { values[d] = (uint8_t)values[a]; }
static void tcg_gen_ext32u_tl(TCGv d, TCGv a) { values[d] = (uint32_t)values[a]; }
static void gen_flush_fp(DisasContext *s) { (void)s; flushes++; }
#define g_assert_not_reached() abort()
/* PRODUCTION */
int main(void)
{
    static const uint64_t upper[] = {
        0, UINT64_MAX, UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210),
        UINT64_C(0x8000000000000000), UINT64_C(0x0000000080000000),
        UINT64_C(0x5555555555555555), UINT64_C(0xaaaaaaaaaaaaaaaa)
    };
    assert(TB_CF_SUBB == 6 && TB_CF_ANY == 7);
    assert(x86_cc_op_cf_class(CC_OP_SUBB) == TB_CF_SUBB);
    assert(x86_cc_op_cf_class(CC_OP_SUBW) == TB_CF_UNKNOWN);
    assert(x86_cc_op_cf_class(CC_OP_SUBL) == TB_CF_SUBL);
    assert(x86_cc_op_cf_class(CC_OP_SUBQ) == TB_CF_UNKNOWN);
    assert(x86_cc_op_cf_class(CC_OP_EFLAGS) == TB_CF_SRC_LSB);
    for (unsigned k = 0; k < 8; k++) {
        uint32_t ambient = (uint32_t)upper[k] & ~TB_CF_FLAGS_MASK;
        for (unsigned cls = 0; cls < 8; cls++) {
            uint32_t f = ambient | tb_cf_flags(cls);
            assert(tb_cf_class_of_flags(f) == (int)cls);
            assert((f & ~TB_CF_FLAGS_MASK) == ambient);
            assert((tb_cf_flags(cls) & ~TB_CF_FLAGS_MASK) == 0);
        }
        for (unsigned lhs = 0; lhs < 256; lhs++) {
            for (unsigned rhs = 0; rhs < 256; rhs++) {
                DisasContext s = { .cf_entry_class = TB_CF_SUBB };
                target_ulong dst = (upper[k] & ~UINT64_C(255)) | (uint8_t)(lhs - rhs);
                target_ulong src = (upper[7-k] & ~UINT64_C(255)) | rhs;
                values[0] = dst; values[1] = src; next_reg = 2; flushes = 0;
                CCPrepare c = gen_prepare_eflags_c_entry(&s);
                assert(c.cond == TCG_COND_LTU && c.use_reg2);
                /* Oracle uses the original operands, not lazy-state reconstruction. */
                assert((values[c.reg] < values[c.reg2]) == (lhs < rhs));
                assert(values[0] == dst && values[1] == src);
                assert(flushes == 1);
            }
        }
    }
    printf("byte carry: 524288 operand/dirty-bit cases, metadata roundtrips, target=%d passed\n", TEST_TARGET_BITS);
    return 0;
}
