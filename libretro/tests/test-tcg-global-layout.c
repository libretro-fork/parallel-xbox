/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-internal.h"
#include "qemu/interval-tree.h"
G_NORETURN void g_assertion_message_expr(const char *domain, const char *file,
                                       int line, const char *func, const char *expr)
{
    (void)domain;
    fprintf(stderr, "%s:%d: %s: %s\n", file, line, func, expr ? expr : "unreachable");
    abort();
}
#ifdef _WIN32
unsigned long tls_key_tcg_ctx;
#else
__thread TCGContext *tcg_ctx;
#endif
#undef tcg_debug_assert
#define tcg_debug_assert(x) assert(x)
#undef g_assert_not_reached
#define g_assert_not_reached() abort()
static void tcg_raise_tb_overflow(TCGContext *s) { (void)s; abort(); }
static TCGContext *refill_owner;
static int refill_size;
static unsigned refill_calls;
static unsigned char refill_buffer[128] QEMU_ALIGNED(16);
void *tcg_malloc_internal(TCGContext *s, int size)
{
    assert(s == refill_owner && size == refill_size);
    ++refill_calls;
    return refill_buffer;
}
static unsigned epilogue_calls;
const TCGOpDef tcg_op_defs[NB_OPS] = {
    [INDEX_op_br] = { .flags = TCG_OPF_BB_END },
};
void gen_bb_epilogue(void)
{
    ++epilogue_calls;
    /* Real epilogues may emit operations; these must precede the terminator. */
    tcg_emit_op(INDEX_op_mov, 2);
}
static char count_events[16];
static unsigned count_event_pos, count_lookups;
static int expected_count;
static TCGLabel exit_label, quiet_label;
static const TranslationBlock *expected_tb;
static void count_event(char event)
{
    assert(count_event_pos + 1 < sizeof(count_events));
    count_events[count_event_pos++] = event;
    count_events[count_event_pos] = 0;
}
static TCGTemp *fixture_count_constant(TCGType type, int64_t val)
{
    assert(type == TCG_TYPE_I32 && val == expected_count);
    ++count_lookups;
    count_event('C');
    return &tcg_ctx->temps[0];
}
void gen_set_label(TCGLabel *label)
{
    assert(label == &exit_label || label == &quiet_label);
    count_event(label == &exit_label ? 'X' : 'Q');
}
void tcg_gen_exit_tb(const TranslationBlock *tb, unsigned idx)
{
    assert(tb == expected_tb && idx == TB_EXIT_REQUESTED);
    count_event('E');
}
static unsigned constant_tables;
gpointer g_malloc0(gsize size)
{
    void *p = calloc(1, size);
    assert(p);
    ++constant_tables;
    return p;
}
/* Record only the host-specific capability/expansion boundary. The operand
 * decoding and direct IR emission below use the production implementation. */
static bool vector_cmpsel;
static bool vector_condition;
static bool vector_immediate;
static int vector_capability;
static unsigned vector_queries, vector_expansions, vector_arity;
static TCGOpcode vector_expected_op;
static TCGType vector_expected_type;
static unsigned vector_expected_vece;
static TCGArg vector_expected_args[6];
static int fixture_vec_capability(TCGOpcode opc, TCGType type, unsigned vece)
{
    assert(type == vector_expected_type && vece == vector_expected_vece);
    ++vector_queries;
    if (vector_cmpsel && opc == INDEX_op_cmp_vec) {
        assert(vector_capability == 0);
        return 1;
    }
    assert(opc == vector_expected_op);
    return vector_capability;
}
static void fixture_vec_expand(TCGOpcode opc, TCGType type, unsigned vece, ...)
{
    assert(opc == vector_expected_op && type == vector_expected_type);
    assert(vece == vector_expected_vece);
    ++vector_expansions;
    va_list ap;
    va_start(ap, vece);
    for (unsigned i = 0; i < vector_arity; ++i) {
        if (vector_condition && i == vector_arity - 1) {
            assert(va_arg(ap, int) == (int)vector_expected_args[i]);
        } else if (vector_immediate && i == 2) {
            assert(va_arg(ap, int64_t) == (int64_t)vector_expected_args[i]);
        } else {
            assert(va_arg(ap, TCGArg) == vector_expected_args[i]);
        }
    }
    va_end(ap);
}
static bool fixture_has_tst;
static TCGContext *cmp_owner;
static unsigned cmp_allocations, cmp_frees;
static TCGTemp *fixture_cmp_temp_new(TCGType type, TCGTempKind kind)
{
    assert(kind == TEMP_EBB);
    ++cmp_allocations;
    cmp_owner->temps[100].base_type = type;
    return &cmp_owner->temps[100];
}
static TCGTemp *fixture_cmp_zero(TCGType type, int64_t value)
{
    assert(value == 0 && cmp_allocations == 1);
    cmp_owner->temps[101].base_type = type;
    return &cmp_owner->temps[101];
}
static void fixture_cmp_temp_free(TCGTemp *temp)
{
    assert(temp == &cmp_owner->temps[100]);
    ++cmp_frees;
}
/* PRODUCTION */
/* COMPARISON */
static bool fixture_has_bitsel;
/* SELECTION */
/* COMPARE-SELECTION */
/* OPTIMIZER */
TCGv_env tcg_env;
static TCGContext *memory_owner;
static TCGOp *memory_op;
static TCGTemp *memory_value, *memory_copy;
static TCGType memory_type;
static intptr_t memory_start, memory_last;
static bool memory_constant;
static char memory_events[16];
static unsigned memory_event_count;
static void memory_event(char c)
{
    assert(memory_event_count + 1 < sizeof(memory_events));
    memory_events[memory_event_count++] = c;
    memory_events[memory_event_count] = 0;
}
static void memory_context(OptContext *ctx) { assert(ctx->tcg == memory_owner); }
static bool fixture_memory_finish_folding(OptContext *ctx, TCGOp *op)
{ memory_context(ctx); assert(op == memory_op); memory_event('Z'); return false; }
static TCGTemp *fixture_memory_find_mem_copy_for(OptContext *ctx, TCGType type, intptr_t ofs)
{ memory_context(ctx); assert(type == memory_type && ofs == memory_start); memory_event('F'); return memory_copy; }
static bool fixture_memory_tcg_opt_gen_mov(OptContext *ctx, TCGOp *op, TCGArg dst, TCGArg src)
{ memory_context(ctx); assert(op == memory_op && dst == temp_arg(memory_value) && src == temp_arg(memory_copy)); memory_event('M'); return false; }
static void fixture_memory_reset_ts(OptContext *ctx, TCGTemp *ts)
{ memory_context(ctx); assert(ts == memory_value); memory_event('R'); }
static void fixture_memory_record_mem_copy(OptContext *ctx, TCGType type, TCGTemp *ts, intptr_t start, intptr_t last)
{ memory_context(ctx); assert(type == memory_type && ts == memory_value && start == memory_start && last == memory_last); memory_event('C'); }
static void fixture_memory_remove_mem_copy_all(OptContext *ctx)
{ memory_context(ctx); memory_event('A'); }
static void fixture_memory_remove_mem_copy_in(OptContext *ctx, intptr_t start, intptr_t last)
{ memory_context(ctx); assert(start == memory_start && last == memory_last); memory_event('I'); }
static bool fixture_memory_ts_is_const(TCGTemp *ts)
{ assert(ts == memory_value); return memory_constant; }
static void fixture_memory_tcg_op_remove(TCGContext *ctx, TCGOp *op)
{ assert(ctx == memory_owner && op == memory_op); memory_event('D'); }
/* OPTIMIZER-MEMORY */

static void check_ops(TCGContext *owner, bool current)
{
    static unsigned char op_arena[4096] QEMU_ALIGNED(16);
    memset(op_arena, 0xa5, sizeof(op_arena));
    owner->pool_cur = (uintptr_t)op_arena;
    owner->pool_end = owner->pool_cur + sizeof(op_arena);
    owner->nb_ops = 0;
    QTAILQ_INIT(&owner->ops);
    QTAILQ_INIT(&owner->free_ops);
    owner->emit_before_op = NULL;
    TCGOp *a = tcg_op_alloc(owner, INDEX_op_mov, 1);
    assert(a->opc == INDEX_op_mov && a->nargs == 4);
    assert(!a->life && !a->param1 && !a->param2);
    QTAILQ_INSERT_TAIL(&owner->ops, a, link);
    TCGOp *b = tcg_op_insert_before(owner, a, INDEX_op_mov, TCG_TYPE_I32, 2);
    TCGOp *c = tcg_op_insert_after(owner, a, INDEX_op_mov, TCG_TYPE_I64, 2);
    assert(TCGOP_TYPE(b) == TCG_TYPE_I32 && TCGOP_TYPE(c) == TCG_TYPE_I64);
    assert(QTAILQ_FIRST(&owner->ops) == b && QTAILQ_NEXT(b, link) == a);
    assert(QTAILQ_NEXT(a, link) == c && !QTAILQ_NEXT(c, link));
    assert(owner->nb_ops == 3);
    QTAILQ_REMOVE(&owner->ops, b, link);
    QTAILQ_INSERT_TAIL(&owner->free_ops, b, link);
    --owner->nb_ops;
    uintptr_t cursor = owner->pool_cur;
    /* Skip an undersized free node; reuse it for the next smaller request. */
    TCGOp *large = tcg_op_alloc(owner, INDEX_op_mov, 7);
    assert(large != b && large->nargs == 7 && owner->pool_cur > cursor);
    cursor = owner->pool_cur;
    TCGOp *reused = tcg_op_alloc(owner, INDEX_op_mov, 2);
    assert(reused == b && reused->nargs == 4 && owner->pool_cur == cursor);
    assert(!reused->life && !reused->param1 && !reused->param2);
    assert(QTAILQ_EMPTY(&owner->free_ops) && owner->nb_ops == 4);
    if (current) {
        TCGOp *tail = tcg_emit_op(INDEX_op_mov, 2);
        assert(QTAILQ_NEXT(c, link) == tail && !QTAILQ_NEXT(tail, link));
        owner->emit_before_op = a;
        TCGOp *before = tcg_emit_op(INDEX_op_mov, 2);
        assert(QTAILQ_FIRST(&owner->ops) == before && QTAILQ_NEXT(before, link) == a);
        owner->emit_before_op = NULL;
        unsigned calls = epilogue_calls;
        TCGOp *end = tcg_emit_op(INDEX_op_br, 1);
        TCGOp *epilogue = QTAILQ_NEXT(tail, link);
        assert(epilogue_calls == calls + 1 && epilogue->opc == INDEX_op_mov);
        assert(QTAILQ_NEXT(epilogue, link) == end && !QTAILQ_NEXT(end, link));
        assert(owner->nb_ops == 8);
    }
}

static void check_vector_operands(TCGContext *owner, TCGContext *other)
{
    static unsigned char arena[8192] QEMU_ALIGNED(16);
    unsigned cases = 0;
    /* Same offsets in a different context must resolve to that context. */
    TCGv_vec handle = (TCGv_vec)((uintptr_t)&owner->temps[5] - (uintptr_t)owner);
    assert(tcgv_vec_temp_context(other, handle) == &other->temps[5]);
    assert(tcgv_vec_temp(handle) == &owner->temps[5]);
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type) {
        for (unsigned vece = 0; vece < 4; ++vece) {
            for (unsigned wider = 0; wider < 2; ++wider) {
                for (unsigned alias = 0; alias < 3; ++alias) {
                    owner->pool_cur = (uintptr_t)arena;
                    owner->pool_end = owner->pool_cur + sizeof(arena);
                    owner->nb_ops = 0;
                    owner->emit_before_op = NULL;
                    QTAILQ_INIT(&owner->ops);
                    QTAILQ_INIT(&owner->free_ops);
                    unsigned ri = 5, ai = alias == 1 ? ri : 19;
                    unsigned bi = alias == 2 ? ri : 37;
                    owner->temps[ri].base_type = type;
                    owner->temps[ai].base_type = ai == ri ? type :
                        (wider ? TCG_TYPE_V256 : type);
                    owner->temps[bi].base_type = bi == ri ? type :
                        (wider ? TCG_TYPE_V256 : type);
                    TCGv_vec rv = (TCGv_vec)((uintptr_t)&owner->temps[ri] - (uintptr_t)owner);
                    TCGv_vec av = (TCGv_vec)((uintptr_t)&owner->temps[ai] - (uintptr_t)owner);
                    TCGv_vec bv = (TCGv_vec)((uintptr_t)&owner->temps[bi] - (uintptr_t)owner);
                    vec_gen_op2(INDEX_op_mov_vec, vece, rv, av);
                    vec_gen_op3(INDEX_op_xor_vec, vece, rv, av, bv);
                    TCGOp *op = QTAILQ_FIRST(&owner->ops);
                    assert(owner->nb_ops == 2 && op->opc == INDEX_op_mov_vec);
                    assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == vece);
                    assert(op->args[0] == temp_arg(&owner->temps[ri]));
                    assert(op->args[1] == temp_arg(&owner->temps[ai]));
                    op = QTAILQ_NEXT(op, link);
                    assert(op && op->opc == INDEX_op_xor_vec && !QTAILQ_NEXT(op, link));
                    assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == vece);
                    assert(op->args[0] == temp_arg(&owner->temps[ri]));
                    assert(op->args[1] == temp_arg(&owner->temps[ai]));
                    assert(op->args[2] == temp_arg(&owner->temps[bi]));
                    cases += 2;
                    vector_expected_type = type;
                    vector_expected_vece = vece;
                    vector_expected_args[0] = temp_arg(&owner->temps[ri]);
                    vector_expected_args[1] = temp_arg(&owner->temps[ai]);
                    vector_expected_args[2] = temp_arg(&owner->temps[bi]);
                    for (unsigned arity = 2; arity <= 3; ++arity) {
                        vector_arity = arity;
                        vector_expected_op = arity == 2 ? INDEX_op_neg_vec : INDEX_op_add_vec;
                        for (int cap = -1; cap <= 1; ++cap) {
                            vector_capability = cap;
                            vector_queries = vector_expansions = 0;
                            owner->pool_cur = (uintptr_t)arena;
                            owner->nb_ops = 0;
                            QTAILQ_INIT(&owner->ops);
                            bool ok = arity == 2 ? do_op2(vece, rv, av, vector_expected_op) :
                                do_op3(vece, rv, av, bv, vector_expected_op);
                            assert(ok == (cap != 0) && vector_queries == 1);
                            assert(vector_expansions == (unsigned)(cap < 0));
                            assert(owner->nb_ops == (cap > 0));
                            op = QTAILQ_FIRST(&owner->ops);
                            if (cap > 0) {
                                assert(op && !QTAILQ_NEXT(op, link));
                                assert(op->opc == vector_expected_op);
                                assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == vece);
                                for (unsigned j = 0; j < arity; ++j) {
                                    assert(op->args[j] == vector_expected_args[j]);
                                }
                            } else {
                                assert(!op);
                            }
                        }
                    }
                }
            }
        }
    }
    puts("TCG optional vector operands: 432 direct/expanded/unsupported cases PASS");
    printf("TCG vector operands: %u emission cases, widths, aliasing and owners PASS\n", cases);
}

static void check_vector_shifts(TCGContext *owner)
{
    static unsigned char arena[4096] QEMU_ALIGNED(16);
    unsigned cases = 0;
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type) {
        for (unsigned vece = 0; vece < 4; ++vece) {
            for (unsigned alias = 0; alias < 2; ++alias) {
                TCGv_vec r = (TCGv_vec)((uintptr_t)&owner->temps[5] - (uintptr_t)owner);
                TCGv_vec a = alias ? r :
                    (TCGv_vec)((uintptr_t)&owner->temps[19] - (uintptr_t)owner);
                TCGv_i32 count = (TCGv_i32)((uintptr_t)&owner->temps[37] - (uintptr_t)owner);
                owner->temps[5].base_type = owner->temps[19].base_type = type;
                owner->temps[37].base_type = TCG_TYPE_I32;
                vector_expected_type = type;
                vector_expected_vece = vece;
                vector_arity = 3;
                vector_expected_args[0] = temp_arg(&owner->temps[5]);
                vector_expected_args[1] = temp_arg(&owner->temps[alias ? 5 : 19]);
                for (unsigned kind = 0; kind < 5; ++kind) {
                    static const TCGOpcode ops[] = { INDEX_op_shli_vec, INDEX_op_shri_vec,
                        INDEX_op_sari_vec, INDEX_op_shls_vec, INDEX_op_shrs_vec };
                    vector_expected_op = ops[kind];
                    vector_immediate = kind < 3;
                    for (unsigned amount = 0; amount < (vector_immediate ? 3u : 1u); ++amount) {
                        int64_t shift = amount == 0 ? 0 : amount == 1 ? 1 : (8 << vece) - 1;
                        vector_expected_args[2] = vector_immediate ? (TCGArg)shift :
                            temp_arg(&owner->temps[37]);
                        for (int cap = -1; cap <= 1; cap += 2) {
                            owner->pool_cur = (uintptr_t)arena;
                            owner->pool_end = owner->pool_cur + sizeof(arena);
                            owner->nb_ops = 0;
                            owner->emit_before_op = NULL;
                            QTAILQ_INIT(&owner->ops);
                            QTAILQ_INIT(&owner->free_ops);
                            vector_capability = cap;
                            vector_queries = vector_expansions = 0;
                            bool zero = vector_immediate && shift == 0;
                            if (vector_immediate) {
                                do_shifti(ops[kind], vece, r, a, shift);
                            } else {
                                do_shifts(vece, r, a, count, ops[kind]);
                            }
                            assert(vector_queries == (unsigned)!zero);
                            assert(vector_expansions == (unsigned)(!zero && cap < 0));
                            bool emitted = zero ? !alias : cap > 0;
                            assert(owner->nb_ops == (int)emitted);
                            TCGOp *op = QTAILQ_FIRST(&owner->ops);
                            if (emitted) {
                                assert(op && !QTAILQ_NEXT(op, link));
                                assert(op->opc == (zero ? INDEX_op_mov_vec : ops[kind]));
                                assert(TCGOP_TYPE(op) == type);
                                assert(TCGOP_VECE(op) == (zero ? 0 : vece));
                                for (unsigned j = 0; j < (zero ? 2u : 3u); ++j) {
                                    assert(op->args[j] == vector_expected_args[j]);
                                }
                            } else {
                                assert(!op);
                            }
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    vector_immediate = false;
    printf("TCG vector shifts: %u immediate/scalar, zero, edge and alias cases PASS\n", cases);
}

static void check_vector_comparison(TCGContext *owner)
{
    static unsigned char arena[4096] QEMU_ALIGNED(16);
    static const TCGCond conds[] = { TCG_COND_EQ, TCG_COND_NE,
        TCG_COND_LT, TCG_COND_LE, TCG_COND_GT, TCG_COND_GE,
        TCG_COND_LTU, TCG_COND_LEU, TCG_COND_GTU, TCG_COND_GEU,
        TCG_COND_TSTEQ, TCG_COND_TSTNE };
    cmp_owner = owner;
    vector_condition = true;
    vector_arity = 4;
    vector_expected_op = INDEX_op_cmp_vec;
    unsigned cases = 0;
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type)
    for (unsigned vece = 0; vece < 4; ++vece)
    for (unsigned ci = 0; ci < ARRAY_SIZE(conds); ++ci)
    for (unsigned host = 0; host < 2; ++host)
    for (unsigned alias = 0; alias < 3; ++alias)
    for (int cap = -1; cap <= 1; cap += 2) {
        unsigned ai = alias == 1 ? 5 : 19, bi = alias == 2 ? 5 : 37;
        TCGv_vec r = (TCGv_vec)((uintptr_t)&owner->temps[5] - (uintptr_t)owner);
        TCGv_vec a = (TCGv_vec)((uintptr_t)&owner->temps[ai] - (uintptr_t)owner);
        TCGv_vec b = (TCGv_vec)((uintptr_t)&owner->temps[bi] - (uintptr_t)owner);
        owner->temps[5].base_type = type;
        owner->temps[ai].base_type = ai == 5 ? type : TCG_TYPE_V256;
        owner->temps[bi].base_type = bi == 5 ? type : TCG_TYPE_V256;
        fixture_has_tst = host;
        bool lowered = !host && (ci >= 10);
        TCGCond cond = conds[ci];
        TCGCond output_cond = lowered ? (ci == 10 ? TCG_COND_EQ : TCG_COND_NE) : cond;
        vector_expected_type = type;
        vector_expected_vece = vece;
        vector_expected_args[0] = temp_arg(&owner->temps[5]);
        vector_expected_args[1] = temp_arg(&owner->temps[lowered ? 100 : ai]);
        vector_expected_args[2] = temp_arg(&owner->temps[lowered ? 101 : bi]);
        vector_expected_args[3] = output_cond;
        vector_capability = cap;
        vector_queries = vector_expansions = cmp_allocations = cmp_frees = 0;
        owner->pool_cur = (uintptr_t)arena;
        owner->pool_end = owner->pool_cur + sizeof(arena);
        owner->nb_ops = 0;
        owner->emit_before_op = NULL;
        QTAILQ_INIT(&owner->ops);
        QTAILQ_INIT(&owner->free_ops);
        tcg_gen_cmp_vec(cond, vece, r, a, b);
        assert(vector_queries == 1 && vector_expansions == (unsigned)(cap < 0));
        assert(cmp_allocations == (unsigned)lowered && cmp_frees == cmp_allocations);
        assert(owner->nb_ops == (int)lowered + (cap > 0));
        TCGOp *op = QTAILQ_FIRST(&owner->ops);
        if (lowered) {
            assert(op && op->opc == INDEX_op_and_vec);
            assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == 0);
            assert(op->args[0] == temp_arg(&owner->temps[100]));
            assert(op->args[1] == temp_arg(&owner->temps[ai]));
            assert(op->args[2] == temp_arg(&owner->temps[bi]));
            op = QTAILQ_NEXT(op, link);
        }
        if (cap > 0) {
            assert(op && op->opc == INDEX_op_cmp_vec);
            assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == vece);
            for (unsigned j = 0; j < 4; ++j) assert(op->args[j] == vector_expected_args[j]);
            op = QTAILQ_NEXT(op, link);
        }
        assert(!op);
        ++cases;
    }
    vector_condition = false;
    printf("TCG vector comparisons: %u conditions, test-lowering, host and alias cases PASS\n", cases);
}

static void check_rollback(TCGContext *owner)
{
    static unsigned char arena[4096] QEMU_ALIGNED(16);
    static const TCGOpcode codes[] = {
        INDEX_op_mov, INDEX_op_br, INDEX_op_brcond, INDEX_op_brcond2_i32, INDEX_op_br
    };
    static const unsigned label_indices[] = {0, 0, 3, 5, 0};
    TCGLabel label = {0};
    TCGLabelUse uses[4];
    TCGOp *ops[5], *op;
    QSIMPLEQ_INIT(&label.branches);
    QTAILQ_INIT(&owner->ops);
    QTAILQ_INIT(&owner->free_ops);
    owner->pool_cur = (uintptr_t)arena;
    owner->pool_end = owner->pool_cur + sizeof(arena);
    owner->nb_ops = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(ops); ++i) {
        op = ops[i] = tcg_op_alloc(owner, codes[i], label_indices[i] + 1);
        QTAILQ_INSERT_TAIL(&owner->ops, op, link);
        if (i) {
            op->args[label_indices[i]] = label_arg(&label);
            uses[i - 1].op = op;
            QSIMPLEQ_INSERT_TAIL(&label.branches, &uses[i - 1], next);
        }
    }
    /* Preserve a branch while discarding all three supported branch forms. */
    tcg_remove_ops_after(ops[1]);
    assert(owner->nb_ops == 2 && QTAILQ_LAST(&owner->ops) == ops[1]);
    assert(QSIMPLEQ_FIRST(&label.branches) == &uses[0]);
    assert(!QSIMPLEQ_NEXT(&uses[0], next));
    assert(label.branches.sqh_last == &uses[0].next.sqe_next);
    unsigned count = 0;
    QTAILQ_FOREACH(op, &owner->free_ops, link) {
        assert(count < 3 && op == ops[4 - count]);
        ++count;
    }
    assert(count == 3);
    tcg_remove_ops_after(ops[1]); /* Already at the saved tail. */
    assert(owner->nb_ops == 2 && QTAILQ_FIRST(&owner->free_ops) == ops[4]);
    tcg_remove_ops_after(ops[0]);
    assert(owner->nb_ops == 1 && QSIMPLEQ_EMPTY(&label.branches));
    assert(label.branches.sqh_last == &label.branches.sqh_first);
    tcg_remove_ops_after(NULL); /* Discard the entire speculative sequence. */
    assert(!owner->nb_ops && QTAILQ_EMPTY(&owner->ops));
    assert(QTAILQ_LAST(&owner->ops) == NULL);
    tcg_remove_ops_after(NULL); /* Empty input remains a no-op. */
    count = 0;
    QTAILQ_FOREACH(op, &owner->free_ops, link) {
        assert(count < ARRAY_SIZE(ops) && op == ops[4 - count]);
        ++count;
    }
    assert(count == ARRAY_SIZE(ops));
    uintptr_t cursor = owner->pool_cur;
    assert(tcg_op_alloc(owner, INDEX_op_mov, 1) == ops[4]);
    assert(owner->nb_ops == 1 && owner->pool_cur == cursor);
}

static void check_count_operands(TCGContext *owner)
{
    static const int counts[] = {0, 1, 255, 4000, INT_MAX};
    static unsigned char arena[512] QEMU_ALIGNED(16), tb_identity;
    owner->pool_cur = (uintptr_t)arena;
    owner->pool_end = owner->pool_cur + sizeof(arena);
    QTAILQ_INIT(&owner->free_ops);
    TCGOp *budget = tcg_op_alloc(owner, INDEX_op_sub, 3);
    TCGOp *quiet = tcg_op_alloc(owner, INDEX_op_sub, 3);
    expected_tb = (const void *)&tb_identity; /* The hook forwards, never dereferences. */
    unsigned cases = 0;
    for (unsigned n = 0; n < ARRAY_SIZE(counts); ++n) {
        for (unsigned mask = 0; mask < 8; ++mask) {
            bool has_budget = mask & 1, has_exit = mask & 2, has_quiet = mask & 4;
            for (unsigned i = 0; i < 4; ++i) {
                budget->args[i] = quiet->args[i] = (TCGArg)0xa5a5;
            }
            owner->exitreq_label = has_exit ? &exit_label : NULL;
            owner->lr_quiet_label = has_quiet ? &quiet_label : NULL;
            owner->lr_quiet_insn = quiet;
            expected_count = counts[n];
            count_lookups = count_event_pos = 0;
            count_events[0] = 0;
            gen_tb_end(owner, expected_tb, 0, has_budget ? budget : NULL, counts[n]);
            assert(count_lookups == (unsigned)(has_budget || has_quiet));
            TCGArg operand = temp_arg(&owner->temps[0]);
            assert(budget->args[2] == (has_budget ? operand : (TCGArg)0xa5a5));
            assert(quiet->args[2] == (has_quiet ? operand : (TCGArg)0xa5a5));
            for (unsigned i = 0; i < 4; ++i) {
                if (i != 2) assert(budget->args[i] == 0xa5a5 && quiet->args[i] == 0xa5a5);
            }
            char expected[16] = "";
            if (has_budget) strcat(expected, "C");
            if (has_exit) strcat(expected, "XE");
            if (has_quiet) {
                if (!has_budget) strcat(expected, "C");
                strcat(expected, "QE");
            }
            assert(!strcmp(count_events, expected));
            assert(owner->exitreq_label == (has_exit ? &exit_label : NULL));
            assert(owner->lr_quiet_label == NULL);
            ++cases;
        }
    }
    printf("TCG count operands: %u budget/exit/idle combinations, values and lazy allocation order PASS\n", cases);
}

static void check_temp_free(TCGContext *current, TCGContext *other)
{
    static const TCGTempKind kinds[] = {TEMP_EBB, TEMP_TB, TEMP_CONST};
    static const unsigned indices[] = {0, 31, 32, 63, 64, TCG_MAX_TEMPS - 2};
    static const TCGType types[] = {
        TCG_TYPE_I32, TCG_TYPE_I64, TCG_TYPE_I128, TCG_TYPE_PTR,
        TCG_TYPE_V64, TCG_TYPE_I32, TCG_TYPE_I32
    };
    unsigned cases = 0;
    for (unsigned mode = 0; mode < ARRAY_SIZE(types); ++mode) {
        TCGContext *owner = mode == 6 ? other : current;
        TCGContext *untouched = mode == 6 ? current : other;
        for (unsigned k = 0; k < ARRAY_SIZE(kinds); ++k) {
            for (unsigned i = 0; i < ARRAY_SIZE(indices); ++i) {
                memset(owner->free_temps, 0, sizeof(owner->free_temps));
                memset(untouched->free_temps, 0, sizeof(untouched->free_temps));
                TCGTemp *temp = &owner->temps[indices[i]];
                memset(temp, 0, 2 * sizeof(*temp));
                temp->kind = kinds[k];
                temp->base_type = types[mode];
                temp->type = TCG_TYPE_I32;
                temp->temp_allocated = 1;
                temp->val = 0x12345678;
                temp[1].temp_subindex = 1;
                temp[1].temp_allocated = 1;
                temp[1].val = 0x7abcdef0;
                TCGTemp expected[2];
                memcpy(expected, temp, sizeof(expected));
                if (kinds[k] == TEMP_EBB) expected[0].temp_allocated = 0;
                uintptr_t handle = (uintptr_t)temp - (uintptr_t)owner;
                assert(tcgv_i32_temp_context(owner, (TCGv_i32)handle) == temp);
                switch (mode) {
                case 0: tcg_temp_free_i32((TCGv_i32)handle); break;
                case 1: tcg_temp_free_i64((TCGv_i64)handle); break;
                case 2: tcg_temp_free_i128((TCGv_i128)handle); break;
                case 3: tcg_temp_free_ptr((TCGv_ptr)handle); break;
                case 4: tcg_temp_free_vec((TCGv_vec)handle); break;
                case 5: tcg_temp_free_internal(temp); break;
                case 6: tcg_temp_free_context(owner, temp); break;
                }
                assert(!memcmp(expected, temp, sizeof(expected)));
                for (unsigned type = 0; type < TCG_TYPE_COUNT; ++type) {
                    for (unsigned bit = 0; bit < TCG_MAX_TEMPS; ++bit) {
                        bool set = kinds[k] == TEMP_EBB && type == types[mode] && bit == indices[i];
                        assert(!!test_bit(bit, owner->free_temps[type].l) == set);
                        assert(!test_bit(bit, untouched->free_temps[type].l));
                    }
                }
                ++cases;
            }
        }
    }
    printf("TCG free: %u typed/pointer/owner cases, ignored kinds, bitmaps and adjacent slots PASS\n", cases);
}

static unsigned new_slots(TCGType type)
{
    return type == TCG_TYPE_I64 ? 64 / TCG_TARGET_REG_BITS :
           type == TCG_TYPE_I128 ? 128 / TCG_TARGET_REG_BITS : 1;
}

static void reset_new_temps(TCGContext *owner)
{
    owner->nb_temps = owner->nb_globals = 0;
    memset(owner->temps, 0xa5, sizeof(owner->temps));
    memset(owner->free_temps, 0, sizeof(owner->free_temps));
}

static void check_new_temp(TCGContext *owner, TCGTemp *temp,
                           TCGType type, TCGTempKind kind)
{
    unsigned n = new_slots(type);
    assert(temp == owner->temps && owner->nb_temps == (int)n);
    for (unsigned i = 0; i < n; ++i) {
        assert(temp[i].base_type == type && temp[i].kind == kind);
        assert(temp[i].type == (n == 1 ? type : TCG_TYPE_REG));
        assert(temp[i].temp_allocated && temp[i].temp_subindex == i);
        assert(!temp[i].val && !temp[i].state_ptr && !temp[i].mem_base);
    }
    const unsigned char *guard = (const void *)&owner->temps[n];
    for (unsigned i = 0; i < sizeof(TCGTemp); ++i) assert(guard[i] == 0xa5);
}

static uintptr_t new_typed(unsigned mode)
{
    switch (mode) {
    case 0: return (uintptr_t)tcg_temp_new_i32();
    case 1: return (uintptr_t)tcg_temp_ebb_new_i32();
    case 2: return (uintptr_t)tcg_temp_new_i64();
    case 3: return (uintptr_t)tcg_temp_ebb_new_i64();
    case 4: return (uintptr_t)tcg_temp_new_ptr();
    case 5: return (uintptr_t)tcg_temp_ebb_new_ptr();
    case 6: return (uintptr_t)tcg_temp_new_i128();
    case 7: return (uintptr_t)tcg_temp_ebb_new_i128();
    case 8: return (uintptr_t)tcg_temp_new_f32();
    case 9: return (uintptr_t)tcg_temp_new_f64();
    default: return (uintptr_t)tcg_temp_new_vec(TCG_TYPE_V64 + mode - 10);
    }
}

static void check_temp_new(TCGContext *current, TCGContext *other)
{
    static const TCGType types[] = {
        TCG_TYPE_I32, TCG_TYPE_I64, TCG_TYPE_I128, TCG_TYPE_F32,
        TCG_TYPE_F64, TCG_TYPE_V64, TCG_TYPE_V128, TCG_TYPE_V256
    };
    unsigned cases = 0;
    for (unsigned api = 0; api < 3; ++api) {
        TCGContext *owner = api == 2 ? other : current;
        for (unsigned i = 0; i < ARRAY_SIZE(types); ++i) {
            for (unsigned k = 0; k < 2; ++k) {
                TCGTempKind kind = k ? TEMP_TB : TEMP_EBB;
                TCGType type = types[i];
                reset_new_temps(owner);
                TCGTemp *t = api == 0 ? tcg_temp_new_internal(type, kind) :
                                       tcg_temp_new_context(owner, type, kind);
                check_new_temp(owner, t, type, kind);
                t->val = 0x12345678;
                tcg_temp_free_context(owner, t);
                TCGTemp *again = api == 0 ? tcg_temp_new_internal(type, kind) :
                                           tcg_temp_new_context(owner, type, kind);
                assert(again == (kind == TEMP_EBB ? t : t + new_slots(type)));
                assert(owner->nb_temps == (int)(new_slots(type) * (kind == TEMP_EBB ? 1 : 2)));
                assert(t->val == 0x12345678 && again->temp_allocated);
                ++cases;
            }
        }
    }
    static const TCGType typed_types[] = {
        TCG_TYPE_I32, TCG_TYPE_I32, TCG_TYPE_I64, TCG_TYPE_I64,
        TCG_TYPE_PTR, TCG_TYPE_PTR, TCG_TYPE_I128, TCG_TYPE_I128,
        TCG_TYPE_F32, TCG_TYPE_F64, TCG_TYPE_V64, TCG_TYPE_V128, TCG_TYPE_V256
    };
    for (unsigned mode = 0; mode < ARRAY_SIZE(typed_types); ++mode) {
        TCGType type = typed_types[mode];
        TCGTempKind kind = mode >= 8 || (mode & 1) ? TEMP_EBB : TEMP_TB;
        reset_new_temps(current);
        uintptr_t handle = new_typed(mode);
        TCGTemp *t = tcgv_i32_temp_context(current, (TCGv_i32)handle);
        assert(handle && handle == (uintptr_t)temp_tcgv_i32_context(current, t));
        check_new_temp(current, t, type, kind);
        tcg_temp_free_context(current, t);
        uintptr_t again = new_typed(mode);
        assert((again == handle) == (kind == TEMP_EBB));
        assert(current->nb_temps == (int)(new_slots(type) * (kind == TEMP_EBB ? 1 : 2)));
        ++cases;
    }
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type) {
        reset_new_temps(current);
        TCGv_vec source = tcg_temp_new_vec(type);
        TCGv_vec match = tcg_temp_new_vec_matching(source);
        assert(source != match && current->nb_temps == 2);
        TCGTemp *t = tcgv_vec_temp(match);
        assert(t->base_type == type && t->kind == TEMP_EBB && t->temp_allocated);
        tcg_temp_free_vec(match);
        assert(tcg_temp_new_vec_matching(source) == match && current->nb_temps == 2);
        assert(tcgv_vec_temp(source)->temp_allocated);
        ++cases;
    }
    printf("TCG new: %u owner/typed/matching cases, fresh/reused slots, lifetimes and split layouts PASS\n", cases);
}

static void check_constant_value(TCGTemp *t, TCGType type, int64_t val)
{
    assert(t->base_type == type && t->kind == TEMP_CONST && t->temp_allocated);
    if (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64) {
        assert(t[0].type == TCG_TYPE_I32 && t[1].type == TCG_TYPE_I32);
        assert(t[1].base_type == type && t[1].kind == TEMP_CONST);
        assert(t[1].temp_allocated && t[1].temp_subindex == 1);
        assert(t[HOST_BIG_ENDIAN].val == val);
        assert(t[!HOST_BIG_ENDIAN].val == (val >> 32));
    } else {
        assert(t->type == type && t->val == val);
    }
}

static void check_temp_constants(TCGContext *current, TCGContext *other)
{
    static const int64_t values[] = {0, 1, -1, INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX};
    unsigned tables = constant_tables, cases = 0;
    assert(!current->const_slots && !other->const_slots);
    for (unsigned api = 0; api < 3; ++api) {
        TCGContext *owner = api == 2 ? other : current;
        TCGContext *untouched = api == 2 ? current : other;
        int untouched_temps = untouched->nb_temps;
        for (TCGType type = TCG_TYPE_I32; type <= TCG_TYPE_I64; ++type) {
            reset_new_temps(owner);
            ++owner->const_gen;
            for (unsigned i = 0; i < ARRAY_SIZE(values); ++i) {
                TCGTemp *t = api == 0 ? tcg_constant_internal(type, values[i]) :
                                       tcg_constant_context(owner, type, values[i]);
                check_constant_value(t, type, values[i]);
                int used = owner->nb_temps;
                TCGTemp *hit = api == 0 ? tcg_constant_internal(type, values[i]) :
                                         tcg_constant_context(owner, type, values[i]);
                assert(hit == t && owner->nb_temps == used);
                ++cases;
            }
            int used = owner->nb_temps;
            ++owner->const_gen;
            TCGTemp *fresh = api == 0 ? tcg_constant_internal(type, 0) :
                                       tcg_constant_context(owner, type, 0);
            assert(fresh == &owner->temps[used]);
            assert(owner->nb_temps == used + (int)new_slots(type));
        }
        assert(untouched->nb_temps == untouched_temps);
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        reset_new_temps(current);
        ++current->const_gen;
        for (unsigned i = 0; i < ARRAY_SIZE(values); ++i) {
            int64_t val = mode == 0 ? (int32_t)values[i] :
                          mode == 1 ? values[i] : (intptr_t)values[i];
            uintptr_t handle = mode == 0 ? (uintptr_t)tcg_constant_i32(val) :
                               mode == 1 ? (uintptr_t)tcg_constant_i64(val) :
                                           (uintptr_t)tcg_constant_ptr_int(val);
            TCGType type = mode == 0 ? TCG_TYPE_I32 : mode == 1 ? TCG_TYPE_I64 : TCG_TYPE_PTR;
            TCGTemp *t = tcgv_i32_temp_context(current, (TCGv_i32)handle);
            assert(handle && (uintptr_t)temp_tcgv_i32_context(current, t) == handle);
            check_constant_value(t, type, val);
            assert(tcg_constant_internal(type, val) == t);
            ++cases;
        }
    }
    static const uint64_t replicated[] = {
        UINT64_C(0xefefefefefefefef), UINT64_C(0xcdefcdefcdefcdef),
        UINT64_C(0x89abcdef89abcdef), UINT64_C(0x0123456789abcdef)
    };
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type) {
        reset_new_temps(current);
        ++current->const_gen;
        TCGv_vec source = tcg_temp_new_vec(type);
        for (unsigned vece = 0; vece < ARRAY_SIZE(replicated); ++vece) {
            TCGv_vec handle = tcg_constant_vec(type, vece, INT64_C(0x0123456789abcdef));
            TCGTemp *t = tcgv_vec_temp(handle);
            check_constant_value(t, type, (int64_t)replicated[vece]);
            int used = current->nb_temps;
            assert(tcg_constant_vec_matching(source, vece, INT64_C(0x0123456789abcdef)) == handle);
            assert(current->nb_temps == used && tcgv_vec_temp(source)->temp_allocated);
            ++cases;
        }
    }
    /* Discover last-bucket colliders through real insertions, without copying
     * the hash implementation. Then force linear probing across table wrap. */
    int64_t collide[3];
    unsigned found = 0;
    TCGConstSlot *slots = current->const_slots + TCG_TYPE_I32 * TCG_CONST_SLOTS;
    for (int64_t val = 0; val < 16384 && found < ARRAY_SIZE(collide); ++val) {
        reset_new_temps(current);
        ++current->const_gen;
        tcg_constant_context(current, TCG_TYPE_I32, val);
        if (slots[TCG_CONST_SLOTS - 1].gen == current->const_gen) collide[found++] = val;
    }
    assert(found == ARRAY_SIZE(collide));
    reset_new_temps(current);
    ++current->const_gen;
    TCGTemp *entries[3];
    for (unsigned i = 0; i < ARRAY_SIZE(entries); ++i) {
        entries[i] = tcg_constant_context(current, TCG_TYPE_I32, collide[i]);
        unsigned slot = i ? i - 1 : TCG_CONST_SLOTS - 1;
        assert(slots[slot].ts == entries[i] && slots[slot].val == collide[i]);
        assert(current->nb_temps == (int)i + 1);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(entries); ++i) {
        assert(tcg_constant_context(current, TCG_TYPE_I32, collide[i]) == entries[i]);
        assert(current->nb_temps == 3);
    }
    assert(constant_tables == tables + 2); /* One lazy table per owner. */
    free(current->const_slots);
    free(other->const_slots);
    current->const_slots = other->const_slots = NULL;
    printf("TCG constants: %u value/type/owner/vector cases, hits, generation invalidation, lazy tables and collision wrap PASS\n", cases);
}

static void check_global(TCGTemp *temp, TCGTemp *base, intptr_t offset,
                         TCGType type, bool indirect)
{
    unsigned slots = TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64 ? 2 : 1;
    for (unsigned i = 0; i < slots; ++i) {
        TCGTemp *part = temp + i;
        assert(part->kind == TEMP_GLOBAL && part->base_type == type);
        assert(part->type == (slots == 2 ? TCG_TYPE_I32 : type));
        assert(part->mem_base == base && part->mem_offset == offset + 4 * (intptr_t)i);
        assert(part->mem_allocated && part->indirect_reg == indirect);
        assert(part->temp_subindex == i && !part->indirect_base);
        assert(!part->state && !part->state_ptr && !part->val);
    }
}

static void check_vector_bitsel(TCGContext *owner)
{
    static unsigned char arena[4096] QEMU_ALIGNED(16);
    unsigned cases = 0;
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type)
    for (unsigned host = 0; host < 2; ++host)
    for (unsigned alias = 0; alias < 4; ++alias)
    for (unsigned wider = 0; wider < 2; ++wider)
    for (unsigned vece = 0; vece < 4; ++vece) {
        reset_new_temps(owner);
        TCGv_vec r = tcg_temp_new_vec(type);
        TCGv_vec handles[3];
        for (unsigned j = 0; j < 3; ++j) {
            handles[j] = alias == j + 1 ? r :
                tcg_temp_new_vec(wider ? TCG_TYPE_V256 : type);
        }
        TCGArg ri = tcgv_vec_arg(r), ai = tcgv_vec_arg(handles[0]);
        TCGArg bi = tcgv_vec_arg(handles[1]), ci = tcgv_vec_arg(handles[2]);
        owner->pool_cur = (uintptr_t)arena;
        owner->pool_end = owner->pool_cur + sizeof(arena);
        owner->nb_ops = 0;
        owner->emit_before_op = NULL;
        QTAILQ_INIT(&owner->ops);
        QTAILQ_INIT(&owner->free_ops);
        fixture_has_bitsel = host;
        int used = owner->nb_temps;
        tcg_gen_bitsel_vec(vece, r, handles[0], handles[1], handles[2]);
        TCGOp *op = QTAILQ_FIRST(&owner->ops);
        assert(op && TCGOP_TYPE(op) == type && TCGOP_VECE(op) == 0);
        if (host) {
            assert(owner->nb_ops == 1 && !QTAILQ_NEXT(op, link));
            assert(op->opc == INDEX_op_bitsel_vec);
            assert(op->args[0] == ri && op->args[1] == ai);
            assert(op->args[2] == bi && op->args[3] == ci);
            assert(owner->nb_temps == used);
        } else {
            assert(owner->nb_ops == 3 && owner->nb_temps == used + 1);
            TCGArg ti = temp_arg(&owner->temps[used]);
            assert(op->opc == INDEX_op_and_vec);
            assert(op->args[0] == ti && op->args[1] == ai && op->args[2] == bi);
            op = QTAILQ_NEXT(op, link);
            assert(op && op->opc == INDEX_op_andc_vec);
            assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == 0);
            assert(op->args[0] == ri && op->args[1] == ci && op->args[2] == ai);
            op = QTAILQ_NEXT(op, link);
            assert(op && op->opc == INDEX_op_or_vec && !QTAILQ_NEXT(op, link));
            assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == 0);
            assert(op->args[0] == ri && op->args[1] == ri && op->args[2] == ti);
            assert(!owner->temps[used].temp_allocated);
            assert(test_bit(used, owner->free_temps[type].l));
        }
        ++cases;
    }
    printf("TCG vector bit-selection: %u native/fallback, width and alias cases PASS\n", cases);
}

static void check_vector_cmpsel(TCGContext *owner)
{
    static unsigned char arena[8192] QEMU_ALIGNED(16);
    static const TCGCond conds[] = { TCG_COND_EQ, TCG_COND_NE,
        TCG_COND_LT, TCG_COND_LE, TCG_COND_GT, TCG_COND_GE,
        TCG_COND_LTU, TCG_COND_LEU, TCG_COND_GTU, TCG_COND_GEU,
        TCG_COND_TSTEQ, TCG_COND_TSTNE };
    unsigned cases = 0;
    vector_cmpsel = vector_condition = true;
    fixture_has_tst = true; /* Test-condition lowering is covered separately. */
    vector_arity = 6;
    vector_expected_op = INDEX_op_cmpsel_vec;
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type)
    for (unsigned vece = 0; vece < 4; ++vece)
    for (unsigned ci = 0; ci < ARRAY_SIZE(conds); ++ci)
    for (unsigned host = 0; host < 2; ++host)
    for (unsigned alias = 0; alias < 5; ++alias)
    for (unsigned wider = 0; wider < 2; ++wider)
    for (int cap = -1; cap <= 1; ++cap) {
        reset_new_temps(owner);
        TCGv_vec r = tcg_temp_new_vec(type), h[4];
        vector_expected_args[0] = tcgv_vec_arg(r);
        for (unsigned j = 0; j < 4; ++j) {
            h[j] = alias == j + 1 ? r : tcg_temp_new_vec(wider ? TCG_TYPE_V256 : type);
            vector_expected_args[j + 1] = tcgv_vec_arg(h[j]);
        }
        vector_expected_args[5] = conds[ci];
        vector_expected_type = type;
        vector_expected_vece = vece;
        vector_capability = cap;
        vector_queries = vector_expansions = 0;
        fixture_has_bitsel = host;
        owner->pool_cur = (uintptr_t)arena;
        owner->pool_end = owner->pool_cur + sizeof(arena);
        owner->nb_ops = 0;
        owner->emit_before_op = NULL;
        QTAILQ_INIT(&owner->ops);
        QTAILQ_INIT(&owner->free_ops);
        int used = owner->nb_temps;
        tcg_gen_cmpsel_vec(conds[ci], vece, r, h[0], h[1], h[2], h[3]);
        assert(vector_queries == (cap == 0 ? 2u : 1u));
        assert(vector_expansions == (unsigned)(cap < 0));
        TCGOp *op = QTAILQ_FIRST(&owner->ops);
        if (cap > 0) {
            assert(owner->nb_ops == 1 && op && op->opc == INDEX_op_cmpsel_vec);
            assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == vece);
            for (unsigned j = 0; j < 6; ++j) assert(op->args[j] == vector_expected_args[j]);
            assert(!QTAILQ_NEXT(op, link) && owner->nb_temps == used);
        } else if (cap < 0) {
            assert(!op && !owner->nb_ops && owner->nb_temps == used);
        } else {
            TCGArg mask = temp_arg(&owner->temps[used]);
            assert(op && op->opc == INDEX_op_cmp_vec);
            assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == vece);
            assert(op->args[0] == mask && op->args[1] == vector_expected_args[1]);
            assert(op->args[2] == vector_expected_args[2] && op->args[3] == (TCGArg)conds[ci]);
            op = QTAILQ_NEXT(op, link);
            TCGArg ri = vector_expected_args[0], yes = vector_expected_args[3];
            TCGArg no = vector_expected_args[4];
            assert(op && TCGOP_TYPE(op) == type && TCGOP_VECE(op) == 0);
            if (host) {
                assert(owner->nb_ops == 2 && owner->nb_temps == used + 1);
                assert(op->opc == INDEX_op_bitsel_vec);
                assert(op->args[0] == ri && op->args[1] == mask);
                assert(op->args[2] == yes && op->args[3] == no);
            } else {
                assert(owner->nb_ops == 4 && owner->nb_temps == used + 2);
                TCGArg t = temp_arg(&owner->temps[used + 1]);
                assert(op->opc == INDEX_op_and_vec && op->args[0] == t);
                assert(op->args[1] == mask && op->args[2] == yes);
                op = QTAILQ_NEXT(op, link);
                assert(op && op->opc == INDEX_op_andc_vec);
                assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == 0);
                assert(op->args[0] == ri && op->args[1] == no && op->args[2] == mask);
                op = QTAILQ_NEXT(op, link);
                assert(op && op->opc == INDEX_op_or_vec);
                assert(TCGOP_TYPE(op) == type && TCGOP_VECE(op) == 0);
                assert(op->args[0] == ri && op->args[1] == ri && op->args[2] == t);
                assert(!owner->temps[used + 1].temp_allocated);
                assert(test_bit(used + 1, owner->free_temps[type].l));
            }
            assert(!QTAILQ_NEXT(op, link));
            assert(!owner->temps[used].temp_allocated);
            assert(test_bit(used, owner->free_temps[type].l));
        }
        ++cases;
    }
    vector_cmpsel = vector_condition = false;
    printf("TCG vector compare-selection: %u capability, fallback and alias cases PASS\n", cases);
}

static void check_vector_data(TCGContext *owner)
{
    static unsigned char arena[4096] QEMU_ALIGNED(16);
    static const tcg_target_long offsets[] = { INTPTR_MIN, -4096, -1, 0, 1, 4096, INTPTR_MAX };
    unsigned cases = 0;
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type)
    for (unsigned vece = 0; vece < 4; ++vece)
    for (unsigned oi = 0; oi < ARRAY_SIZE(offsets); ++oi)
    for (unsigned kind = 0; kind < 6; ++kind)
    for (TCGType low = TCG_TYPE_V64; low <= (kind == 5 ? type : TCG_TYPE_V64); ++low) {
        reset_new_temps(owner);
        TCGv_vec r = tcg_temp_new_vec(type);
        TCGv_i32 a32 = tcg_temp_new_i32();
        TCGv_i64 a64 = tcg_temp_new_i64();
        TCGv_ptr base = tcg_temp_new_ptr();
        TCGArg ri = tcgv_vec_arg(r);
        TCGArg expected[3] = { ri, 0, (TCGArg)offsets[oi] };
        TCGOpcode opc;
        unsigned args = 3, output_vece = vece;
        TCGType output_type = type;
        owner->pool_cur = (uintptr_t)arena;
        owner->pool_end = owner->pool_cur + sizeof(arena);
        owner->nb_ops = 0;
        owner->emit_before_op = NULL;
        QTAILQ_INIT(&owner->ops);
        QTAILQ_INIT(&owner->free_ops);
        if (kind == 0) {
            opc = INDEX_op_dup_vec;
            expected[1] = temp_arg(tcgv_i32_temp(a32));
            args = 2;
            tcg_gen_dup_i32_vec(vece, r, a32);
        } else if (kind == 1) {
            opc = INDEX_op_dup_vec;
            expected[1] = temp_arg(tcgv_i64_temp(a64));
            args = 2;
            if (TCG_TARGET_REG_BITS == 32 && vece == 3) {
                opc = INDEX_op_dup2_vec;
                expected[2] = temp_arg(tcgv_i64_temp(a64) + 1);
                args = 3;
            }
            tcg_gen_dup_i64_vec(vece, r, a64);
        } else {
            expected[1] = temp_arg(tcgv_ptr_temp(base));
            switch (kind) {
            case 2:
                opc = INDEX_op_dupm_vec;
                tcg_gen_dup_mem_vec(vece, r, base, offsets[oi]);
                break;
            case 3:
                opc = INDEX_op_ld_vec;
                output_vece = 0;
                tcg_gen_ld_vec(r, base, (TCGArg)offsets[oi]);
                break;
            case 4:
                opc = INDEX_op_st_vec;
                output_vece = 0;
                tcg_gen_st_vec(r, base, (TCGArg)offsets[oi]);
                break;
            default:
                opc = INDEX_op_st_vec;
                output_vece = 0;
                output_type = low;
                tcg_gen_stl_vec(r, base, (TCGArg)offsets[oi], low);
                break;
            }
        }
        TCGOp *op = QTAILQ_FIRST(&owner->ops);
        assert(owner->nb_ops == 1 && op && !QTAILQ_NEXT(op, link));
        assert(op->opc == opc && TCGOP_TYPE(op) == output_type);
        assert(TCGOP_VECE(op) == output_vece);
        for (unsigned j = 0; j < args; ++j) assert(op->args[j] == expected[j]);
        ++cases;
    }
    printf("TCG vector data: %u duplication, offset, partial-store and split-i64 cases PASS\n", cases);
}

static void check_vector_immediate(TCGContext *owner)
{
    static unsigned char arena[4096] QEMU_ALIGNED(16);
    static const uint64_t values[] = { 0, 1, UINT64_MAX, INT32_MIN, INT32_MAX,
        UINT64_C(0x8000000000000000), INT64_MAX, UINT64_C(0x0123456789abcdef) };
    unsigned cases = 0;
    for (TCGType type = TCG_TYPE_V64; type <= TCG_TYPE_V256; ++type)
    for (unsigned vece = 0; vece < 4; ++vece)
    for (unsigned vi = 0; vi < ARRAY_SIZE(values); ++vi) {
        reset_new_temps(owner);
        ++owner->const_gen;
        TCGv_vec r = tcg_temp_new_vec(type);
        TCGArg ri = tcgv_vec_arg(r);
        owner->pool_cur = (uintptr_t)arena;
        owner->pool_end = owner->pool_cur + sizeof(arena);
        owner->nb_ops = 0;
        owner->emit_before_op = NULL;
        QTAILQ_INIT(&owner->ops);
        QTAILQ_INIT(&owner->free_ops);
        unsigned bits = 8u << vece;
        uint64_t mask = UINT64_MAX >> (64 - bits);
        uint64_t expected = 0;
        for (unsigned shift = 0; shift < 64; shift += bits) {
            expected |= (values[vi] & mask) << shift;
        }
        tcg_gen_dupi_vec(vece, r, values[vi]);
        TCGOp *first = QTAILQ_FIRST(&owner->ops);
        assert(first && first->opc == INDEX_op_mov_vec && owner->nb_ops == 1);
        assert(TCGOP_TYPE(first) == type && TCGOP_VECE(first) == 0);
        assert(first->args[0] == ri);
        TCGTemp *constant = arg_temp(first->args[1]);
        check_constant_value(constant, type, (int64_t)expected);
        int used = owner->nb_temps;
        tcg_gen_dupi_vec(vece, r, values[vi]);
        TCGOp *second = QTAILQ_NEXT(first, link);
        assert(owner->nb_temps == used && owner->nb_ops == 2);
        assert(second && second->opc == INDEX_op_mov_vec && !QTAILQ_NEXT(second, link));
        assert(TCGOP_TYPE(second) == type && TCGOP_VECE(second) == 0);
        assert(second->args[0] == ri && second->args[1] == first->args[1]);
        /* An already matching constant keeps the existing self-move elision. */
        tcg_gen_dupi_vec(vece, (TCGv_vec)temp_tcgv_i32_context(owner, constant), values[vi]);
        assert(owner->nb_temps == used && owner->nb_ops == 2);
        assert(!QTAILQ_NEXT(second, link));
        cases += 3;
    }
    free(owner->const_slots);
    owner->const_slots = NULL;
    printf("TCG vector immediates: %u replication, cache and self-move cases PASS\n", cases);
}

static void check_optimizer_temps(TCGContext *current, TCGContext *other)
{
    static unsigned char arena[4096] QEMU_ALIGNED(16);
    static const TCGType types[] = { TCG_TYPE_I32, TCG_TYPE_I64,
        TCG_TYPE_V64, TCG_TYPE_V128, TCG_TYPE_V256 };
    static const uint64_t values[] = { 0, 1, UINT64_MAX, INT32_MIN, INT32_MAX,
        UINT64_C(0x8000000000000000), INT64_MAX, UINT64_C(0x0123456789abcdef) };
    unsigned cases = 0;
    for (unsigned api = 0; api < 2; ++api)
    for (unsigned ti = 0; ti < ARRAY_SIZE(types); ++ti)
    for (unsigned vi = 0; vi < ARRAY_SIZE(values); ++vi) {
        TCGContext *owner = api ? other : current;
        TCGContext *untouched = api ? current : other;
        int other_temps = untouched->nb_temps;
        uintptr_t other_pool = untouched->pool_cur;
        reset_new_temps(owner);
        ++owner->const_gen;
        owner->pool_cur = (uintptr_t)arena;
        owner->pool_end = owner->pool_cur + sizeof(arena);
        OptContext opt = { .tcg = owner, .type = types[ti] };
        int64_t expected = opt.type == TCG_TYPE_I32 ? (int32_t)values[vi] : (int64_t)values[vi];
        TCGArg constant = arg_new_constant(&opt, values[vi]);
        TCGTemp *ct = arg_temp(constant);
        assert(ct >= owner->temps && ct < owner->temps + owner->nb_temps);
        check_constant_value(ct, opt.type, expected);
        TempOptInfo *info = ct->state_ptr;
        assert(info && info->z_mask == (uint64_t)expected && info->o_mask == (uint64_t)expected);
        assert(info->s_mask == (uint64_t)(INT64_MIN >> clrsb64(expected)));
        assert(info->next_copy == ct && info->prev_copy == ct && QSIMPLEQ_EMPTY(&info->mem_copy));
        assert(test_bit(temp_idx_context(owner, ct), opt.temps_used.l));
        int used = owner->nb_temps;
        uintptr_t cursor = owner->pool_cur;
        assert(arg_new_constant(&opt, values[vi]) == constant);
        assert(owner->nb_temps == used && owner->pool_cur == cursor);
        TCGTemp *temp = arg_temp(arg_new_temp(&opt));
        assert(temp >= owner->temps && temp < owner->temps + owner->nb_temps);
        assert(temp->base_type == opt.type && temp->kind == TEMP_EBB && temp->temp_allocated);
        info = temp->state_ptr;
        assert(info && info->z_mask == UINT64_MAX && !info->o_mask && !info->s_mask);
        assert(info->next_copy == temp && info->prev_copy == temp && QSIMPLEQ_EMPTY(&info->mem_copy));
        assert(test_bit(temp_idx_context(owner, temp), opt.temps_used.l));
        tcg_temp_free_context(owner, temp);
        used = owner->nb_temps;
        cursor = owner->pool_cur;
        memset(&opt.temps_used, 0, sizeof(opt.temps_used));
        info->z_mask = 0; info->o_mask = 42; info->s_mask = 42;
        assert(arg_temp(arg_new_temp(&opt)) == temp);
        assert(owner->nb_temps == used && owner->pool_cur == cursor && temp->state_ptr == info);
        assert(info->z_mask == UINT64_MAX && !info->o_mask && !info->s_mask);
        assert(temp->temp_allocated && test_bit(temp_idx_context(owner, temp), opt.temps_used.l));
        assert(untouched->nb_temps == other_temps && untouched->pool_cur == other_pool);
        ++cases;
    }
    free(current->const_slots); current->const_slots = NULL;
    free(other->const_slots); other->const_slots = NULL;
    printf("TCG optimizer temporaries: %u owner, constant, cache, reuse and metadata cases PASS\n", cases);
}

static void check_optimizer_memory(TCGContext *current, TCGContext *other)
{
    static const TCGType types[] = { TCG_TYPE_I32, TCG_TYPE_I64,
        TCG_TYPE_V64, TCG_TYPE_V128, TCG_TYPE_V256 };
    static const unsigned sizes[] = { 4, 8, 8, 16, 32 };
    static const intptr_t offsets[] = { -128, 0, 4096 };
    static const TCGOpcode stores[] = { INDEX_op_st8, INDEX_op_st16,
        INDEX_op_st32, INDEX_op_st, INDEX_op_st_vec };
    unsigned char storage[sizeof(TCGOp) + 8 * sizeof(TCGArg)] QEMU_ALIGNED(16);
    memory_op = (TCGOp *)storage;
    tcg_env = (TCGv_env)((uintptr_t)&current->temps[7] - (uintptr_t)current);
    unsigned cases = 0;
    for (unsigned api = 0; api < 2; ++api)
    for (unsigned ti = 0; ti < ARRAY_SIZE(types); ++ti)
    for (unsigned oi = 0; oi < ARRAY_SIZE(offsets); ++oi)
    for (unsigned base = 0; base < 3; ++base) {
        memory_owner = api ? other : current;
        memory_type = types[ti];
        memory_start = offsets[oi];
        memory_last = memory_start + sizes[ti] - 1;
        OptContext ctx = { .tcg = memory_owner, .type = memory_type };
        memset(storage, 0, sizeof(storage));
        memory_value = &memory_owner->temps[19];
        memory_value->base_type = memory_type;
        memory_op->args[0] = temp_arg(memory_value);
        memory_op->args[1] = temp_arg(base == 0 ? &memory_owner->temps[7] :
            base == 1 ? &memory_owner->temps[11] : &current->temps[7]);
        memory_op->args[2] = (TCGArg)memory_start;
        bool env = base == 0 || (base == 2 && !api);
        assert(is_env_base(&ctx, memory_op->args[1]) == env);
        for (unsigned hit = 0; hit < 3; ++hit) {
            memory_copy = hit ? &memory_owner->temps[23] : NULL;
            if (memory_copy) memory_copy->base_type = hit == 1 ? memory_type : TCG_TYPE_F32;
            memory_event_count = 0;
            bool result = fold_tcg_ld_memcopy(&ctx, memory_op);
            assert(result == (env && hit != 1));
            assert(!strcmp(memory_events, !env ? "Z" : hit == 1 ? "FM" : "FRC"));
            ++cases;
        }
        for (unsigned si = 0; si < ARRAY_SIZE(stores); ++si) {
            memory_op->opc = stores[si];
            memory_last = memory_start + (si < 3 ? (1u << si) : sizes[ti]) - 1;
            memory_event_count = 0;
            assert(fold_tcg_st(&ctx, memory_op));
            assert(!strcmp(memory_events, env ? "I" : "A"));
            ++cases;
        }
        memory_op->opc = INDEX_op_st;
        memory_last = memory_start + sizes[ti] - 1;
        for (unsigned constant = 0; constant < 2; ++constant)
        for (unsigned hit = 0; hit < 3; ++hit) {
            memory_constant = constant;
            memory_copy = hit == 0 ? NULL : hit == 1 ? memory_value : &memory_owner->temps[23];
            memory_event_count = 0;
            assert(fold_tcg_st_memcopy(&ctx, memory_op));
            const char *expected = !env ? "A" : !constant ? "IC" : hit == 1 ? "FD" : "FIC";
            assert(!strcmp(memory_events, expected));
            ++cases;
        }
    }
    printf("TCG optimizer memory: %u owner, base, forwarding and invalidation cases PASS\n", cases);
}

int main(void)
{
    static TCGContext context;
    assert(sizeof(void *) * CHAR_BIT == TCG_TARGET_REG_BITS);
#ifdef _WIN32
    tls_key_tcg_ctx = TlsAlloc();
    assert(tls_key_tcg_ctx != TLS_OUT_OF_INDEXES);
#endif
    SET_tcg_ctx(&context);
    static unsigned char arena[16384] QEMU_ALIGNED(16);
    TCGLabel *labels[128], *label;
    context.pool_cur = (uintptr_t)arena;
    context.pool_end = context.pool_cur + sizeof(arena);
    memset(arena, 0xa5, sizeof(arena));
    QSIMPLEQ_INIT(&context.labels);
    for (unsigned i = 0; i < ARRAY_SIZE(labels); ++i) {
        labels[i] = label = gen_new_label();
        assert(!label->has_value && !label->u.value);
        assert(QSIMPLEQ_EMPTY(&label->branches) && QSIMPLEQ_EMPTY(&label->relocs));
        assert(label->branches.sqh_last == &label->branches.sqh_first);
        assert(label->relocs.sqh_last == &label->relocs.sqh_first);
        /* Each new label must preserve existing list members and values. */
        label->u.value = i + 1;
        label->has_value = true;
    }
    unsigned count = 0;
    QSIMPLEQ_FOREACH(label, &context.labels, next) {
        assert(count < ARRAY_SIZE(labels) && label == labels[count]);
        assert(label->has_value && label->u.value == count + 1);
        ++count;
    }
    assert(count == ARRAY_SIZE(labels));
    memset(context.temps, 0xa5, sizeof(context.temps));
    TCGTemp *fixed = tcg_global_reg_new_internal(&context, TCG_TYPE_PTR, 0);
    assert(fixed->kind == TEMP_FIXED && fixed->reg == 0);
    assert(tcg_regset_test_reg(context.reserved_regs, 0));
    TCGv_ptr handle = temp_tcgv_ptr(fixed);
    static const intptr_t offsets[] = {0, 4, 512, -8};
    for (unsigned i = 0; i < ARRAY_SIZE(offsets); ++i) {
        int first = context.nb_globals;
        TCGv_i32 value32 = tcg_global_mem_new_i32(handle, offsets[i]);
        assert(tcgv_i32_temp(value32) == &context.temps[first]);
        assert(context.nb_globals == first + 1);
        check_global(tcgv_i32_temp(value32), fixed, offsets[i], TCG_TYPE_I32, false);
        first = context.nb_globals;
        TCGv_i64 value64 = tcg_global_mem_new_i64(handle, offsets[i]);
        assert(tcgv_i64_temp(value64) == &context.temps[first]);
        assert(context.nb_globals == first + (TCG_TARGET_REG_BITS == 32 ? 2 : 1));
        check_global(tcgv_i64_temp(value64), fixed, offsets[i], TCG_TYPE_I64, false);
    }
    TCGTemp *indirect = tcg_global_mem_new_internal(handle, 128, TCG_TYPE_PTR);
    int first = context.nb_globals;
    TCGTemp *value = tcg_global_mem_new_internal(temp_tcgv_ptr(indirect), 256,
                                               TCG_TYPE_I64);
    assert(value == &context.temps[first] && indirect->indirect_base);
    assert(context.nb_indirects == (TCG_TARGET_REG_BITS == 32 ? 2 : 1));
    check_global(value, indirect, 256, TCG_TYPE_I64, true);
    tcg_set_frame(&context, 1, 64, 4096);
    assert(context.frame_start == 64 && context.frame_end == 4160);
    assert(context.frame_temp->kind == TEMP_FIXED && context.frame_temp->reg == 1);
    assert(tcg_regset_test_reg(context.reserved_regs, 1));
    assert(context.nb_globals == context.nb_temps);
    /* Clearing one compact temporary must not overwrite its next neighbor. */
    const unsigned char *next = (const void *)&context.temps[context.nb_temps];
    for (unsigned i = 0; i < sizeof(TCGTemp); ++i) {
        assert(next[i] == 0xa5);
    }
    /* An explicit owner can differ from TLS; both paths must retain it. */
    static TCGContext other;
    /* Every slot must use its supplied owner even when TLS points elsewhere. */
    for (unsigned i = 0; i < ARRAY_SIZE(context.temps); ++i) {
        assert(temp_idx_context(&context, &context.temps[i]) == i);
        assert(temp_idx_context(&other, &other.temps[i]) == i);
        assert(temp_idx(&context.temps[i]) == i);
        /* Handles retain every slot's offset and their non-NULL encoding. */
        TCGv_i32 h = temp_tcgv_i32(&context.temps[i]);
        assert(h != NULL);
        assert((uintptr_t)h == (uintptr_t)&context.temps[i] - (uintptr_t)&context);
        assert(tcgv_i32_temp(h) == &context.temps[i]);
        assert(tcgv_i64_temp(temp_tcgv_i64(&context.temps[i])) == &context.temps[i]);
        assert(tcgv_ptr_temp(temp_tcgv_ptr(&context.temps[i])) == &context.temps[i]);
    }
    puts("TCG indices: explicit/TLS owners and every temporary slot PASS");
    static unsigned char other_arena[128] QEMU_ALIGNED(16);
    static const struct { int requested, aligned; } requests[] = {
        {0, 0}, {1, 8}, {7, 8}, {8, 8}, {9, 16}, {63, 64}, {64, 64}
    };
    uintptr_t base = (uintptr_t)other_arena;
    uintptr_t current_cursor = context.pool_cur;
    for (unsigned i = 0; i < ARRAY_SIZE(requests); ++i) {
        other.pool_cur = base;
        other.pool_end = base + requests[i].aligned;
        unsigned calls = refill_calls;
        assert(tcg_malloc_context(&other, requests[i].requested) == other_arena);
        assert(other.pool_cur == other.pool_end && refill_calls == calls);
        assert(context.pool_cur == current_cursor);
        if (requests[i].aligned) {
            other.pool_cur = base;
            other.pool_end = base + requests[i].aligned - 1;
            refill_owner = &other;
            refill_size = requests[i].aligned;
            assert(tcg_malloc_context(&other, requests[i].requested) == refill_buffer);
            assert(refill_calls == calls + 1 && other.pool_cur == base);
            assert(context.pool_cur == current_cursor);
        }
    }
    static const int op_counts[] = {0, 3999, 4000, 4001, INT_MAX};
    for (unsigned i = 0; i < ARRAY_SIZE(op_counts); ++i) {
        other.nb_ops = op_counts[i];
        context.nb_ops = op_counts[i] < 4000 ? 4000 : 0;
        assert(tcg_op_buf_full(&other) == (op_counts[i] >= 4000));
        assert(tcg_op_buf_full(&context) == (context.nb_ops >= 4000));
    }
    puts("TCG operation limit: supplied owners and 3999/4000/4001 boundaries PASS");
    /* Existing callers without a local owner retain their TLS-based API. */
    context.pool_end = current_cursor + 8;
    unsigned calls = refill_calls;
    assert(tcg_malloc(1) == (void *)current_cursor);
    assert(context.pool_cur == current_cursor + 8 && refill_calls == calls);
    refill_owner = &context;
    refill_size = 16;
    assert(tcg_malloc(9) == refill_buffer && refill_calls == calls + 1);
    check_temp_free(&context, &other);
    check_temp_new(&context, &other);
    check_temp_constants(&context, &other);
    check_ops(&other, false); /* Explicit owner differs from TLS. */
    check_ops(&context, true);
    check_rollback(&context);
    check_count_operands(&context);
    check_vector_operands(&context, &other);
    check_vector_shifts(&context);
    check_vector_comparison(&context);
    check_vector_bitsel(&context);
    check_vector_cmpsel(&context);
    check_vector_data(&context);
    check_vector_immediate(&context);
    check_optimizer_temps(&context, &other);
    check_optimizer_memory(&context, &other);
    puts("TCG rollback: suffix/full/empty removal, branch uses and reuse PASS");
    puts("TCG ops: owners, allocation/reuse, insertion order and epilogues PASS");
    printf("TCG global layout: %d-bit direct/indirect globals, %shandles and frame PASS\n",
           TCG_TARGET_REG_BITS, TCG_TARGET_REG_BITS == 32 ? "split halves, " : "");
    puts("TCG labels: pool allocation, queue heads, identity, order and values PASS");
    puts("TCG pools: explicit/TLS owners, alignment, exact fit and refill forwarding PASS");
#ifdef _WIN32
    TlsFree(tls_key_tcg_ctx);
#endif
    return 0;
}
