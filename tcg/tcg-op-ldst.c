/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
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
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-mo.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "exec/plugin-gen.h"
#include "tcg-internal.h"
#include "tcg-has.h"
#include "tcg-target-mo.h"
#include "exec/translator.h"
/* the libretro core's idle detection: every guest store is announced */
#define LR_QUIET_STORE() lr_quiet_store_emitted()

static void check_max_alignment(unsigned a_bits)
{
    /*
     * The requested alignment cannot overlap the TLB flags.
     * FIXME: Must keep the count up-to-date with "exec/tlb-flags.h".
     */
    if (tcg_use_softmmu) {
        tcg_debug_assert(a_bits + 5 <= TARGET_PAGE_BITS);
    }
}

static MemOp tcg_canonicalize_memop(MemOp op, bool is64, bool st)
{
    unsigned a_bits = memop_alignment_bits(op);

    check_max_alignment(a_bits);

    /* Prefer MO_ALIGN+MO_XX over MO_ALIGN_XX+MO_XX */
    if (a_bits == (op & MO_SIZE)) {
        op = (op & ~MO_AMASK) | MO_ALIGN;
    }

    switch (op & MO_SIZE) {
    case MO_8:
        op &= ~MO_BSWAP;
        break;
    case MO_16:
        break;
    case MO_32:
        if (!is64) {
            op &= ~MO_SIGN;
        }
        break;
    case MO_64:
        if (is64) {
            op &= ~MO_SIGN;
            break;
        }
        /* fall through */
    default:
        g_assert_not_reached();
    }
    if (st) {
        op &= ~MO_SIGN;
    }

    /* The core's TCG is serial (one vCPU): no atomicity needed. */
    op &= ~MO_ATOM_MASK;
    op |= MO_ATOM_NONE;

    return op;
}

static void gen_ldst1(TCGOpcode opc, TCGType type, TCGTemp *v,
                      TCGTemp *addr, MemOpIdx oi)
{
    TCGOp *op = tcg_gen_op3(opc, type, temp_arg(v), temp_arg(addr), oi);
    TCGOP_FLAGS(op) = get_memop(oi) & MO_SIZE;
}

static void gen_ldst2(TCGOpcode opc, TCGType type, TCGTemp *vl, TCGTemp *vh,
                      TCGTemp *addr, MemOpIdx oi)
{
    TCGOp *op = tcg_gen_op4(opc, type, temp_arg(vl), temp_arg(vh),
                            temp_arg(addr), oi);
    TCGOP_FLAGS(op) = get_memop(oi) & MO_SIZE;
}

static void gen_ld_i64(TCGv_i64 v, TCGTemp *addr, MemOpIdx oi)
{
    if (TCG_TARGET_REG_BITS == 32) {
        gen_ldst2(INDEX_op_qemu_ld2, TCG_TYPE_I64,
                  tcgv_i32_temp(TCGV_LOW(v)), tcgv_i32_temp(TCGV_HIGH(v)),
                  addr, oi);
    } else {
        gen_ldst1(INDEX_op_qemu_ld, TCG_TYPE_I64, tcgv_i64_temp(v), addr, oi);
    }
}

static void gen_st_i64(TCGv_i64 v, TCGTemp *addr, MemOpIdx oi)
{
    if (TCG_TARGET_REG_BITS == 32) {
        gen_ldst2(INDEX_op_qemu_st2, TCG_TYPE_I64,
                  tcgv_i32_temp(TCGV_LOW(v)), tcgv_i32_temp(TCGV_HIGH(v)),
                  addr, oi);
    } else {
        gen_ldst1(INDEX_op_qemu_st, TCG_TYPE_I64, tcgv_i64_temp(v), addr, oi);
    }
}

static void tcg_gen_req_mo(TCGBar type)
{
    type &= tcg_ctx->guest_mo;
    type &= ~TCG_TARGET_DEFAULT_MO;
    if (type) {
        tcg_gen_mb(type | TCG_BAR_SC);
    }
}

static TCGTemp *tci_extend_addr(TCGTemp *addr)
{
#ifdef CONFIG_TCG_INTERPRETER
    /*
     * 64-bit interpreter requires 64-bit addresses.
     * Compare to the extension performed by tcg_out_{ld,st}_helper_args
     * for native code generation.
     */
    if (TCG_TARGET_REG_BITS == 64 && tcg_ctx->addr_type == TCG_TYPE_I32) {
        TCGv_i64 temp = tcg_temp_ebb_new_i64();
        tcg_gen_extu_i32_i64(temp, temp_tcgv_i32(addr));
        return tcgv_i64_temp(temp);
    }
#endif
    return addr;
}

static void maybe_free_addr(TCGTemp *addr, TCGTemp *copy)
{
    if (addr != copy) {
        tcg_temp_free_internal(copy);
    }
}

/* Only required for loads, where value might overlap addr. */
static TCGv_i64 plugin_maybe_preserve_addr(TCGTemp *addr)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        /* Save a copy of the vaddr for use after a load.  */
        TCGv_i64 temp = tcg_temp_ebb_new_i64();
        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            tcg_gen_extu_i32_i64(temp, temp_tcgv_i32(addr));
        } else {
            tcg_gen_mov_i64(temp, temp_tcgv_i64(addr));
        }
        return temp;
    }
#endif
    return NULL;
}

#ifdef CONFIG_PLUGIN
static void
plugin_gen_mem_callbacks(TCGv_i64 copy_addr, TCGTemp *orig_addr, MemOpIdx oi,
                         enum qemu_plugin_mem_rw rw)
{
    qemu_plugin_meminfo_t info = make_plugin_meminfo(oi, rw);

    if (tcg_ctx->addr_type == TCG_TYPE_I32) {
        if (!copy_addr) {
            copy_addr = tcg_temp_ebb_new_i64();
            tcg_gen_extu_i32_i64(copy_addr, temp_tcgv_i32(orig_addr));
        }
        tcg_gen_plugin_mem_cb(copy_addr, info);
        tcg_temp_free_i64(copy_addr);
    } else {
        if (copy_addr) {
            tcg_gen_plugin_mem_cb(copy_addr, info);
            tcg_temp_free_i64(copy_addr);
        } else {
            tcg_gen_plugin_mem_cb(temp_tcgv_i64(orig_addr), info);
        }
    }
}
#endif

static void
plugin_gen_mem_callbacks_i32(TCGv_i32 val,
                             TCGv_i64 copy_addr, TCGTemp *orig_addr,
                             MemOpIdx oi, enum qemu_plugin_mem_rw rw)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        tcg_gen_st_i32(val, tcg_env,
                       offsetof(CPUState, neg.plugin_mem_value_low) -
                       sizeof(CPUState) + (HOST_BIG_ENDIAN * 4));
        plugin_gen_mem_callbacks(copy_addr, orig_addr, oi, rw);
    }
#endif
}

static void
plugin_gen_mem_callbacks_i64(TCGv_i64 val,
                             TCGv_i64 copy_addr, TCGTemp *orig_addr,
                             MemOpIdx oi, enum qemu_plugin_mem_rw rw)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        tcg_gen_st_i64(val, tcg_env,
                       offsetof(CPUState, neg.plugin_mem_value_low) -
                       sizeof(CPUState));
        plugin_gen_mem_callbacks(copy_addr, orig_addr, oi, rw);
    }
#endif
}

static void
plugin_gen_mem_callbacks_i128(TCGv_i128 val,
                             TCGv_i64 copy_addr, TCGTemp *orig_addr,
                             MemOpIdx oi, enum qemu_plugin_mem_rw rw)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        tcg_gen_st_i64(TCGV128_LOW(val), tcg_env,
                       offsetof(CPUState, neg.plugin_mem_value_low) -
                       sizeof(CPUState));
        tcg_gen_st_i64(TCGV128_HIGH(val), tcg_env,
                       offsetof(CPUState, neg.plugin_mem_value_high) -
                       sizeof(CPUState));
        plugin_gen_mem_callbacks(copy_addr, orig_addr, oi, rw);
    }
#endif
}

static void tcg_gen_qemu_ld_i32_int(TCGv_i32 val, TCGTemp *addr,
                                    TCGArg idx, MemOp memop)
{
    MemOp orig_memop;
    MemOpIdx orig_oi, oi;
    TCGv_i64 copy_addr;
    TCGTemp *addr_new;

    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);
    orig_memop = memop = tcg_canonicalize_memop(memop, 0, 0);
    orig_oi = oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        memop &= ~MO_BSWAP;
        /* The bswap primitive benefits from zero-extended input.  */
        if ((memop & MO_SSIZE) == MO_SW) {
            memop &= ~MO_SIGN;
        }
        oi = make_memop_idx(memop, idx);
    }

    addr_new = tci_extend_addr(addr);
    copy_addr = plugin_maybe_preserve_addr(addr);
    gen_ldst1(INDEX_op_qemu_ld, TCG_TYPE_I32, tcgv_i32_temp(val), addr_new, oi);
    plugin_gen_mem_callbacks_i32(val, copy_addr, addr, orig_oi,
                                 QEMU_PLUGIN_MEM_R);
    maybe_free_addr(addr, addr_new);

    if ((orig_memop ^ memop) & MO_BSWAP) {
        switch (orig_memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i32(val, val, (orig_memop & MO_SIGN
                                           ? TCG_BSWAP_IZ | TCG_BSWAP_OS
                                           : TCG_BSWAP_IZ | TCG_BSWAP_OZ));
            break;
        case MO_32:
            tcg_gen_bswap32_i32(val, val);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

void tcg_gen_qemu_ld_i32_chk(TCGv_i32 val, TCGTemp *addr, TCGArg idx,
                             MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) <= MO_32);
    tcg_gen_qemu_ld_i32_int(val, addr, idx, memop);
}

static void tcg_gen_qemu_st_i32_int(TCGv_i32 val, TCGTemp *addr,
                                    TCGArg idx, MemOp memop)
{
    TCGv_i32 swap = NULL;
    MemOpIdx orig_oi, oi;
    TCGTemp *addr_new;

    tcg_gen_req_mo(TCG_MO_LD_ST | TCG_MO_ST_ST);
    memop = tcg_canonicalize_memop(memop, 0, 1);
    orig_oi = oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        swap = tcg_temp_ebb_new_i32();
        switch (memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i32(swap, val, 0);
            break;
        case MO_32:
            tcg_gen_bswap32_i32(swap, val);
            break;
        default:
            g_assert_not_reached();
        }
        val = swap;
        memop &= ~MO_BSWAP;
        oi = make_memop_idx(memop, idx);
    }

    addr_new = tci_extend_addr(addr);
    gen_ldst1(INDEX_op_qemu_st, TCG_TYPE_I32, tcgv_i32_temp(val), addr_new, oi);
    plugin_gen_mem_callbacks_i32(val, NULL, addr, orig_oi, QEMU_PLUGIN_MEM_W);
    maybe_free_addr(addr, addr_new);

    if (swap) {
        tcg_temp_free_i32(swap);
    }
}

void tcg_gen_qemu_st_i32_chk(TCGv_i32 val, TCGTemp *addr, TCGArg idx,
                             MemOp memop, TCGType addr_type)
{
    LR_QUIET_STORE();
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) <= MO_32);
    tcg_gen_qemu_st_i32_int(val, addr, idx, memop);
}

static void tcg_gen_qemu_ld_i64_int(TCGv_i64 val, TCGTemp *addr,
                                    TCGArg idx, MemOp memop)
{
    MemOp orig_memop;
    MemOpIdx orig_oi, oi;
    TCGv_i64 copy_addr;
    TCGTemp *addr_new;

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_qemu_ld_i32_int(TCGV_LOW(val), addr, idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_i32(TCGV_HIGH(val), TCGV_LOW(val), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(val), 0);
        }
        return;
    }

    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);
    orig_memop = memop = tcg_canonicalize_memop(memop, 1, 0);
    orig_oi = oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        memop &= ~MO_BSWAP;
        /* The bswap primitive benefits from zero-extended input.  */
        if ((memop & MO_SIGN) && (memop & MO_SIZE) < MO_64) {
            memop &= ~MO_SIGN;
        }
        oi = make_memop_idx(memop, idx);
    }

    addr_new = tci_extend_addr(addr);
    copy_addr = plugin_maybe_preserve_addr(addr);
    gen_ld_i64(val, addr_new, oi);
    plugin_gen_mem_callbacks_i64(val, copy_addr, addr, orig_oi,
                                 QEMU_PLUGIN_MEM_R);
    maybe_free_addr(addr, addr_new);

    if ((orig_memop ^ memop) & MO_BSWAP) {
        int flags = (orig_memop & MO_SIGN
                     ? TCG_BSWAP_IZ | TCG_BSWAP_OS
                     : TCG_BSWAP_IZ | TCG_BSWAP_OZ);
        switch (orig_memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i64(val, val, flags);
            break;
        case MO_32:
            tcg_gen_bswap32_i64(val, val, flags);
            break;
        case MO_64:
            tcg_gen_bswap64_i64(val, val);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

void tcg_gen_qemu_ld_i64_chk(TCGv_i64 val, TCGTemp *addr, TCGArg idx,
                             MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) <= MO_64);
    tcg_gen_qemu_ld_i64_int(val, addr, idx, memop);
}

static void tcg_gen_qemu_st_i64_int(TCGv_i64 val, TCGTemp *addr,
                                    TCGArg idx, MemOp memop)
{
    LR_QUIET_STORE();
    TCGv_i64 swap = NULL;
    MemOpIdx orig_oi, oi;
    TCGTemp *addr_new;

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_qemu_st_i32_int(TCGV_LOW(val), addr, idx, memop);
        return;
    }

    tcg_gen_req_mo(TCG_MO_LD_ST | TCG_MO_ST_ST);
    memop = tcg_canonicalize_memop(memop, 1, 1);
    orig_oi = oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        swap = tcg_temp_ebb_new_i64();
        switch (memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i64(swap, val, 0);
            break;
        case MO_32:
            tcg_gen_bswap32_i64(swap, val, 0);
            break;
        case MO_64:
            tcg_gen_bswap64_i64(swap, val);
            break;
        default:
            g_assert_not_reached();
        }
        val = swap;
        memop &= ~MO_BSWAP;
        oi = make_memop_idx(memop, idx);
    }

    addr_new = tci_extend_addr(addr);
    gen_st_i64(val, addr_new, oi);
    plugin_gen_mem_callbacks_i64(val, NULL, addr, orig_oi, QEMU_PLUGIN_MEM_W);
    maybe_free_addr(addr, addr_new);

    if (swap) {
        tcg_temp_free_i64(swap);
    }
}

void tcg_gen_qemu_st_i64_chk(TCGv_i64 val, TCGTemp *addr, TCGArg idx,
                             MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) <= MO_64);
    tcg_gen_qemu_st_i64_int(val, addr, idx, memop);
}

/*
 * Return true if @mop, without knowledge of the pointer alignment,
 * does not require 16-byte atomicity, and it would be adventagous
 * to avoid a call to a helper function.
 */
static bool use_two_i64_for_i128(MemOp mop)
{
    /* Two softmmu tlb lookups is larger than one function call. */
    if (tcg_use_softmmu) {
        return false;
    }

    /*
     * For user-only, two 64-bit operations may well be smaller than a call.
     * Determine if that would be legal for the requested atomicity.
     */
    switch (mop & MO_ATOM_MASK) {
    case MO_ATOM_NONE:
    case MO_ATOM_IFALIGN_PAIR:
        return true;
    case MO_ATOM_IFALIGN:
    case MO_ATOM_SUBALIGN:
    case MO_ATOM_WITHIN16:
    case MO_ATOM_WITHIN16_PAIR:
        return false;
    default:
        g_assert_not_reached();
    }
}

static void canonicalize_memop_i128_as_i64(MemOp ret[2], MemOp orig)
{
    MemOp mop_1 = orig, mop_2;

    /* Reduce the size to 64-bit. */
    mop_1 = (mop_1 & ~MO_SIZE) | MO_64;

    /* Retain the alignment constraints of the original. */
    switch (orig & MO_AMASK) {
    case MO_UNALN:
    case MO_ALIGN_2:
    case MO_ALIGN_4:
        mop_2 = mop_1;
        break;
    case MO_ALIGN_8:
        /* Prefer MO_ALIGN+MO_64 to MO_ALIGN_8+MO_64. */
        mop_1 = (mop_1 & ~MO_AMASK) | MO_ALIGN;
        mop_2 = mop_1;
        break;
    case MO_ALIGN:
        /* Second has 8-byte alignment; first has 16-byte alignment. */
        mop_2 = mop_1;
        mop_1 = (mop_1 & ~MO_AMASK) | MO_ALIGN_16;
        break;
    case MO_ALIGN_16:
    case MO_ALIGN_32:
    case MO_ALIGN_64:
        /* Second has 8-byte alignment; first retains original. */
        mop_2 = (mop_1 & ~MO_AMASK) | MO_ALIGN;
        break;
    default:
        g_assert_not_reached();
    }

    /* Use a memory ordering implemented by the host. */
    if ((orig & MO_BSWAP) && !tcg_target_has_memory_bswap(mop_1)) {
        mop_1 &= ~MO_BSWAP;
        mop_2 &= ~MO_BSWAP;
    }

    ret[0] = mop_1;
    ret[1] = mop_2;
}

static void tcg_gen_qemu_ld_i128_int(TCGv_i128 val, TCGTemp *addr,
                                     TCGArg idx, MemOp memop)
{
    MemOpIdx orig_oi;
    TCGv_i64 ext_addr = NULL;
    TCGTemp *addr_new;

    check_max_alignment(memop_alignment_bits(memop));
    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);

    /* The core's TCG is serial (one vCPU): no atomicity needed. */
    memop &= ~MO_ATOM_MASK;
    memop |= MO_ATOM_NONE;
    orig_oi = make_memop_idx(memop, idx);

    /* TODO: For now, force 32-bit hosts to use the helper. */
    if (TCG_TARGET_HAS_qemu_ldst_i128 && TCG_TARGET_REG_BITS == 64) {
        TCGv_i64 lo, hi;
        bool need_bswap = false;
        MemOpIdx oi = orig_oi;

        if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
            lo = TCGV128_HIGH(val);
            hi = TCGV128_LOW(val);
            oi = make_memop_idx(memop & ~MO_BSWAP, idx);
            need_bswap = true;
        } else {
            lo = TCGV128_LOW(val);
            hi = TCGV128_HIGH(val);
        }

        addr_new = tci_extend_addr(addr);
        gen_ldst2(INDEX_op_qemu_ld2, TCG_TYPE_I128, tcgv_i64_temp(lo),
                  tcgv_i64_temp(hi), addr_new, oi);
        maybe_free_addr(addr, addr_new);

        if (need_bswap) {
            tcg_gen_bswap64_i64(lo, lo);
            tcg_gen_bswap64_i64(hi, hi);
        }
    } else if (use_two_i64_for_i128(memop)) {
        MemOp mop[2];
        TCGTemp *addr_p8;
        TCGv_i64 x, y;
        bool need_bswap;

        canonicalize_memop_i128_as_i64(mop, memop);
        need_bswap = (mop[0] ^ memop) & MO_BSWAP;

        /*
         * Since there are no global TCGv_i128, there is no visible state
         * changed if the second load faults.  Load directly into the two
         * subwords.
         */
        if ((memop & MO_BSWAP) == MO_LE) {
            x = TCGV128_LOW(val);
            y = TCGV128_HIGH(val);
        } else {
            x = TCGV128_HIGH(val);
            y = TCGV128_LOW(val);
        }

        addr_new = tci_extend_addr(addr);
        gen_ld_i64(x, addr_new, make_memop_idx(mop[0], idx));
        maybe_free_addr(addr, addr_new);

        if (need_bswap) {
            tcg_gen_bswap64_i64(x, x);
        }

        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            TCGv_i32 t = tcg_temp_ebb_new_i32();
            tcg_gen_addi_i32(t, temp_tcgv_i32(addr), 8);
            addr_p8 = tcgv_i32_temp(t);
        } else {
            TCGv_i64 t = tcg_temp_ebb_new_i64();
            tcg_gen_addi_i64(t, temp_tcgv_i64(addr), 8);
            addr_p8 = tcgv_i64_temp(t);
        }

        addr_new = tci_extend_addr(addr_p8);
        gen_ld_i64(y, addr_new, make_memop_idx(mop[1], idx));
        maybe_free_addr(addr_p8, addr_new);
        tcg_temp_free_internal(addr_p8);

        if (need_bswap) {
            tcg_gen_bswap64_i64(y, y);
        }
    } else {
        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            ext_addr = tcg_temp_ebb_new_i64();
            tcg_gen_extu_i32_i64(ext_addr, temp_tcgv_i32(addr));
            addr = tcgv_i64_temp(ext_addr);
        }
        gen_helper_ld_i128(val, tcg_env, temp_tcgv_i64(addr),
                           tcg_constant_i32(orig_oi));
    }

    plugin_gen_mem_callbacks_i128(val, ext_addr, addr, orig_oi,
                                  QEMU_PLUGIN_MEM_R);
}

void tcg_gen_qemu_ld_i128_chk(TCGv_i128 val, TCGTemp *addr, TCGArg idx,
                              MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) == MO_128);
    tcg_debug_assert((memop & MO_SIGN) == 0);
    tcg_gen_qemu_ld_i128_int(val, addr, idx, memop);
}

static void tcg_gen_qemu_st_i128_int(TCGv_i128 val, TCGTemp *addr,
                                     TCGArg idx, MemOp memop)
{
    LR_QUIET_STORE();
    MemOpIdx orig_oi;
    TCGv_i64 ext_addr = NULL;
    TCGTemp *addr_new;

    check_max_alignment(memop_alignment_bits(memop));
    tcg_gen_req_mo(TCG_MO_ST_LD | TCG_MO_ST_ST);

    /* The core's TCG is serial (one vCPU): no atomicity needed. */
    memop &= ~MO_ATOM_MASK;
    memop |= MO_ATOM_NONE;
    orig_oi = make_memop_idx(memop, idx);

    /* TODO: For now, force 32-bit hosts to use the helper. */

    if (TCG_TARGET_HAS_qemu_ldst_i128 && TCG_TARGET_REG_BITS == 64) {
        TCGv_i64 lo, hi;
        MemOpIdx oi = orig_oi;
        bool need_bswap = false;

        if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
            lo = tcg_temp_ebb_new_i64();
            hi = tcg_temp_ebb_new_i64();
            tcg_gen_bswap64_i64(lo, TCGV128_HIGH(val));
            tcg_gen_bswap64_i64(hi, TCGV128_LOW(val));
            oi = make_memop_idx(memop & ~MO_BSWAP, idx);
            need_bswap = true;
        } else {
            lo = TCGV128_LOW(val);
            hi = TCGV128_HIGH(val);
        }

        addr_new = tci_extend_addr(addr);
        gen_ldst2(INDEX_op_qemu_st2, TCG_TYPE_I128,
                  tcgv_i64_temp(lo), tcgv_i64_temp(hi), addr_new, oi);
        maybe_free_addr(addr, addr_new);

        if (need_bswap) {
            tcg_temp_free_i64(lo);
            tcg_temp_free_i64(hi);
        }
    } else if (use_two_i64_for_i128(memop)) {
        MemOp mop[2];
        TCGTemp *addr_p8;
        TCGv_i64 x, y, b = NULL;

        canonicalize_memop_i128_as_i64(mop, memop);

        if ((memop & MO_BSWAP) == MO_LE) {
            x = TCGV128_LOW(val);
            y = TCGV128_HIGH(val);
        } else {
            x = TCGV128_HIGH(val);
            y = TCGV128_LOW(val);
        }

        if ((mop[0] ^ memop) & MO_BSWAP) {
            b = tcg_temp_ebb_new_i64();
            tcg_gen_bswap64_i64(b, x);
            x = b;
        }

        addr_new = tci_extend_addr(addr);
        gen_st_i64(x, addr_new, make_memop_idx(mop[0], idx));
        maybe_free_addr(addr, addr_new);

        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            TCGv_i32 t = tcg_temp_ebb_new_i32();
            tcg_gen_addi_i32(t, temp_tcgv_i32(addr), 8);
            addr_p8 = tcgv_i32_temp(t);
        } else {
            TCGv_i64 t = tcg_temp_ebb_new_i64();
            tcg_gen_addi_i64(t, temp_tcgv_i64(addr), 8);
            addr_p8 = tcgv_i64_temp(t);
        }

        addr_new = tci_extend_addr(addr_p8);
        if (b) {
            tcg_gen_bswap64_i64(b, y);
            gen_st_i64(b, addr_new, make_memop_idx(mop[1], idx));
            tcg_temp_free_i64(b);
        } else {
            gen_st_i64(y, addr_new, make_memop_idx(mop[1], idx));
        }
        maybe_free_addr(addr_p8, addr_new);
        tcg_temp_free_internal(addr_p8);
    } else {
        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            ext_addr = tcg_temp_ebb_new_i64();
            tcg_gen_extu_i32_i64(ext_addr, temp_tcgv_i32(addr));
            addr = tcgv_i64_temp(ext_addr);
        }
        gen_helper_st_i128(tcg_env, temp_tcgv_i64(addr), val,
                           tcg_constant_i32(orig_oi));
    }

    plugin_gen_mem_callbacks_i128(val, ext_addr, addr, orig_oi,
                                  QEMU_PLUGIN_MEM_W);
}

void tcg_gen_qemu_st_i128_chk(TCGv_i128 val, TCGTemp *addr, TCGArg idx,
                              MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) == MO_128);
    tcg_debug_assert((memop & MO_SIGN) == 0);
    tcg_gen_qemu_st_i128_int(val, addr, idx, memop);
}

void tcg_gen_ext_i32(TCGv_i32 ret, TCGv_i32 val, MemOp opc)
{
    switch (opc & MO_SSIZE) {
    case MO_SB:
        tcg_gen_ext8s_i32(ret, val);
        break;
    case MO_UB:
        tcg_gen_ext8u_i32(ret, val);
        break;
    case MO_SW:
        tcg_gen_ext16s_i32(ret, val);
        break;
    case MO_UW:
        tcg_gen_ext16u_i32(ret, val);
        break;
    case MO_UL:
    case MO_SL:
        tcg_gen_mov_i32(ret, val);
        break;
    default:
        g_assert_not_reached();
    }
}

void tcg_gen_ext_i64(TCGv_i64 ret, TCGv_i64 val, MemOp opc)
{
    switch (opc & MO_SSIZE) {
    case MO_SB:
        tcg_gen_ext8s_i64(ret, val);
        break;
    case MO_UB:
        tcg_gen_ext8u_i64(ret, val);
        break;
    case MO_SW:
        tcg_gen_ext16s_i64(ret, val);
        break;
    case MO_UW:
        tcg_gen_ext16u_i64(ret, val);
        break;
    case MO_SL:
        tcg_gen_ext32s_i64(ret, val);
        break;
    case MO_UL:
        tcg_gen_ext32u_i64(ret, val);
        break;
    case MO_UQ:
    case MO_SQ:
        tcg_gen_mov_i64(ret, val);
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * The core runs one vCPU and TCG has no parallel mode, so the guest's
 * atomic operations are expanded inline as a load, the operation and a store,
 * as QEMU does in a serial context; there are no out-of-line atomic helpers.
 */

static void tcg_gen_nonatomic_cmpxchg_i32_int(TCGv_i32 retv, TCGTemp *addr,
                                              TCGv_i32 cmpv, TCGv_i32 newv,
                                              TCGArg idx, MemOp memop)
{
    TCGv_i32 t1 = tcg_temp_ebb_new_i32();
    TCGv_i32 t2 = tcg_temp_ebb_new_i32();

    tcg_gen_ext_i32(t2, cmpv, memop & MO_SIZE);

    tcg_gen_qemu_ld_i32_int(t1, addr, idx, memop & ~MO_SIGN);
    tcg_gen_movcond_i32(TCG_COND_EQ, t2, t1, t2, newv, t1);
    tcg_gen_qemu_st_i32_int(t2, addr, idx, memop);
    tcg_temp_free_i32(t2);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i32(retv, t1, memop);
    } else {
        tcg_gen_mov_i32(retv, t1);
    }
    tcg_temp_free_i32(t1);
}

void tcg_gen_atomic_cmpxchg_i32_chk(TCGv_i32 retv, TCGTemp *addr,
                                    TCGv_i32 cmpv, TCGv_i32 newv,
                                    TCGArg idx, MemOp memop,
                                    TCGType addr_type)
{
    LR_QUIET_STORE();
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) <= MO_32);
    tcg_gen_nonatomic_cmpxchg_i32_int(retv, addr, cmpv, newv, idx, memop);
}

static void tcg_gen_nonatomic_cmpxchg_i64_int(TCGv_i64 retv, TCGTemp *addr,
                                              TCGv_i64 cmpv, TCGv_i64 newv,
                                              TCGArg idx, MemOp memop)
{
    TCGv_i64 t1, t2;

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_nonatomic_cmpxchg_i32_int(TCGV_LOW(retv), addr, TCGV_LOW(cmpv),
                                          TCGV_LOW(newv), idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_i32(TCGV_HIGH(retv), TCGV_LOW(retv), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(retv), 0);
        }
        return;
    }

    t1 = tcg_temp_ebb_new_i64();
    t2 = tcg_temp_ebb_new_i64();

    tcg_gen_ext_i64(t2, cmpv, memop & MO_SIZE);

    tcg_gen_qemu_ld_i64_int(t1, addr, idx, memop & ~MO_SIGN);
    tcg_gen_movcond_i64(TCG_COND_EQ, t2, t1, t2, newv, t1);
    tcg_gen_qemu_st_i64_int(t2, addr, idx, memop);
    tcg_temp_free_i64(t2);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i64(retv, t1, memop);
    } else {
        tcg_gen_mov_i64(retv, t1);
    }
    tcg_temp_free_i64(t1);
}

void tcg_gen_nonatomic_cmpxchg_i64_chk(TCGv_i64 retv, TCGTemp *addr,
                                       TCGv_i64 cmpv, TCGv_i64 newv,
                                       TCGArg idx, MemOp memop,
                                       TCGType addr_type)
{
    LR_QUIET_STORE();
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) <= MO_64);
    tcg_gen_nonatomic_cmpxchg_i64_int(retv, addr, cmpv, newv, idx, memop);
}

void tcg_gen_atomic_cmpxchg_i64_chk(TCGv_i64 retv, TCGTemp *addr,
                                    TCGv_i64 cmpv, TCGv_i64 newv,
                                    TCGArg idx, MemOp memop, TCGType addr_type)
{
    LR_QUIET_STORE();
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) <= MO_64);
    tcg_gen_nonatomic_cmpxchg_i64_int(retv, addr, cmpv, newv, idx, memop);
}

static void do_nonatomic_op_i32(TCGv_i32 ret, TCGTemp *addr, TCGv_i32 val,
                                TCGArg idx, MemOp memop, bool new_val,
                                void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    LR_QUIET_STORE();
    TCGv_i32 t1 = tcg_temp_ebb_new_i32();
    TCGv_i32 t2 = tcg_temp_ebb_new_i32();

    memop = tcg_canonicalize_memop(memop, 0, 0);

    tcg_gen_qemu_ld_i32_int(t1, addr, idx, memop);
    tcg_gen_ext_i32(t2, val, memop);
    gen(t2, t1, t2);
    tcg_gen_qemu_st_i32_int(t2, addr, idx, memop);

    tcg_gen_ext_i32(ret, (new_val ? t2 : t1), memop);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static void do_nonatomic_op_i64(TCGv_i64 ret, TCGTemp *addr, TCGv_i64 val,
                                TCGArg idx, MemOp memop, bool new_val,
                                void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    LR_QUIET_STORE();
    TCGv_i64 t1 = tcg_temp_ebb_new_i64();
    TCGv_i64 t2 = tcg_temp_ebb_new_i64();

    memop = tcg_canonicalize_memop(memop, 1, 0);

    tcg_gen_qemu_ld_i64_int(t1, addr, idx, memop);
    tcg_gen_ext_i64(t2, val, memop);
    gen(t2, t1, t2);
    tcg_gen_qemu_st_i64_int(t2, addr, idx, memop);

    tcg_gen_ext_i64(ret, (new_val ? t2 : t1), memop);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

static void do_nonatomic_op_i128(TCGv_i128 ret, TCGTemp *addr, TCGv_i128 val,
                                 TCGArg idx, MemOp memop, bool new_val,
                                 void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i128 t = tcg_temp_ebb_new_i128();
    TCGv_i128 r = tcg_temp_ebb_new_i128();

    tcg_gen_qemu_ld_i128_int(r, addr, idx, memop);
    gen(TCGV128_LOW(t), TCGV128_LOW(r), TCGV128_LOW(val));
    gen(TCGV128_HIGH(t), TCGV128_HIGH(r), TCGV128_HIGH(val));
    tcg_gen_qemu_st_i128_int(t, addr, idx, memop);

    tcg_gen_mov_i128(ret, r);
    tcg_temp_free_i128(t);
    tcg_temp_free_i128(r);
}

#define GEN_ATOMIC_HELPER(NAME, OP, NEW)                                \
void tcg_gen_atomic_##NAME##_i32_chk(TCGv_i32 ret, TCGTemp *addr,       \
                                     TCGv_i32 val, TCGArg idx,          \
                                     MemOp memop, TCGType addr_type)    \
{                                                                       \
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);                  \
    tcg_debug_assert((memop & MO_SIZE) <= MO_32);                       \
    do_nonatomic_op_i32(ret, addr, val, idx, memop, NEW,                \
                        tcg_gen_##OP##_i32);                            \
}                                                                       \
void tcg_gen_atomic_##NAME##_i64_chk(TCGv_i64 ret, TCGTemp *addr,       \
                                     TCGv_i64 val, TCGArg idx,          \
                                     MemOp memop, TCGType addr_type)    \
{                                                                       \
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);                  \
    tcg_debug_assert((memop & MO_SIZE) <= MO_64);                       \
    do_nonatomic_op_i64(ret, addr, val, idx, memop, NEW,                \
                        tcg_gen_##OP##_i64);                            \
}

#define GEN_ATOMIC_HELPER128(NAME, OP, NEW)                             \
GEN_ATOMIC_HELPER(NAME, OP, NEW)                                        \
void tcg_gen_atomic_##NAME##_i128_chk(TCGv_i128 ret, TCGTemp *addr,     \
                                      TCGv_i128 val, TCGArg idx,        \
                                      MemOp memop, TCGType addr_type)   \
{                                                                       \
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);                  \
    tcg_debug_assert((memop & MO_SIZE) == MO_128);                      \
    do_nonatomic_op_i128(ret, addr, val, idx, memop, NEW,               \
                         tcg_gen_##OP##_i64);                           \
}

GEN_ATOMIC_HELPER(fetch_add, add, 0)
GEN_ATOMIC_HELPER128(fetch_and, and, 0)
GEN_ATOMIC_HELPER128(fetch_or, or, 0)
GEN_ATOMIC_HELPER(fetch_xor, xor, 0)
GEN_ATOMIC_HELPER(fetch_smin, smin, 0)
GEN_ATOMIC_HELPER(fetch_umin, umin, 0)
GEN_ATOMIC_HELPER(fetch_smax, smax, 0)
GEN_ATOMIC_HELPER(fetch_umax, umax, 0)

GEN_ATOMIC_HELPER(add_fetch, add, 1)
GEN_ATOMIC_HELPER(and_fetch, and, 1)
GEN_ATOMIC_HELPER(or_fetch, or, 1)
GEN_ATOMIC_HELPER(xor_fetch, xor, 1)
GEN_ATOMIC_HELPER(smin_fetch, smin, 1)
GEN_ATOMIC_HELPER(umin_fetch, umin, 1)
GEN_ATOMIC_HELPER(smax_fetch, smax, 1)
GEN_ATOMIC_HELPER(umax_fetch, umax, 1)

static void tcg_gen_mov2_i32(TCGv_i32 r, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_mov_i32(r, b);
}

static void tcg_gen_mov2_i64(TCGv_i64 r, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_mov_i64(r, b);
}

GEN_ATOMIC_HELPER128(xchg, mov2, 0)

#undef GEN_ATOMIC_HELPER
#undef GEN_ATOMIC_HELPER128
