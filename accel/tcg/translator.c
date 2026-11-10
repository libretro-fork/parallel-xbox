/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 * Copyright (c) 2026 Réda Chérif-Touil
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "accel/tcg/tb-code-hash.h"
#include "exec/target_page.h"
#include "exec/translator.h"
#include "exec/plugin-gen.h"
#include "tcg/tcg-op-common.h"
#include "internal-common.h"
#include "tb-internal.h"
#include "tb-jmp-cache.h"
#include "tb-hash.h"
#include "tcg/tcg-temp-internal.h"
#include "hw/core/cpu.h"
#include "system/cpus.h"
#include "qemu/cpu-boost.h"

#ifdef CONFIG_SOFTMMU
static void gen_tb_flags_check(TCGv_ptr tb, TCGv_i32 t32, uint32_t flags,
                               TCGLabel *slow)
{
    tcg_gen_ld_i32(t32, tb, offsetof(TranslationBlock, flags));
    if ((flags & XEMU_TB_CF_ANY) == XEMU_TB_CF_ANY) {
        /* An independent-only request must never accept a specific class. */
        tcg_gen_brcondi_i32(TCG_COND_NE, t32, flags, slow);
    } else {
        tcg_gen_xori_i32(t32, t32, flags);
        tcg_gen_movcond_i32(TCG_COND_EQ, t32, t32,
                            tcg_constant_i32(XEMU_TB_CF_ANY & ~flags),
                            tcg_constant_i32(0), t32);
        tcg_gen_brcondi_i32(TCG_COND_NE, t32, 0, slow);
    }
}
#endif

static void gen_lookup_and_goto_ptr(TCGv_i64 pc, uint64_t cs_base,
                                    uint32_t flags, int direct_slot)
{
#ifdef CONFIG_SOFTMMU
    uint32_t cflags = tb_cflags(tcg_ctx->gen_tb);
    bool any_only = (flags & XEMU_TB_CF_ANY) == XEMU_TB_CF_ANY;
    TCGLabel *slow;
    TCGv_i64 t, hash;
    TCGv_i32 t32;
    TCGv_ptr ent, tb, code, hoff;

    /* The libretro core runs with icount (its guest's time is its
     * instructions): the inline lookup is safe with it, as the cflags
     * compare below sends a block with a cut-down instruction count
     * (CF_COUNT_MASK) to the slow path, as tb_lookup() does, and the
     * blocks' own countdown (gen_tb_start) ends a chain whose budget is
     * spent. QEMU leaves icount out for its helper path's extra budget
     * check before each block, which that countdown makes redundant. */
    if (cflags & (CF_NO_GOTO_PTR | CF_COUNT_MASK | CF_SINGLE_STEP |
                  CF_MEMI_ONLY | CF_BP_PAGE | CF_NOIRQ)) {
        if (direct_slot >= 0) {
            tcg_gen_exit_tb(tcg_ctx->gen_tb, direct_slot);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
        return;
    }

    QEMU_BUILD_BUG_ON(sizeof(((CPUJumpCache *)0)->array[0]) != 16);
    QEMU_BUILD_BUG_ON(offsetof(CPUState, breakpoints.tqh_first) !=
                      offsetof(CPUState, breakpoints));

    slow = gen_new_label();
    t = tcg_temp_ebb_new_i64();
    hash = tcg_temp_ebb_new_i64();
    t32 = tcg_temp_ebb_new_i32();
    ent = tcg_temp_ebb_new_ptr();
    tb = tcg_temp_ebb_new_ptr();
    code = tcg_temp_ebb_new_ptr();
    hoff = tcg_temp_ebb_new_ptr();

    /* tb_jmp_cache_hash_func() */
    tcg_gen_shri_i64(t, pc, TARGET_PAGE_BITS - TB_JMP_PAGE_BITS);
    tcg_gen_xor_i64(t, t, pc);
    tcg_gen_shri_i64(hash, t, TARGET_PAGE_BITS - TB_JMP_PAGE_BITS);
    tcg_gen_andi_i64(hash, hash, TB_JMP_PAGE_MASK);
    tcg_gen_andi_i64(t, t, TB_JMP_ADDR_MASK);
    tcg_gen_or_i64(hash, hash, t);
    tcg_gen_shli_i64(hash, hash, 4);

    tcg_gen_ld_ptr(ent, tcg_env,
                   offsetof(CPUState, tb_jmp_cache) - sizeof(CPUState));
    tcg_gen_trunc_i64_ptr(hoff, hash);
    tcg_gen_add_ptr(ent, ent, hoff);

    /* The same identity tests as tb_lookup(), with an optional page guard. */
    tcg_gen_ld_ptr(tb, ent, offsetof(CPUJumpCache, array[0].tb));
    tcg_gen_brcondi_ptr(TCG_COND_EQ, tb, 0, slow);
    if (direct_slot >= 0) {
        /* Single-page targets still return once to install a direct link.
         * Two-page targets use only the TLB-invalidated jump cache. */
        if (sizeof(tb_page_addr_t) == 8) {
            tcg_gen_ld_i64(t, tb, offsetof(TranslationBlock, page_addr[1]));
        } else {
            tcg_gen_ld32u_i64(t, tb, offsetof(TranslationBlock, page_addr[1]));
        }
        tcg_gen_brcondi_i64(TCG_COND_EQ, t, (tb_page_addr_t)-1, slow);
    }
    if (any_only) {
        /* Reject a class-dependent entry before the PC/segment loads. */
        gen_tb_flags_check(tb, t32, flags, slow);
    }
    tcg_gen_ld_i64(t, ent, offsetof(CPUJumpCache, array[0].pc));
    tcg_gen_brcond_i64(TCG_COND_NE, t, pc, slow);
    tcg_gen_ld_i64(t, tb, offsetof(TranslationBlock, cs_base));
    tcg_gen_brcondi_i64(TCG_COND_NE, t, cs_base, slow);
    if (!any_only) {
        gen_tb_flags_check(tb, t32, flags, slow);
    }
    tcg_gen_ld_i32(t32, tb, offsetof(TranslationBlock, cflags));
    tcg_gen_brcondi_i32(TCG_COND_NE, t32, cflags, slow);
    tcg_gen_ld_i64(t, tcg_env,
                   offsetof(CPUState, breakpoints) - sizeof(CPUState));
    tcg_gen_brcondi_i64(TCG_COND_NE, t, 0, slow);

    tcg_gen_ld_ptr(code, tb, offsetof(TranslationBlock, tc.ptr));
    tcg_gen_goto_ptr(code);

    gen_set_label(slow);
    if (direct_slot >= 0) {
        tcg_gen_exit_tb(tcg_ctx->gen_tb, direct_slot);
    } else {
        tcg_gen_lookup_and_goto_ptr();
    }
#else
    tcg_gen_lookup_and_goto_ptr();
#endif
}

void tcg_gen_lookup_and_goto_ptr_fast(TCGv_i64 pc, uint64_t cs_base,
                                      uint32_t flags)
{
    gen_lookup_and_goto_ptr(pc, cs_base, flags, -1);
}

void tcg_gen_lookup_and_goto_ptr_2page(TCGv_i64 pc, uint64_t cs_base,
                                       uint32_t flags, unsigned direct_slot)
{
    tcg_debug_assert(direct_slot < 2);
    gen_lookup_and_goto_ptr(pc, cs_base, flags, direct_slot);
}

static void set_can_do_io(DisasContextBase *db, bool val)
{
    QEMU_BUILD_BUG_ON(sizeof_field(CPUState, neg.can_do_io) != 1);
    tcg_gen_st8_i32(tcg_constant_i32(val), tcg_env,
                    offsetof(CPUState, neg.can_do_io) - sizeof(CPUState));
}

bool translator_io_start(DisasContextBase *db)
{
    /*
     * Ensure that this instruction will be the last in the TB.
     * The target may override this to something more forceful.
     */
    if (db->is_jmp == DISAS_NEXT) {
        db->is_jmp = DISAS_TOO_MANY;
    }
    return true;
}

/* This is only a feasibility filter, never an idle proof. The live kernel
 * and RAM-poll proofs in misc_helper.c still validate complete code and
 * memory state before time can advance. These bytes belong to the first
 * translated instruction, so ordinary TB invalidation covers their changes. */
static bool lr_quiet_candidate_prefix(const DisasContextBase *db)
{
    const uint8_t *code = db->host_addr[0];
    if (!code) {
        return true; /* MMIO/non-RAM code: keep the conservative path. */
    }
    switch (code[0]) {
    case 0xFB: /* kernel STI */
    case 0x90: /* either NOP after STI */
        return true;
    case 0x3B: /* CMP EBP,[EBP+0] after CLI */
        if ((db->pc_first & ~TARGET_PAGE_MASK) > TARGET_PAGE_SIZE - 3) {
            return true;
        }
        return code[1] == 0x6D && code[2] == 0;
    case 0x8B: /* RAM timer wait starts with MOV ECX,[EDX]. */
        if ((db->pc_first & ~TARGET_PAGE_MASK) == TARGET_PAGE_SIZE - 1) {
            return true; /* Do not inspect an unprobed second code page. */
        }
        return code[1] == 0x0A;
    default:
        return false;
    }
}

static TCGOp *gen_tb_start(TCGContext *s, DisasContextBase *db, uint32_t cflags)
{
    TCGv_i32 count = NULL;
    TCGOp *icount_start_insn = NULL;
    /* Only the frame engine's guest instruction budget limits a block. */
    bool budget = (cflags & CF_USE_ICOUNT) != 0;

    TCGv_i32 test = NULL;

    if (budget) {
        /*
         * Keep the budget separate from the signed request half. The
         * x86-64 emitter retains the low half in its reserved register
         * across chains, synchronizing it at C calls and returns. Other
         * hosts use matching 16-bit stores/loads, avoiding a full-width
         * load that cannot forward from the preceding narrow store.
         * Negative in either half is the original u32 < 0 condition.
         */
        count = tcg_temp_new_i32();
        tcg_gen_ld16u_i32(count, tcg_env,
                          offsetof(CPUState, neg.icount_decr.u16.low) -
                          sizeof(CPUState));
        /*
         * We emit a sub with a dummy immediate argument. Keep the insn index
         * of the sub so that we later (when we know the actual insn count)
         * can update the argument with the actual insn count.
         */
        tcg_gen_sub_i32(count, count, tcg_constant_i32(0));
        icount_start_insn = QTAILQ_LAST(&s->ops);
        if (!(cflags & CF_NOIRQ)) {
            test = tcg_temp_new_i32();
            tcg_gen_ld16s_i32(test, tcg_env,
                              offsetof(CPUState, neg.icount_decr.u16.high) -
                              sizeof(CPUState));
            tcg_gen_or_i32(test, test, count);
        }
    } else if (!(cflags & CF_NOIRQ)) {
        test = tcg_temp_new_i32();
        tcg_gen_ld_i32(test, tcg_env,
                       offsetof(CPUState, neg.icount_decr.u32) -
                       sizeof(CPUState));
    }

    /*
     * Emit the check against icount_decr to see if we should exit
     * unless we suppress the check with CF_NOIRQ. If we are using
     * icount and have suppressed interruption the higher level code
     * should have ensured we don't run more instructions than the
     * budget.
     */
    if (cflags & CF_NOIRQ) {
        s->exitreq_label = NULL;
    } else {
        s->exitreq_label = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_LT, test, 0, s->exitreq_label);
    }

    if (budget) {
        tcg_gen_st16_i32(count, tcg_env,
                         offsetof(CPUState, neg.icount_decr.u16.low) -
                         sizeof(CPUState));
    }

    /* The libretro core's idle detection: the block's instructions come
     * off neg.lr_quiet, and a block that would take it below zero leaves
     * for cpu-exec.c to check whether the candidate is a proven wait.
     * Register-only computation must retain its instruction budget. A store
     * (lr_quiet_store_emitted) resets the count; so do device accesses
     * (but polls of a register that has not changed, cputlb.c), the TSC
     * and interrupts, in C. Not in a block that may not be
     * interrupted. */
    s->lr_quiet_label = NULL;
    s->lr_quiet_insn = NULL;
    s->lr_quiet_first = s->lr_quiet_last = NULL;
    s->lr_quiet_store = false;
    s->lr_quiet_enabled = (cflags & CF_USE_ICOUNT) && !(cflags & CF_NOIRQ);
    if (s->lr_quiet_enabled && lr_quiet_candidate_prefix(db)) {
        TCGv_i32 quiet = tcg_temp_new_i32();

        tcg_gen_ld_i32(quiet, tcg_env,
                       offsetof(CPUState, neg.lr_quiet) - sizeof(CPUState));
        s->lr_quiet_first = QTAILQ_LAST(&s->ops);
        tcg_gen_sub_i32(quiet, quiet, tcg_constant_i32(0));
        s->lr_quiet_insn = QTAILQ_LAST(&s->ops);
        tcg_gen_st_i32(quiet, tcg_env,
                       offsetof(CPUState, neg.lr_quiet) - sizeof(CPUState));
        s->lr_quiet_label = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_LT, quiet, 0, s->lr_quiet_label);
        s->lr_quiet_last = QTAILQ_LAST(&s->ops);
    }

    return icount_start_insn;
}

/* A guest store is being emitted (tcg/tcg-op-ldst.c): the block's first
 * one resets the quiet count, so the wait starts over */
void lr_quiet_store_emitted(void)
{
    TCGContext *s = tcg_ctx;

    if (s->lr_quiet_enabled && !s->lr_quiet_store) {
        s->lr_quiet_store = true;
        if (s->lr_quiet_label) {
            /* A straight-line, side-effect-free prefix cannot bypass this
             * reset. Its quiet decrement/check would immediately be overwritten.
             * Keep the preamble if any branch, helper or guest access precedes
             * the reset: that path may leave without ever reaching this store. */
            TCGOp *op = QTAILQ_NEXT(s->lr_quiet_last, link);
            bool dominates = true;
            for (; op; op = QTAILQ_NEXT(op, link)) {
                if (tcg_op_defs[op->opc].flags &
                    (TCG_OPF_BB_END | TCG_OPF_COND_BRANCH | TCG_OPF_SIDE_EFFECTS |
                     TCG_OPF_CALL_CLOBBER)) {
                    dominates = false;
                    break;
                }
            }
            if (dominates) {
                TCGOp *stop = QTAILQ_NEXT(s->lr_quiet_last, link);
                op = s->lr_quiet_first;
                while (op != stop) {
                    TCGOp *next = QTAILQ_NEXT(op, link);
                    tcg_op_remove(s, op);
                    op = next;
                }
                s->lr_quiet_label = NULL;
            }
        }
        /* Stores retain their reset even when no quiet check was emitted. */
        tcg_gen_st_i32(tcg_constant_i32(LR_QUIET_INSNS), tcg_env,
                       offsetof(CPUState, neg.lr_quiet) - sizeof(CPUState));
    }
}

static void gen_tb_end(TCGContext *s, const TranslationBlock *tb, uint32_t cflags,
                       TCGOp *icount_start_insn, int num_insns)
{
    TCGTemp *insn_count = NULL;

    if (icount_start_insn) {
        /*
         * Update the num_insn immediate parameter now that we know
         * the actual insn count.
         */
        insn_count = tcg_constant_internal(TCG_TYPE_I32, num_insns);
        tcg_set_insn_param(icount_start_insn, 2, temp_arg(insn_count));
    }

    if (s->exitreq_label) {
        gen_set_label(s->exitreq_label);
        tcg_gen_exit_tb(tb, TB_EXIT_REQUESTED);
    }
    if (s->lr_quiet_label) {
        if (!insn_count) {
            insn_count = tcg_constant_internal(TCG_TYPE_I32, num_insns);
        }
        tcg_set_insn_param(s->lr_quiet_insn, 2, temp_arg(insn_count));
        gen_set_label(s->lr_quiet_label);
        tcg_gen_exit_tb(tb, TB_EXIT_REQUESTED);
        s->lr_quiet_label = NULL;
    }
}

bool translator_is_same_page(const DisasContextBase *db, vaddr addr)
{
    return ((addr ^ db->pc_first) & TARGET_PAGE_MASK) == 0;
}

bool translator_use_goto_tb(DisasContextBase *db, vaddr dest)
{
    /* Suppress goto_tb if requested. */
    if (tb_cflags(db->tb) & CF_NO_GOTO_TB) {
        return false;
    }

    /* Check for the dest on the same page as the start of the TB.  */
    return translator_is_same_page(db, dest);
}

void translator_loop(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                     vaddr pc, void *host_pc, const TranslatorOps *ops,
                     DisasContextBase *db)
{
    TCGContext *s = tcg_ctx;
    uint32_t cflags = tb_cflags(tb);
    TCGOp *icount_start_insn;
    TCGOp *first_insn_start = NULL;
    bool plugin_enabled;

    /* Initialize DisasContext */
    db->tb = tb;
    db->pc_first = pc;
    db->pc_next = pc;
    db->is_jmp = DISAS_NEXT;
    db->num_insns = 0;
    db->max_insns = *max_insns;
    db->insn_start = NULL;
    db->fake_insn = false;
    db->host_addr[0] = host_pc;
    db->host_addr[1] = NULL;
    db->record_start = 0;
    db->record_len = 0;
    db->code_mmuidx = cpu_mmu_index(cpu, true);

    ops->init_disas_context(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    /* Start translating.  */
    icount_start_insn = gen_tb_start(s, db, cflags);
    ops->tb_start(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    plugin_enabled = plugin_gen_tb_start(cpu, db);
    db->plugin_enabled = plugin_enabled;

    while (true) {
        *max_insns = ++db->num_insns;
        ops->insn_start(db, cpu);
        db->insn_start = QTAILQ_LAST(&s->ops);
        if (first_insn_start == NULL) {
            first_insn_start = db->insn_start;
        }
        tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

        if (plugin_enabled) {
            plugin_gen_insn_start(cpu, db);
        }

        /*
         * Disassemble one instruction.  The translate_insn hook should
         * update db->pc_next and db->is_jmp to indicate what should be
         * done next -- either exiting this loop or locate the start of
         * the next instruction.
         */
        vaddr insn_pc = db->pc_next;
        ops->translate_insn(db, cpu);
        /* An instruction known to reach a device ends the block, where it
         * may (cpu_io_recompile) */
        if ((cflags & CF_USE_ICOUNT) && db->is_jmp == DISAS_NEXT &&
            tb_io_insn_known(insn_pc)) {
            db->is_jmp = DISAS_TOO_MANY;
        }

        /*
         * We can't instrument after instructions that change control
         * flow although this only really affects post-load operations.
         *
         * Calling plugin_gen_insn_end() before we possibly stop translation
         * is important. Even if this ends up as dead code, plugin generation
         * needs to see a matching plugin_gen_insn_{start,end}() pair in order
         * to accurately track instrumented helpers that might access memory.
         */
        if (plugin_enabled) {
            plugin_gen_insn_end();
        }

        /* Stop translation if translate_insn so indicated.  */
        if (db->is_jmp != DISAS_NEXT) {
            break;
        }

        /* Stop translation if the output buffer is full,
           or we have executed all of the allowed instructions.  */
        if (tcg_op_buf_full(s) || db->num_insns >= db->max_insns ||
            tcg_temps_nearly_full(s)) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }
    }

    /* Emit code to exit the TB, as indicated by db->is_jmp.  */
    ops->tb_stop(db, cpu);
    gen_tb_end(s, tb, cflags, icount_start_insn, db->num_insns);

    /*
     * Manage can_do_io for the translation block: set to false before
     * the first insn and set to true before the last insn.
     */
    if (db->num_insns == 1) {
        tcg_debug_assert(first_insn_start == db->insn_start);
    } else {
        tcg_debug_assert(first_insn_start != db->insn_start);
        s->emit_before_op = first_insn_start;
        set_can_do_io(db, false);
    }
    s->emit_before_op = db->insn_start;
    set_can_do_io(db, true);
    s->emit_before_op = NULL;

    /* Record the translated block extent before notifying plugins. */
    tb->size = db->pc_next - db->pc_first;
    tb->icount = db->num_insns;
    tb->ihash = tb_code_hash_func_host(cpu_env(cpu), db->pc_first, tb->size,
                                      tb_page_addr0(tb) != -1 ?
                                      db->host_addr[0] : NULL);

    if (plugin_enabled) {
        plugin_gen_tb_end(cpu, db->num_insns);
    }


}

static bool translator_ld(CPUArchState *env, DisasContextBase *db,
                          void *dest, vaddr pc, size_t len)
{
    TranslationBlock *tb = db->tb;
    vaddr last = pc + len - 1;
    void *host;
    vaddr base;

    /* Use slow path if first page is MMIO. */
    if (unlikely(tb_page_addr0(tb) == -1)) {
        /* We capped translation with first page MMIO in tb_gen_code. */
        tcg_debug_assert(db->max_insns == 1);
        return false;
    }

    host = db->host_addr[0];
    base = db->pc_first;

    if (likely(((base ^ last) & TARGET_PAGE_MASK) == 0)) {
        /* Entire read is from the first page. */
        goto do_read;
    }

    if (unlikely(((base ^ pc) & TARGET_PAGE_MASK) == 0)) {
        /*
         * Read begins on the first page and extends to the second.
         * The unaligned read is never atomic.
         */
        size_t len0 = -(pc | TARGET_PAGE_MASK);
        memcpy(dest, host + (pc - base), len0);
        pc += len0;
        dest += len0;
        len -= len0;
    }

    /*
     * The read must conclude on the second page and not extend to a third.
     *
     * TODO: We could allow the two pages to be virtually discontiguous,
     * since we already allow the two pages to be physically discontiguous.
     * The only reasonable use case would be executing an insn at the end
     * of the address space wrapping around to the beginning.  For that,
     * we would need to know the current width of the address space.
     * In the meantime, assert.
     */
    base = (base & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    assert(((base ^ pc) & TARGET_PAGE_MASK) == 0);
    assert(((base ^ last) & TARGET_PAGE_MASK) == 0);
    host = db->host_addr[1];

    if (host == NULL) {
        tb_page_addr_t page0, old_page1, new_page1;

        new_page1 = get_page_addr_code_hostp(env, base, &db->host_addr[1]);

        /*
         * If the second page is MMIO, treat as if the first page
         * was MMIO as well, so that we do not cache the TB.
         */
        if (unlikely(new_page1 == -1)) {
            tb_unlock_pages(tb);
            tb_set_page_addr0(tb, -1);
            /* Require that this be the final insn. */
            db->max_insns = db->num_insns;
            return false;
        }

        /*
         * If this is not the first time around, and page1 matches,
         * then we already have the page locked.  Alternately, we're
         * not doing anything to prevent the PTE from changing, so
         * we might wind up with a different page, requiring us to
         * re-do the locking.
         */
        old_page1 = tb_page_addr1(tb);
        if (likely(new_page1 != old_page1)) {
            page0 = tb_page_addr0(tb);
            if (unlikely(old_page1 != -1)) {
                tb_unlock_page1(page0, old_page1);
            }
            tb_set_page_addr1(tb, new_page1);
            tb_lock_page1(page0, new_page1);
        }
        host = db->host_addr[1];
    }

 do_read:
    /*
     * Assume aligned reads should be atomic, if possible.
     * We're not in a position to jump out with EXCP_ATOMIC.
     */
    host += pc - base;
    switch (len) {
    case 2:
        if (QEMU_IS_ALIGNED(pc, 2)) {
            uint16_t t = retro_atomic_load_relaxed_u16((retro_atomic_u16_t *)host);
            stw_he_p(dest, t);
            return true;
        }
        break;
    case 4:
        if (QEMU_IS_ALIGNED(pc, 4)) {
            uint32_t t = retro_atomic_load_relaxed_int((retro_atomic_int_t *)host);
            stl_he_p(dest, t);
            return true;
        }
        break;
#ifdef CONFIG_ATOMIC64
    case 8:
        if (QEMU_IS_ALIGNED(pc, 8)) {
            uint64_t t = retro_atomic_load_relaxed_64((retro_atomic_64_t *)host);
            stq_he_p(dest, t);
            return true;
        }
        break;
#endif
    }
    /* Unaligned or partial read from the second page is not atomic. */
    memcpy(dest, host, len);
    return true;
}

static void record_save(DisasContextBase *db, vaddr pc,
                        const void *from, int size)
{
    int offset;

    /* Do not record probes before the start of TB. */
    if (pc < db->pc_first) {
        return;
    }

    /*
     * In translator_access, we verified that pc is within 2 pages
     * of pc_first, thus this will never overflow.
     */
    offset = pc - db->pc_first;

    /*
     * Either the first or second page may be I/O.  If it is the second,
     * then the first byte we need to record will be at a non-zero offset.
     * In either case, we should not need to record but a single insn.
     */
    if (db->record_len == 0) {
        db->record_start = offset;
        db->record_len = size;
    } else {
        assert(offset == db->record_start + db->record_len);
        assert(db->record_len + size <= sizeof(db->record));
        db->record_len += size;
    }

    memcpy(db->record + (offset - db->record_start), from, size);
}

size_t translator_st_len(const DisasContextBase *db)
{
    return db->fake_insn ? db->record_len : db->tb->size;
}

bool translator_st(const DisasContextBase *db, void *dest,
                   vaddr addr, size_t len)
{
    size_t offset, offset_end;

    if (addr < db->pc_first) {
        return false;
    }
    offset = addr - db->pc_first;
    offset_end = offset + len;
    if (offset_end > translator_st_len(db)) {
        return false;
    }

    if (!db->fake_insn) {
        size_t offset_page1 = -(db->pc_first | TARGET_PAGE_MASK);

        /* Get all the bytes from the first page. */
        if (db->host_addr[0]) {
            if (offset_end <= offset_page1) {
                memcpy(dest, db->host_addr[0] + offset, len);
                return true;
            }
            if (offset < offset_page1) {
                size_t len0 = offset_page1 - offset;
                memcpy(dest, db->host_addr[0] + offset, len0);
                offset += len0;
                dest += len0;
            }
        }

        /* Get any bytes from the second page. */
        if (db->host_addr[1] && offset >= offset_page1) {
            memcpy(dest, db->host_addr[1] + (offset - offset_page1),
                   offset_end - offset);
            return true;
        }
    }

    /* Else get recorded bytes. */
    if (db->record_len != 0 &&
        offset >= db->record_start &&
        offset_end <= db->record_start + db->record_len) {
        memcpy(dest, db->record + (offset - db->record_start),
               offset_end - offset);
        return true;
    }
    return false;
}

uint8_t translator_ldub(CPUArchState *env, DisasContextBase *db, vaddr pc)
{
    uint8_t val;

    /* A byte cannot straddle pages. The first-page mapping and lock were
     * established before translation; avoid the general loader's call and
     * output temporary for ordinary opcode/prefix reads from that page. */
    if (likely(tb_page_addr0(db->tb) != -1 &&
               ((db->pc_first ^ pc) & TARGET_PAGE_MASK) == 0)) {
        return *(const uint8_t *)(db->host_addr[0] + (pc - db->pc_first));
    }

    if (!translator_ld(env, db, &val, pc, sizeof(val))) {
        MemOpIdx oi = make_memop_idx(MO_UB, db->code_mmuidx);
        val = cpu_ldb_code_mmu(env, pc, oi, 0);
        record_save(db, pc, &val, sizeof(val));
    }
    return val;
}

uint16_t translator_lduw_end(CPUArchState *env, DisasContextBase *db,
                             vaddr pc, MemOp endian)
{
    uint16_t val;

    if (likely(tb_page_addr0(db->tb) != -1 &&
               ((db->pc_first ^ (pc + sizeof(val) - 1)) & TARGET_PAGE_MASK) == 0)) {
        const void *host = db->host_addr[0] + (pc - db->pc_first);

        /* Preserve the general loader's aligned atomic read. */
        if (QEMU_IS_ALIGNED(pc, sizeof(val))) {
            val = retro_atomic_load_relaxed_u16((retro_atomic_u16_t *)host);
        } else {
            val = lduw_he_p(host);
        }
    } else if (!translator_ld(env, db, &val, pc, sizeof(val))) {
        MemOpIdx oi = make_memop_idx(MO_UW, db->code_mmuidx);
        val = cpu_ldw_code_mmu(env, pc, oi, 0);
        record_save(db, pc, &val, sizeof(val));
    }
    if (endian & MO_BSWAP) {
        val = bswap16(val);
    }
    return val;
}

uint32_t translator_ldl_end(CPUArchState *env, DisasContextBase *db,
                            vaddr pc, MemOp endian)
{
    uint32_t val;

    if (likely(tb_page_addr0(db->tb) != -1 &&
               ((db->pc_first ^ (pc + sizeof(val) - 1)) & TARGET_PAGE_MASK) == 0)) {
        const void *host = db->host_addr[0] + (pc - db->pc_first);

        if (QEMU_IS_ALIGNED(pc, sizeof(val))) {
            val = retro_atomic_load_relaxed_int((retro_atomic_int_t *)host);
        } else {
            val = ldl_he_p(host);
        }
    } else if (!translator_ld(env, db, &val, pc, sizeof(val))) {
        MemOpIdx oi = make_memop_idx(MO_UL, db->code_mmuidx);
        val = cpu_ldl_code_mmu(env, pc, oi, 0);
        record_save(db, pc, &val, sizeof(val));
    }
    if (endian & MO_BSWAP) {
        val = bswap32(val);
    }
    return val;
}

