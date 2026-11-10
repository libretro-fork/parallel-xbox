/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compile the actual backend and execute its emitted x86-64 guards. */
#ifdef CONFIG_DEBUG_TCG
#define TEST_TLB_MEMO_DEBUG 1
#endif
#include "qemu/osdep.h"
#ifdef TEST_TLB_MEMO_DEBUG
/* Pregenerated release host configs undefine this command-line option. */
#define CONFIG_DEBUG_TCG 1
#endif
#include "tcg/tcg.c"
#include "tcg/tcg-common.c"
#include "tcg-tlb-plan-reference.h"
#include "exec/tlb-flags.h"

const TargetPageBits target_page = {.decided = true, .bits = 12, .mask = ~UINT64_C(4095)};
unsigned cpuinfo;
void *g_malloc(size_t n) { abort(); }
void g_free(void *p) { free(p); }
void gen_bb_epilogue(void) { abort(); }
G_NORETURN void g_assertion_message_expr(const char *domain, const char *file,
                                       int line, const char *func, const char *expr)
{
    fprintf(stderr, "%s:%d: %s: %s\n", file, line, func, expr ? expr : "unreachable");
    abort();
}

typedef struct {
    void *env;
    uint32_t addr;
    uint64_t ntag;
    uintptr_t addend;
    CPUTLBEntry *entry;
} Input;
typedef uintptr_t (*Probe)(Input *);
static TCGContext context;
static unsigned char code[16384] QEMU_ALIGNED(4096);

static void finish(TCGContext *s)
{
#ifdef _WIN64
    tgen_arithi(s, ARITH_ADD + P_REXW, TCG_REG_ESP, 32, 0);
#endif
    tcg_out_pop(s, TCG_REG_EBX);
    tcg_out_pop(s, TCG_REG_R12);
    tcg_out_pop(s, TCG_REG_EBP);
    tcg_out8(s, 0xc3);
}
static Probe build(bool load, unsigned size, unsigned align, bool rewalk, bool invalidate)
{
    TCGContext *s = &context;
    memset(s, 0, sizeof(*s));
    s->addr_type = TCG_TYPE_I32;
    s->code_buf = s->code_ptr = code;
    s->tlbm_filled = 3;
    int slot = !load;
    MemOpIdx oi = make_memop_idx(size | (align << MO_ASHIFT), 0);
    TCGLabelQemuLdst access = {.is_ld = load, .oi = oi, .addr_reg = TCG_REG_EAX,
                               .memo_slot = slot + 1};
    tcg_out_push(s, TCG_REG_EBP);
    tcg_out_push(s, TCG_REG_R12);
    tcg_out_push(s, TCG_REG_EBX);
#ifdef _WIN64
    tgen_arithi(s, ARITH_SUB + P_REXW, TCG_REG_ESP, 32, 0);
#endif
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R12, tcg_target_call_iarg_regs[0]);
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_AREG0, TCG_REG_R12, offsetof(Input, env));
    tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_EAX, TCG_REG_R12, offsetof(Input, addr));
    tcg_out_ld(s, TCG_TYPE_I64, TLBM_NTAG(slot), TCG_REG_R12, offsetof(Input, ntag));
    tcg_out_ld(s, TCG_TYPE_PTR, slot ? TLBM_ENT : TCG_REG_R11, TCG_REG_R12,
               slot ? offsetof(Input, entry) : offsetof(Input, addend));
    if (invalidate) tlbm_out_invalidate(s);
    tlbm_out_share(s, &access, TCG_REG_EAX, oi, load, align, slot);
    access.memo_access = s->code_ptr;
    tcg_out_modrm_sib_offset(s, OPC_LEA + P_REXW, TCG_REG_EAX, TCG_REG_EAX,
                             tlbm_addend_reg(slot), 0, 0);
    finish(s);
    for (unsigned i = 0; i < 2; ++i)
        if (access.label_ptr[i]) tcg_patch32(access.label_ptr[i], s->code_ptr - access.label_ptr[i] - 4);
    if (rewalk) tlbm_out_slow_entry(s, &access);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_EAX, -1);
    finish(s);
    assert(s->code_ptr < code + sizeof(code));
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), code, s->code_ptr - code);
#endif
    return (Probe)code;
}

/* Exercise the production access emitters as well as manually-built IR. */
#include "tcg/tcg-op.c"
#include "tcg/tcg-op-ldst.c"

static void check_plan_count(void)
{
    typedef union {max_align_t align; unsigned char data[sizeof(TCGOp) + 5 * sizeof(TCGArg)];} Op;
    static Op ops[128];
    static unsigned char arena[65536] QEMU_ALIGNED(16);
    static TCGContext foreign;
    static const TCGOpcode codes[] = {INDEX_op_qemu_ld, INDEX_op_qemu_st,
        INDEX_op_qemu_ld2, INDEX_op_qemu_st2, INDEX_op_mov, INDEX_op_add,
        INDEX_op_sub, INDEX_op_set_label, INDEX_op_exit_tb};
    uint32_t rng = 0x12345678;
#ifdef _WIN32
    tls_key_tcg_ctx = TlsAlloc(); assert(tls_key_tcg_ctx != TLS_OUT_OF_INDEXES);
#endif
    SET_tcg_ctx(&foreign);
    for (unsigned trial = 0; trial < 10000; trial++) {
        TCGContext *s = &context;
        memset(s, 0, sizeof(*s));
        s->addr_type = TCG_TYPE_I32; s->nb_temps = 16; s->nb_globals = 4;
        QTAILQ_INIT(&s->ops);
        for (unsigned j=0;j<16;j++) s->temps[j]=(TCGTemp){.kind=j<4?TEMP_GLOBAL:TEMP_EBB,.type=TCG_TYPE_I32};
        s->temps[15]=(TCGTemp){.kind=TEMP_CONST,.type=TCG_TYPE_I32,.val=16};
        unsigned actual=0, length=trial%129;
        for (unsigned j=0;j<length;j++) {
            rng=rng*1664525u+1013904223u;
            TCGOp *op=(TCGOp *)ops[j].data;
            memset(op,0,sizeof(ops[j])); op->opc=codes[(rng>>8)%G_N_ELEMENTS(codes)];
            unsigned nd=(op->opc==INDEX_op_qemu_ld2 || op->opc==INDEX_op_qemu_st2)?2:1;
            for (unsigned k=0;k<5;k++) op->args[k]=temp_arg(&s->temps[(j+k)%14]);
            if (op->opc==INDEX_op_qemu_ld || op->opc==INDEX_op_qemu_st ||
                op->opc==INDEX_op_qemu_ld2 || op->opc==INDEX_op_qemu_st2) {
                op->args[nd]=temp_arg(&s->temps[(rng>>16)%4]);
                op->args[nd+1]=make_memop_idx(rng%4,(rng>>20)%2); actual++;
            } else if (op->opc==INDEX_op_add || op->opc==INDEX_op_sub) {
                op->args[2]=temp_arg(&s->temps[15]);
            }
            QTAILQ_INSERT_TAIL(&s->ops,op,link); s->nb_ops++;
        }
        /* Removed accesses leave a conservative emission bound. */
        s->nb_guest_access=actual+trial%17;
        s->pool_cur=(uintptr_t)arena; s->pool_end=s->pool_cur+sizeof(arena);
        reference_tlbm_plan(s);
        uint8_t expected[129];
        for (unsigned j=0;j<=actual;j++) expected[j]=tlbm_take_hint(s);
        s->pool_cur=(uintptr_t)arena;
        tcg_tlbm_plan(s);
        for (unsigned j=0;j<=actual;j++) assert(tlbm_take_hint(s)==expected[j]);
    }
    assert(!foreign.pool_cur);
    SET_tcg_ctx(&context);
    memset(&context,0,sizeof(context)); context.addr_type=TCG_TYPE_I32;
    tcg_func_start(&context);
    context.pool_cur=(uintptr_t)arena; context.pool_end=context.pool_cur+sizeof(arena);
    for (unsigned j=0;j<16;j++) context.temps[j]=(TCGTemp){.kind=TEMP_EBB,.type=TCG_TYPE_I32};
    context.nb_temps=16;
    gen_ldst1(INDEX_op_qemu_ld,TCG_TYPE_I32,&context.temps[0],&context.temps[1],make_memop_idx(MO_32,0));
    gen_ldst1(INDEX_op_qemu_st,TCG_TYPE_I32,&context.temps[0],&context.temps[1],make_memop_idx(MO_32,0));
    gen_ldst2(INDEX_op_qemu_ld2,TCG_TYPE_I64,&context.temps[0],&context.temps[1],&context.temps[2],make_memop_idx(MO_64,0));
    gen_ldst2(INDEX_op_qemu_st2,TCG_TYPE_I64,&context.temps[0],&context.temps[1],&context.temps[2],make_memop_idx(MO_64,0));
    assert(context.nb_guest_access==4);
    tcg_func_start(&context); assert(!context.nb_guest_access);
#ifdef _WIN32
    TlsFree(tls_key_tcg_ctx);
#endif
    puts("TLB planner: 10000 differential traces, conservative deletion bounds, real emitters and restart reset PASS");
}

static void check_plan(void)
{
    typedef union {max_align_t align; unsigned char data[sizeof(TCGOp) + 5 * sizeof(TCGArg)];} Op;
    static unsigned char arena[65536] QEMU_ALIGNED(16);
    static TCGContext foreign;
    Op ops[11];
    static const TCGOpcode codes[] = {INDEX_op_qemu_ld, INDEX_op_mov, INDEX_op_add,
        INDEX_op_qemu_ld, INDEX_op_qemu_st, INDEX_op_qemu_st, INDEX_op_call,
        INDEX_op_qemu_ld, INDEX_op_qemu_ld, INDEX_op_set_label, INDEX_op_qemu_ld};
    static TCGHelperInfo helper;
    tcg_target_init(&context);
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned j = 0; j < 2; ++j) assert(tcg_regset_test_reg(context.reserved_regs, tlbm_regs[i][j]));
#ifdef _WIN32
    tls_key_tcg_ctx = TlsAlloc(); assert(tls_key_tcg_ctx != TLS_OUT_OF_INDEXES);
#endif
    SET_tcg_ctx(&foreign);
    for (unsigned mode = 0; mode < 7; ++mode) {
        TCGContext *s = &context;
        memset(s, 0, sizeof(*s)); memset(ops, 0, sizeof(ops));
        s->nb_temps = 12; s->nb_globals = 4; s->addr_type = TCG_TYPE_I32;
        s->pool_cur = (uintptr_t)arena; s->pool_end = s->pool_cur + sizeof(arena);
        QTAILQ_INIT(&s->ops);
        for (unsigned i = 0; i < 12; ++i)
            s->temps[i] = (TCGTemp){.kind = i < 4 ? TEMP_GLOBAL : TEMP_EBB, .type = TCG_TYPE_I32};
        s->temps[11] = (TCGTemp){.kind = TEMP_CONST, .type = TCG_TYPE_I32, .val = 16};
        if (mode >= 3 && mode <= 5) s->temps[0] = (TCGTemp){.kind = TEMP_CONST, .val = mode == 3 ? 0x100 : mode == 4 ? 0xff0 : 0xfffffff0u};
        for (unsigned i = 0; i < 11; ++i) {
            TCGOp *op = (TCGOp *)ops[i].data;
            op->opc = codes[i];
            for (unsigned j = 0; j < 5; ++j) op->args[j] = temp_arg(&s->temps[j]);
            if (op->opc == INDEX_op_qemu_ld || op->opc == INDEX_op_qemu_st) {
                op->args[0] = temp_arg(&s->temps[op->opc == INDEX_op_qemu_st ? 3 : 4]);
                op->args[1] = temp_arg(&s->temps[i == 3 || i == 5 ? 2 : 0]);
                if (mode == 6 && (i == 7 || i == 8)) op->args[1] = temp_arg(&s->temps[2]);
                op->args[2] = make_memop_idx(MO_32, mode == 1 && i == 3 ? 1 : 0);
                if (mode == 2 && !i) op->args[0] = temp_arg(&s->temps[0]);
            } else if (i == 1) {
                op->args[0] = temp_arg(&s->temps[1]); op->args[1] = temp_arg(&s->temps[0]);
            } else if (i == 2) {
                op->args[0] = temp_arg(&s->temps[2]); op->args[1] = temp_arg(&s->temps[1]); op->args[2] = temp_arg(&s->temps[11]);
            } else if (op->opc == INDEX_op_call) {
                TCGOP_CALLI(op) = TCGOP_CALLO(op) = 0; op->args[1] = (uintptr_t)&helper;
            }
            QTAILQ_INSERT_TAIL(&s->ops, op, link); ++s->nb_ops;
        }
        s->nb_guest_access = 7;
        tcg_tlbm_plan(s);
        int want[] = {TLBM_FILL, TLBM_SHARE, TLBM_FILL, TLBM_SHARE, TLBM_FILL, TLBM_SHARE, TLBM_WALK};
        if (mode == 1 || mode == 2 || mode == 4 || mode == 5) want[0] = want[1] = TLBM_WALK;
        if (mode == 4 || mode == 5) want[2] = want[3] = TLBM_WALK;
        assert(s->tlbm_nhint == 7);
        for (unsigned i = 0; i < 7; ++i) assert(tlbm_take_hint(s) == want[i]);
        assert(tlbm_take_hint(s) == TLBM_WALK);
        tcg_tlbm_forget(s); assert(s->tlbm_mmu[0] == -1 && s->tlbm_mmu[1] == -1);
    }
    assert(!foreign.pool_cur);
#ifdef _WIN32
    TlsFree(tls_key_tcg_ctx);
#endif
    puts("TLB memo planner: copy/offset roots, address overwrites, MMU separation, constants, page/wrap bounds, call/label barriers and foreign TLS PASS");
}

static uint64_t sum_args(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                         uint64_t e, uint64_t f, uint64_t g)
{
    return a * 3 + b * 5 + c * 7 + d * 11 + e * 13 + f * 17 + g * 19;
}
static void check_call_abi(void *env)
{
    typedef union {max_align_t align; unsigned char data[sizeof(TCGOp) + 10 * sizeof(TCGArg)];} Op;
    typedef uint64_t (*CallProbe)(void *);
    for (unsigned trial = 0; trial < 128; ++trial) {
        TCGContext *s = &context;
        memset(s, 0, sizeof(*s));
        tcg_target_init(s);
        tcg_regset_set_reg(s->reserved_regs, TCG_AREG0);
        tcg_regset_set_reg(s->reserved_regs, TCG_TARGET_LR_ICOUNT_REG);
        s->code_buf = s->code_ptr = code;
        s->addr_type = TCG_TYPE_I32;
        tcg_out_push(s, TCG_REG_EBP); tcg_out_push(s, TCG_REG_R12);
        tcg_out_push(s, TCG_REG_EBX); tcg_out_push(s, TCG_REG_R15); tcg_out_push(s, TCG_REG_R13);
        tgen_arithi(s, ARITH_SUB + P_REXW, TCG_REG_ESP, 128, 0);
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R15, 1234);
        Op storage = {0}; TCGOp *op = (TCGOp *)storage.data;
        TCGHelperInfo info = {.func = sum_args, .flags = TCG_CALL_NO_RWG,
                              .nr_in = 7, .nr_out = 1, .out_kind = TCG_CALL_RET_NORMAL};
        op->opc = INDEX_op_call; TCGOP_CALLI(op) = 7; TCGOP_CALLO(op) = 1;
        s->nb_globals = 1; s->nb_temps = 9;
        s->temps[0] = (TCGTemp){.kind = TEMP_FIXED, .type = TCG_TYPE_PTR,
            .base_type = TCG_TYPE_PTR, .val_type = TEMP_VAL_REG, .reg = TCG_AREG0};
        s->reg_to_temp[TCG_AREG0] = &s->temps[0];
        s->temps[1] = (TCGTemp){.kind = TEMP_EBB, .type = TCG_TYPE_I64,
                                .base_type = TCG_TYPE_I64, .val_type = TEMP_VAL_DEAD};
        op->args[0] = temp_arg(&s->temps[1]);
        uint64_t args[7];
        for (unsigned i = 0; i < 7; ++i) {
            args[i] = UINT64_C(0x1234567887654321) * (trial + i + 1);
            s->temps[i + 2] = (TCGTemp){.kind = TEMP_CONST, .type = TCG_TYPE_I64,
                .base_type = TCG_TYPE_I64, .val_type = TEMP_VAL_CONST, .val = args[i]};
            if (trial & 1) {
                TCGTemp *temp = &s->temps[i + 2];
                temp->kind = TEMP_EBB; temp->val_type = TEMP_VAL_MEM;
                temp->mem_base = &s->temps[0]; temp->mem_offset = i * 8;
                temp->mem_allocated = temp->mem_coherent = true;
                memcpy((unsigned char *)env + i * 8, &args[i], 8);
            }
            op->args[i + 1] = temp_arg(&s->temps[i + 2]);
            info.in[i].kind = TCG_CALL_ARG_NORMAL; info.in[i].arg_slot = i;
        }
        op->args[8] = (uintptr_t)sum_args; op->args[9] = (uintptr_t)&info;
        tcg_reg_alloc_call(s, op);
        assert(s->tlbm_mmu[0] == -1 && s->tlbm_mmu[1] == -1);
        tgen_arithi(s, ARITH_ADD + P_REXW, TCG_REG_ESP, 128, 0);
        tcg_out_pop(s, TCG_REG_R13); tcg_out_pop(s, TCG_REG_R15); tcg_out_pop(s, TCG_REG_EBX);
        tcg_out_pop(s, TCG_REG_R12); tcg_out_pop(s, TCG_REG_EBP); tcg_out8(s, 0xc3);
#ifdef _WIN32
        FlushInstructionCache(GetCurrentProcess(), code, s->code_ptr - code);
#endif
        uint64_t actual = ((CallProbe)code)(env);
        assert(actual == sum_args(args[0], args[1], args[2], args[3], args[4], args[5], args[6]));
    }
    puts("TLB memo helper ABI: 128 actual seven-argument calls, constant/memory inputs, R8/R9 reuse, stack arguments and budget synchronization PASS");
}
int main(void)
{
    check_plan();
    check_plan_count();
#ifdef _WIN32
    DWORD previous;
    assert(VirtualProtect(code, sizeof(code), PAGE_EXECUTE_READWRITE, &previous));
#else
    assert(!mprotect(code, sizeof(code), PROT_READ | PROT_WRITE | PROT_EXEC));
#endif
    static struct {CPUNegativeOffsetState neg; unsigned char env[64];} cpu;
    check_call_abi(cpu.env);
    static CPUTLBEntry entries[64];
    static unsigned char ram[8192];
    cpu.neg.tlb.f[mmuidx_to_fast_index(0)].mask = 63u << CPU_TLB_ENTRY_BITS;
    cpu.neg.tlb.f[mmuidx_to_fast_index(0)].table = entries;
    Input input = {.env = cpu.env};
    unsigned cases = 0;
    for (unsigned load = 0; load < 2; ++load)
    for (unsigned size = 0; size <= 4; ++size)
    for (unsigned align = 0; align <= 4; ++align)
    for (unsigned rewalk = 0; rewalk < 2; ++rewalk)
    for (unsigned invalidate = 0; invalidate < 2; ++invalidate) {
        Probe probe = build(load, size, align, rewalk, invalidate);
        for (unsigned i = 0; i < 12288; ++i)
        for (unsigned variant = 0; variant < 7; ++variant) {
            static const uint32_t pages[] = {0, 0x1000u, 0xfffff000u};
            uint32_t page = pages[i / 4096];
            input.addr = page + i % 4096;
            CPUTLBEntry *live = &entries[(input.addr >> 12) & 63];
            input.entry = live;
            input.ntag = -(uint64_t)page;
            input.addend = (uintptr_t)ram - page;
            *live = (CPUTLBEntry){.addr_read = page, .addr_write = page, .addend = input.addend};
            if (variant == 1) live->addr_write |= TLB_NOTDIRTY;
            if (variant == 2) {live->addr_read |= TLB_FORCE_SLOW; live->addr_write |= TLB_FORCE_SLOW;}
            if (variant == 3) {live->addr_read |= TLB_INVALID_MASK; live->addr_write |= TLB_INVALID_MASK;}
            if (variant >= 2 && (load || variant == 4 || variant == 5))
                input.ntag = -(uint64_t)(page ^ 0x1000u);
            if (variant == 5) live->addr_read = live->addr_write = page ^ 0x40000u;
            if (variant == 6) live->addend += 4096;
            bool bounds = i % 4096 + (1u << size) <= 4096 && !(input.addr & ((1u << align) - 1));
            bool shared = !invalidate && input.ntag == -(uint64_t)page && bounds &&
                          (load || live->addr_write == page);
            bool hit = shared || (rewalk && bounds && (load ? live->addr_read : live->addr_write) == page);
            uintptr_t addend = shared && load ? input.addend : live->addend;
            uintptr_t actual = probe(&input);
            if (actual != (hit ? addend + input.addr : UINTPTR_MAX)) {
                fprintf(stderr, "load=%u size=%u align=%u rewalk=%u invalidate=%u page=%x offset=%u variant=%u actual=%" PRIxPTR " expected=%" PRIxPTR "\n",
                        load, size, align, rewalk, invalidate, page, i % 4096, variant,
                        actual, hit ? addend + input.addr : UINTPTR_MAX);
                abort();
            }
            ++cases;
        }
    }
    printf("TLB memo emitted guards and rewalks: %u page/wrap/alignment/width/load/store/dirty/slow/invalid/stale-page/addend cases PASS\n", cases);
    return 0;
}
