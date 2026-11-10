/*
 *  emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *  Copyright (c) 2026 Réda Chérif-Touil
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "hw/core/cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "accel/tcg/helper-retaddr.h"
#include "trace.h"
#include "exec/cpu-common.h"
#include "exec/cpu-interrupt.h"
#include "exec/page-protection.h"
#include "exec/mmap-lock.h"
#include "exec/translation-block.h"
#include "tcg/tcg.h"
#include "qemu/rcu.h"
#include "qemu/qht-lookup.h"
#include "exec/log.h"
#include "qemu/main-loop.h"
#include "exec/icount.h"
#include "system/tcg.h"
#include "exec/helper-proto-common.h"
#include "tcg-accel-ops.h"
#include "tb-jmp-cache.h"
#include "tb-hash.h"
#include "tb-code-hash.h"
#include "tb-context.h"
#include "tb-internal.h"
#include "internal-common.h"
#include "qemu/cpu-boost.h"
#include "qemu/processor.h"
#include "exec/tb-flush.h"
#include "system/cpus.h"

struct tb_desc {
    TCGTBCPUState s;
    CPUArchState *env;
    tb_page_addr_t page_addr0;
    const void *host_pc;
};

static bool tb_lookup_cmp(const void *p, const void *d)
{
    const TranslationBlock *tb = p;
    const struct tb_desc *desc = d;

    if ((tb_cflags(tb) & CF_PCREL || tb->pc == desc->s.pc) &&
        tb_page_addr0(tb) == desc->page_addr0 &&
        tb->cs_base == desc->s.cs_base &&
        tb->flags == desc->s.flags &&
        (tb_cflags(tb) & ~CF_INVALID) == desc->s.cflags) {
        /* check next page if needed */
        tb_page_addr_t tb_phys_page1 = tb_page_addr1(tb);
        if (tb_phys_page1 == -1) {
            return true;
        } else {
            tb_page_addr_t phys_page1;
            vaddr virt_page1;

            /*
             * We know that the first page matched, and an otherwise valid TB
             * encountered an incomplete instruction at the end of that page,
             * therefore we know that generating a new TB from the current PC
             * must also require reading from the next page -- even if the
             * second pages do not match, and therefore the resulting insn
             * is different for the new TB.  Therefore any exception raised
             * here by the faulting lookup is not premature.
             */
            virt_page1 = TARGET_PAGE_ALIGN(desc->s.pc);
            phys_page1 = get_page_addr_code(desc->env, virt_page1);
            if (tb_phys_page1 == phys_page1) {
                return true;
            }
        }
    }
    return false;
}

static inline QEMU_ALWAYS_INLINE TranslationBlock *
tb_htable_lookup_common(CPUState *cpu, TCGTBCPUState s, const struct qht *ht,
                        qht_lookup_func_t func, tb_page_addr_t phys_pc,
                        const void *host_pc)
{
    struct tb_desc desc;
    uint32_t h;

    desc.s = s;
    desc.env = cpu_env(cpu);
    if (phys_pc == -1) {
        return NULL;
    }
    desc.page_addr0 = phys_pc;
    desc.host_pc = host_pc;
    h = tb_hash_func(phys_pc, (s.cflags & CF_PCREL ? 0 : s.pc),
                     s.flags, s.cs_base, s.cflags);
    return qht_lookup_inline(ht, &desc, h, func);
}

/*
 * CPU boost: a miss falls back to the block translated for any class of the
 * carry flag it starts with (XEMU_TB_CF_ANY), in both tables. Keep this
 * miss scan out of the primary jump-cache hit path.
 */
static __attribute__((noinline))
TranslationBlock *tb_htable_lookup(CPUState *cpu, TCGTBCPUState s)
{
    tb_page_addr_t phys_pc = get_page_addr_code(cpu_env(cpu), s.pc);
    TranslationBlock *tb = tb_htable_lookup_common(cpu, s, &tb_ctx.htable,
                                                   tb_lookup_cmp, phys_pc,
                                                   NULL);

    if (!tb && xemu_cpu_boost) {
        s.flags |= XEMU_TB_CF_ANY;
        tb = tb_htable_lookup_common(cpu, s, &tb_ctx.htable, tb_lookup_cmp,
                                     phys_pc, NULL);
    }
    return tb;
}

static bool inv_tb_lookup_cmp(const void *p, const void *d)
{
    const TranslationBlock *tb = p;
    const struct tb_desc *desc = d;

    return tb_lookup_cmp(p, d) &&
           tb->ihash == tb_code_hash_func_host(desc->env, desc->s.pc, tb->size,
                                                desc->host_pc);
}

TranslationBlock *inv_tb_htable_lookup(CPUState *cpu, TCGTBCPUState s,
                                       tb_page_addr_t phys_pc,
                                       const void *host_pc)
{
    /* tb_gen_code already performed the executable-page probe. Reuse that
     * result, including the non-cacheable -1 sentinel, before any execution. */
    TranslationBlock *tb = tb_htable_lookup_common(cpu, s, &tb_ctx.inv_htable,
                                                   inv_tb_lookup_cmp, phys_pc,
                                                   host_pc);

    if (!tb && xemu_cpu_boost) {
        s.flags |= XEMU_TB_CF_ANY;
        tb = tb_htable_lookup_common(cpu, s, &tb_ctx.inv_htable,
                                     inv_tb_lookup_cmp, phys_pc, host_pc);
    }
    return tb;
}

/* The Xbox target always uses the x86 hook, including without CPU boost.
 * Boost still controls the guest flag specialization inside that hook. */
#include "target/i386/tcg/tcg-cpu.h"

static inline TCGTBCPUState xemu_get_tb_cpu_state(CPUState *cpu)
{
    return x86_tb_cpu_state(cpu);
}

/* Most indirect branches need only the already configured flags. Keep the
 * single-step and debug policy in curr_cflags, off the usual call path. */
static inline uint32_t xemu_lookup_cflags(CPUState *cpu)
{
    if (unlikely(cpu->singlestep_enabled ||
                 retro_atomic_load_relaxed_int(&one_insn_per_tb) ||
                 qemu_loglevel_mask(CPU_LOG_TB_NOCHAIN))) {
        return curr_cflags(cpu);
    }
    return cpu->tcg_cflags;
}

/**
 * tb_lookup:
 * @cpu: CPU that will execute the returned translation block
 * @pc: guest PC
 * @cs_base: arch-specific value associated with translation block
 * @flags: arch-specific translation block flags
 * @cflags: CF_* flags
 *
 * Look up a translation block inside the QHT using @pc, @cs_base, @flags and
 * @cflags. Uses @cpu's tb_jmp_cache. Might cause an exception, so have a
 * longjmp destination ready.
 *
 * Returns: an existing translation block or NULL.
 */
static inline TranslationBlock *tb_lookup(CPUState *cpu, TCGTBCPUState s)
{
    TranslationBlock *tb;
    CPUJumpCache *jc;
    uint32_t hash;

    /* we should never be trying to look up an INVALID tb */
    tcg_debug_assert(!(s.cflags & CF_INVALID));

    hash = tb_jmp_cache_hash_func(s.pc);
    jc = cpu->tb_jmp_cache;

    tb = retro_atomic_load_relaxed_ptr(&jc->array[hash].tb);
    if (likely(tb &&
               jc->array[hash].pc == s.pc &&
               tb->cs_base == s.cs_base &&
               (tb->flags == s.flags ||
                tb->flags == (s.flags | XEMU_TB_CF_ANY)) &&
               tb_cflags(tb) == s.cflags)) {
        goto hit;
    }

    /* A different PC may have displaced the primary slot while its class
     * entry remains valid. Check the full identity in the secondary cache
     * after any primary miss, including an invalidated primary entry. */
    if (xemu_cpu_boost) {
        unsigned secondary = tb_jmp_cache_class_hash(s.pc, s.flags);
        tb = retro_atomic_load_relaxed_ptr(&jc->classes[secondary].tb);
        if (tb && jc->classes[secondary].pc == s.pc &&
            tb->cs_base == s.cs_base && tb->flags == s.flags &&
            tb_cflags(tb) == s.cflags) {
            /* Promote without touching the class entry or the global table. */
            jc->array[hash].pc = s.pc;
            retro_atomic_store_relaxed_ptr(&jc->array[hash].tb, tb);
            tb_jmp_cache_record(jc, hash, 0);
            goto hit;
        }
    }

    tb = tb_htable_lookup(cpu, s);
    if (tb == NULL) {
        return NULL;
    }

    tb_jmp_cache_store(jc, s.pc, s.flags, tb);

hit:
    /*
     * As long as tb is not NULL, the contents are consistent.  Therefore,
     * the virtual PC has to match for non-CF_PCREL translations.
     */
    assert((tb_cflags(tb) & CF_PCREL) || tb->pc == s.pc);
    return tb;
}



static bool check_for_breakpoints_slow(CPUState *cpu, vaddr pc,
                                       uint32_t *cflags)
{
    CPUBreakpoint *bp;
    bool match_page = false;

    /*
     * Singlestep overrides breakpoints.
     * This requirement is visible in the record-replay tests, where
     * we would fail to make forward progress in reverse-continue.
     *
     * TODO: gdb singlestep should only override gdb breakpoints,
     * so that one could (gdb) singlestep into the guest kernel's
     * architectural breakpoint handler.
     */
    if (cpu->singlestep_enabled) {
        return false;
    }

    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        /*
         * If we have an exact pc match, trigger the breakpoint.
         * Otherwise, note matches within the page.
         */
        if (pc == bp->pc) {
            bool match_bp = false;

            if (bp->flags & BP_GDB) {
                match_bp = true;
            } else if (bp->flags & BP_CPU) {
                const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;
                assert(tcg_ops->debug_check_breakpoint);
                match_bp = tcg_ops->debug_check_breakpoint(cpu);
            }

            if (match_bp) {
                cpu->exception_index = EXCP_DEBUG;
                return true;
            }
        } else if (((pc ^ bp->pc) & TARGET_PAGE_MASK) == 0) {
            match_page = true;
        }
    }

    /*
     * Within the same page as a breakpoint, single-step,
     * returning to helper_lookup_tb_ptr after each insn looking
     * for the actual breakpoint.
     *
     * TODO: Perhaps better to record all of the TBs associated
     * with a given virtual page that contains a breakpoint, and
     * then invalidate them when a new overlapping breakpoint is
     * set on the page.  Non-overlapping TBs would not be
     * invalidated, nor would any TB need to be invalidated as
     * breakpoints are removed.
     */
    if (match_page) {
        *cflags = (*cflags & ~CF_COUNT_MASK) | CF_NO_GOTO_TB | CF_BP_PAGE | 1;
    }
    return false;
}

static inline bool check_for_breakpoints(CPUState *cpu, vaddr pc,
                                         uint32_t *cflags)
{
    return unlikely(!QTAILQ_EMPTY(&cpu->breakpoints)) &&
        check_for_breakpoints_slow(cpu, pc, cflags);
}

/**
 * helper_lookup_tb_ptr: quick check for next tb
 * @env: current cpu state
 *
 * Look for an existing TB matching the current cpu state.
 * If found, return the code pointer.  If not found, return
 * the tcg epilogue so that we return into cpu_tb_exec.
 */
const void *HELPER(lookup_tb_ptr)(CPUArchState *env)
{
    CPUState *cpu = env_cpu(env);
    TranslationBlock *tb;

    /*
     * By definition we've just finished a TB, so I/O is OK.
     * Avoid the possibility of calling cpu_io_recompile() if
     * a page table walk triggered by tb_lookup() calling
     * probe_access_internal() happens to touch an MMIO device.
     * The next TB, if we chain to it, will clear the flag again.
     */
    cpu->neg.can_do_io = true;

    TCGTBCPUState s = xemu_get_tb_cpu_state(cpu);
    s.cflags = xemu_lookup_cflags(cpu);

    if (check_for_breakpoints(cpu, s.pc, &s.cflags)) {
        cpu_loop_exit(cpu);
    }

    tb = tb_lookup(cpu, s);
    if (tb == NULL) {

        return tcg_code_gen_epilogue;
    }



    return tb->tc.ptr;
}



/* Execute a TB, and fix up the CPU state afterwards if necessary */
/*
 * Disable CFI checks.
 * TCG creates binary blobs at runtime, with the transformed code.
 * A TB is a blob of binary code, created at runtime and called with an
 * indirect function call. Since such function did not exist at compile time,
 * the CFI runtime has no way to verify its signature and would fail.
 * TCG is not considered a security-sensitive part of QEMU so this does not
 * affect the impact of CFI in environment with high security requirements
 */
static inline TranslationBlock * QEMU_DISABLE_CFI
cpu_tb_exec(CPUState *cpu, TranslationBlock *itb, int *tb_exit)
{
    uintptr_t ret;
    TranslationBlock *last_tb;
    const void *tb_ptr = itb->tc.ptr;



    qemu_thread_jit_execute();
    ret = tcg_qemu_tb_exec(cpu_env(cpu), tb_ptr);
    cpu->neg.can_do_io = true;
    qemu_plugin_disable_mem_helpers(cpu);
    /*
     * TODO: Delay swapping back to the read-write region of the TB
     * until we actually need to modify the TB.  The read-only copy,
     * coming from the rx region, shares the same host TLB entry as
     * the code that executed the exit_tb opcode that arrived here.
     * If we insist on touching both the RX and the RW pages, we
     * double the host TLB pressure.
     */
    last_tb = tcg_splitwx_to_rw((void *)(ret & ~TB_EXIT_MASK));
    *tb_exit = ret & TB_EXIT_MASK;

    trace_exec_tb_exit(last_tb, *tb_exit);

    if (*tb_exit > TB_EXIT_IDX1) {
        /* We didn't start executing this TB (eg because the instruction
         * counter hit zero); we must restore the guest PC to the address
         * of the start of the TB.
         */
        x86_cpu_synchronize_from_tb(cpu, last_tb);

    }

    /*
     * If gdb single-step, and we haven't raised another exception,
     * raise a debug exception.  Single-step with another exception
     * is handled in cpu_handle_exception.
     */
    if (unlikely(cpu->singlestep_enabled) && cpu->exception_index == -1) {
        cpu->exception_index = EXCP_DEBUG;
        cpu_loop_exit(cpu);
    }

    return last_tb;
}


static inline void cpu_exec_enter(CPUState *cpu)
{
    x86_cpu_exec_enter(cpu);
}

static inline void cpu_exec_exit(CPUState *cpu)
{
    x86_cpu_exec_exit(cpu);
}

static void cpu_exec_longjmp_cleanup(CPUState *cpu)
{
    /* Non-buggy compilers preserve this; assert the correct value. */
    g_assert(cpu == current_cpu);

    /*
     * For softmmu, a tlb_fill fault during translation will land here,
     * and we need to release any page locks held.  In system mode we
     * have one tcg_ctx per thread, so we know it was this cpu doing
     * the translation.
     *
     * Alternative 1: Install a cleanup to be called via an exception
     * handling safe longjmp.  It seems plausible that all our hosts
     * support such a thing.  We'd have to properly register unwind info
     * for the JIT for EH, rather that just for GDB.
     *
     * Alternative 2: Set and restore cpu->jmp_env in tb_gen_code to
     * capture the cpu_loop_exit longjmp, perform the cleanup, and
     * jump again to arrive here.
     */
    if (tcg_ctx->gen_tb) {
        tb_unlock_pages(tcg_ctx->gen_tb);
        tcg_ctx->gen_tb = NULL;
    }
    /* Guest faults resume on the same frame owner. Longjmp bypasses C
     * cleanup guards; the ownership inherited at CPU entry remains held. */
    g_assert(bql_locked());
    assert_no_pages_locked();
}

void tb_set_jmp_target(TranslationBlock *tb, int n, uintptr_t addr)
{
    /*
     * Get the rx view of the structure, from which we find the
     * executable code address, and tb_target_set_jmp_target can
     * produce a pc-relative displacement to jmp_target_addr[n].
     */
    const TranslationBlock *c_tb = tcg_splitwx_to_rx(tb);
    uintptr_t offset = tb->jmp_insn_offset[n];
    uintptr_t jmp_rx = (uintptr_t)tb->tc.ptr + offset;
    uintptr_t jmp_rw = jmp_rx - tcg_splitwx_diff;

    tb->jmp_target_addr[n] = addr;
    tb_target_set_jmp_target(c_tb, n, jmp_rx, jmp_rw);
}

static inline void tb_add_jump(TranslationBlock *tb, int n,
                               TranslationBlock *tb_next)
{
    qemu_thread_jit_write();
    assert(n < ARRAY_SIZE(tb->jmp_list_next));
    qemu_spin_lock(&tb_next->jmp_lock);

    /* make sure the destination TB is valid */
    if (tb_cflags(tb_next) & CF_INVALID) {
        goto out_unlock_next;
    }
    /* Atomically claim the jump destination slot only if it was NULL */
    if (!retro_atomic_cas_64(&tb->jmp_dest[n], 0,
                             (int64_t)(uintptr_t)tb_next)) {
        goto out_unlock_next;
    }

    /* patch the native jump address */
    tb_set_jmp_target(tb, n, (uintptr_t)tb_next->tc.ptr);

    /* add in TB jmp list */
    tb->jmp_list_next[n] = tb_next->jmp_list_head;
    tb_next->jmp_list_head = (uintptr_t)tb | n;

    qemu_spin_unlock(&tb_next->jmp_lock);

    qemu_log_mask(CPU_LOG_EXEC, "Linking TBs %p index %d -> %p\n",
                  tb->tc.ptr, n, tb_next->tc.ptr);
    return;

 out_unlock_next:
    qemu_spin_unlock(&tb_next->jmp_lock);
}

static inline bool cpu_handle_halt(CPUState *cpu)
{
    if (cpu->halted) {
        const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;
        bool leave_halt = tcg_ops->cpu_exec_halt(cpu);

        if (!leave_halt) {
            return true;
        }

        cpu->halted = 0;
    }

    return false;
}

static inline void cpu_handle_debug_exception(CPUState *cpu)
{
    const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;
    CPUWatchpoint *wp;

    if (!cpu->watchpoint_hit) {
        QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
            wp->flags &= ~BP_WATCHPOINT_HIT;
        }
    }

    if (tcg_ops->debug_excp_handler) {
        tcg_ops->debug_excp_handler(cpu);
    }
}

static inline bool cpu_handle_exception(CPUState *cpu, int *ret)
{
    if (cpu->exception_index < 0) {
        return false;
    }

    if (cpu->exception_index >= EXCP_INTERRUPT) {
        /* exit request from the cpu execution loop */
        *ret = cpu->exception_index;
        if (*ret == EXCP_DEBUG) {
            cpu_handle_debug_exception(cpu);
        }
        cpu->exception_index = -1;
        return true;
    }

    const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;

    tcg_ops->do_interrupt(cpu);
    cpu->exception_index = -1;

    if (unlikely(cpu->singlestep_enabled)) {
        /*
         * After processing the exception, ensure an EXCP_DEBUG is
         * raised when single-stepping so that GDB doesn't miss the
         * next instruction.
         */
        *ret = EXCP_DEBUG;
        cpu_handle_debug_exception(cpu);
        return true;
    }

    return false;
}

void tcg_kick_vcpu_thread(CPUState *cpu)
{
    /*
     * Ensure cpu_exec will see the reason why the exit request was set.
     * FIXME: this is not always needed.  Other accelerators instead
     * read interrupt_request and set exit_request on demand from the
     * CPU thread; see kvm_arch_pre_run() for example.
     */
    retro_atomic_store_release_int(&cpu->exit_request, 1);

    /* Ensure cpu_exec will see the exit request after TCG has exited.  */
    retro_atomic_store_release_u16(&cpu->neg.icount_decr.u16.high, 0xffff);
}

static inline bool icount_exit_request(CPUState *cpu)
{
    if (!icount_enabled()) {
        return false;
    }
    if (cpu->cflags_next_tb != -1 && !(cpu->cflags_next_tb & CF_USE_ICOUNT)) {
        return false;
    }
    return retro_atomic_load_relaxed_u16(&cpu->neg.icount_decr.u16.low)
           + cpu->icount_extra == 0;
}

static inline bool cpu_handle_interrupt(CPUState *cpu,
                                        TranslationBlock **last_tb)
{
    /*
     * If we have requested custom cflags with CF_NOIRQ we should
     * skip checking here. Any pending interrupts will get picked up
     * by the next TB we execute under normal cflags.
     */
    if (cpu->cflags_next_tb != -1 && cpu->cflags_next_tb & CF_NOIRQ) {
        return false;
    }

    /* Clear the interrupt flag now since we're processing
     * cpu->interrupt_request and cpu->exit_request.
     * Ensure zeroing happens before reading cpu->exit_request or
     * cpu->interrupt_request (see also store-release in
     * tcg_kick_vcpu_thread())
     */
    retro_atomic_store_release_u16(&cpu->neg.icount_decr.u16.high, 0);
    retro_atomic_thread_fence_seq_cst();

    if (unlikely(cpu_test_interrupt(cpu, ~0))) {
        if (cpu_test_interrupt(cpu, CPU_INTERRUPT_DEBUG)) {
            cpu_reset_interrupt(cpu, CPU_INTERRUPT_DEBUG);
            cpu->exception_index = EXCP_DEBUG;
            return true;
        }
        if (cpu_test_interrupt(cpu, CPU_INTERRUPT_HALT)) {
            cpu_reset_interrupt(cpu, CPU_INTERRUPT_HALT);
            cpu->halted = 1;
            cpu->exception_index = EXCP_HLT;
            return true;
        } else {
            const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;
            int interrupt_request =
                retro_atomic_load_relaxed_int(&cpu->interrupt_request);

            if (cpu_test_interrupt(cpu, CPU_INTERRUPT_RESET)) {
                tcg_ops->cpu_exec_reset(cpu);
                return true;
            }

            if (unlikely(cpu->singlestep_enabled & SSTEP_NOIRQ)) {
                /* Mask out external interrupts for this step. */
                interrupt_request &= ~CPU_INTERRUPT_SSTEP_MASK;
            }

            /*
             * The target hook has 3 exit conditions:
             * False when the interrupt isn't processed,
             * True when it is, and we should restart on a new TB,
             * and via longjmp via cpu_loop_exit.
             */
            if (tcg_ops->cpu_exec_interrupt(cpu, interrupt_request)) {
                /* something happened: a wait ends here */
                cpu->neg.lr_quiet = LR_QUIET_INSNS;
                cpu->lr_poll_runs = 0;
                /*
                 * After processing the interrupt, ensure an EXCP_DEBUG is
                 * raised when single-stepping so that GDB doesn't miss the
                 * next instruction.
                 */
                if (unlikely(cpu->singlestep_enabled)) {
                    cpu->exception_index = EXCP_DEBUG;
                    return true;
                }
                cpu->exception_index = -1;
                *last_tb = NULL;
            }
        }
        if (cpu_test_interrupt(cpu, CPU_INTERRUPT_EXITTB)) {
            cpu_reset_interrupt(cpu, CPU_INTERRUPT_EXITTB);
            /* ensure that no TB jump will be modified as
               the program flow was changed */
            *last_tb = NULL;
        }

    }

    /*
     * Finally, check if we need to exit to the main loop.
     * The corresponding store-release is in cpu_exit.
     */
    if (unlikely(retro_atomic_load_acquire_int(&cpu->exit_request)) ||
        icount_exit_request(cpu)) {
        if (cpu->exception_index == -1) {
            cpu->exception_index = EXCP_INTERRUPT;
        }
        return true;
    }

    return false;
}

/* A posted interrupt need not be deliverable (for example, masked by the
 * target). Use the existing handler before the idle proof rather than
 * disabling idle handling whenever any interrupt bit is set. */
static bool cpu_kernel_idle_wait(CPUState *cpu, TranslationBlock **last_tb)
{
    if ((cpu_test_interrupt(cpu, ~0) ||
         retro_atomic_load_acquire_int(&cpu->exit_request)) &&
        cpu_handle_interrupt(cpu, last_tb)) {
        cpu_loop_exit(cpu);
    }
    return xemu_libretro_kernel_idle(cpu) || xemu_libretro_memory_poll(cpu);
}

static inline void cpu_loop_exec_tb(CPUState *cpu, TranslationBlock *tb,
                                    vaddr pc, TranslationBlock **last_tb,
                                    int *tb_exit)
{
    trace_exec_tb(tb, pc);
    tb = cpu_tb_exec(cpu, tb, tb_exit);
    if (*tb_exit != TB_EXIT_REQUESTED) {
        *last_tb = tb;
        /* Kernel idle loops can return at interrupt-shadow boundaries. A
         * short run of such exits only nominates a candidate: the opcode
         * pattern and empty-list/dispatch conditions must prove the wait.
         * Active code that happens to exit this way keeps running. */
        if (tb) {
            return;
        }
        if (cpu->neg.lr_quiet < cpu->lr_quiet_at_exit &&
            cpu->neg.lr_quiet != LR_QUIET_INSNS) {
            /* counted down, not reset, since the last exit (a block that
             * stored leaves the count at exactly LR_QUIET_INSNS: the reset
             * comes after the block's countdown, a rep stos's every
             * step) */
            cpu->lr_quiet_at_exit = cpu->neg.lr_quiet;
            if (++cpu->lr_quiet_exits < LR_QUIET_EXITS) {
                return;
            }
            cpu->lr_quiet_exits = 0;
            if (!cpu_kernel_idle_wait(cpu, last_tb)) {
                return;
            }
            cpu->neg.lr_quiet = -1;
        } else {
            cpu->lr_quiet_exits = 0;
            cpu->lr_quiet_at_exit = cpu->neg.lr_quiet;
            return;
        }
    }

    *last_tb = NULL;
    bool poll_wait = cpu->lr_poll_wait;
    if (poll_wait) {
        /* a tight poll loop (cputlb.c lr_poll_read) asked for this exit:
         * the same wait as a quiet run */
        cpu->lr_poll_wait = false;
        cpu->neg.lr_quiet = -1;
    }
    if (cpu->neg.lr_quiet < 0) {
        /* A store-free stretch is only a candidate: register-only work
         * and read-only scans still make progress. Skip time only for a
         * verified kernel idle loop or an explicit device/RAM-poll wait. */
        extern void lr_quiet_exit(CPUState *cpu);
        cpu->neg.lr_quiet = LR_QUIET_INSNS;
        cpu->lr_quiet_at_exit = LR_QUIET_INSNS;
        if (poll_wait || cpu_kernel_idle_wait(cpu, last_tb)) {
            lr_quiet_exit(cpu);
        }
        if (!tb || cpu_loop_exit_requested(cpu)) {
            /* a plain block-end exit (tb NULL) has no budget to refill */
            return;
        }
    }
    if (cpu_loop_exit_requested(cpu)) {
        /* Something asked us to stop executing chained TBs; just
         * continue round the main loop. Whatever requested the exit
         * will also have set something else (eg exit_request or
         * interrupt_request) which will be handled by
         * cpu_handle_interrupt.  cpu_handle_interrupt will also
         * clear cpu->icount_decr.u16.high.
         */
        return;
    }

    /* Instruction counter expired.  */
    assert(icount_enabled());
    /* Ensure global icount has gone forward */
    icount_update(cpu);
    /* Refill decrementer and continue execution.  */
    int32_t insns_left = MIN(0xffff, cpu->icount_budget);
    retro_atomic_store_relaxed_u16(&cpu->neg.icount_decr.u16.low, insns_left);
    cpu->icount_extra = cpu->icount_budget - insns_left;

    /*
     * If the next tb has more instructions than we have left to
     * execute we need to ensure we find/generate a TB with exactly
     * insns_left instructions in it.
     */
    if (insns_left > 0 && insns_left < tb->icount)  {
        assert(insns_left <= CF_COUNT_MASK);
        assert(cpu->icount_extra == 0);
        cpu->cflags_next_tb = (tb->cflags & ~CF_COUNT_MASK) | insns_left;
    }
}

/* main execution loop */

static int __attribute__((noinline))
cpu_exec_loop(CPUState *cpu)
{
    int ret;

    /* if an exception is pending, we execute it here */
    while (!cpu_handle_exception(cpu, &ret)) {
        TranslationBlock *last_tb = NULL;
        int tb_exit = 0;

        while (!cpu_handle_interrupt(cpu, &last_tb)) {
            TranslationBlock *tb;
            TCGTBCPUState s = xemu_get_tb_cpu_state(cpu);
            s.cflags = cpu->cflags_next_tb;

            /*
             * When requested, use an exact setting for cflags for the next
             * execution.  This is used for icount, precise smc, and stop-
             * after-access watchpoints.  Since this request should never
             * have CF_INVALID set, -1 is a convenient invalid value that
             * does not require tcg headers for cpu_common_reset.
             */
            if (s.cflags == -1) {
                s.cflags = curr_cflags(cpu);
            } else {
                cpu->cflags_next_tb = -1;
            }

            if (check_for_breakpoints(cpu, s.pc, &s.cflags)) {
                break;
            }

            tb = tb_lookup(cpu, s);
            if (tb == NULL) {
                uint64_t generation = tb_ctx.flush_generation;
                CPUJumpCache *jc;

                mmap_lock();
                tb = tb_gen_code(cpu, s);
                mmap_unlock();

                /* Serial translation can flush a full code buffer and
                 * continue in place. The old block may now be reused. */
                if (unlikely(generation != tb_ctx.flush_generation)) {
                    last_tb = NULL;
                }

                /*
                 * We add the TB in the virtual pc hash table
                 * for the fast lookup
                 */
                jc = cpu->tb_jmp_cache;
                tb_jmp_cache_store(jc, s.pc, s.flags, tb);
            }

            /*
             * We don't take care of direct jumps when address mapping
             * changes in system emulation.  So it's not safe to make a
             * direct jump to a TB spanning two pages because the mapping
             * for the second page can change.
             */
            if (tb_page_addr1(tb) != -1) {
                last_tb = NULL;
            }
            /* See if we can patch the calling TB. */
            if (last_tb) {
                tb_add_jump(last_tb, tb_exit, tb);
            }

            cpu_loop_exec_tb(cpu, tb, s.pc, &last_tb, &tb_exit);

        }
    }
    return ret;
}

static int cpu_exec_setjmp(CPUState *cpu)
{
    /* Prepare setjmp context for exception handling. */
    if (unlikely(sigsetjmp(cpu->jmp_env, 0) != 0)) {
        cpu_exec_longjmp_cleanup(cpu);
    }

    return cpu_exec_loop(cpu);
}

int cpu_exec(CPUState *cpu)
{
    int ret;

    /* Only the libretro frame owner executes this core's sole guest CPU. */
    g_assert(bql_locked());

    SET_current_cpu(cpu);

    if (cpu_handle_halt(cpu)) {
        return EXCP_HALTED;
    }

    RCU_READ_LOCK_GUARD();
    cpu_exec_enter(cpu);

    ret = cpu_exec_setjmp(cpu);

    cpu_exec_exit(cpu);
    return ret;
}

bool tcg_exec_realizefn(CPUState *cpu, Error **errp)
{
    static bool tcg_target_initialized;

    if (!tcg_target_initialized) {
        /* Check mandatory TCGCPUOps handlers */
        const TCGCPUOps *tcg_ops = cpu->cc->tcg_ops;
        assert(tcg_ops->cpu_exec_halt);
        assert(tcg_ops->cpu_exec_interrupt);
        assert(tcg_ops->cpu_exec_reset);
        assert(tcg_ops->pointer_wrap);
        assert(tcg_ops->translate_code);
        assert(tcg_ops->get_tb_cpu_state);
        assert(tcg_ops->mmu_index);
        tcg_ops->initialize();
        tcg_target_initialized = true;
    }

    cpu->tb_jmp_cache = g_new0(CPUJumpCache, 1);
    tlb_init(cpu);
    tcg_iommu_init_notifier_list(cpu);
    /* qemu_plugin_vcpu_init_hook delayed until cpu_index assigned. */

    return true;
}

/* undo the initializations in reverse order */
void tcg_exec_unrealizefn(CPUState *cpu)
{
    tcg_iommu_free_notifier_list(cpu);

    tlb_destroy(cpu);
    g_free_rcu(cpu->tb_jmp_cache, rcu);
}
