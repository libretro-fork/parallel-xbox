/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "tcg/tcg.h"
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
/* PRODUCTION */

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
        assert(!label->present && !label->has_value && !label->u.value);
        assert(QSIMPLEQ_EMPTY(&label->branches) && QSIMPLEQ_EMPTY(&label->relocs));
        assert(label->branches.sqh_last == &label->branches.sqh_first);
        assert(label->relocs.sqh_last == &label->relocs.sqh_first);
        /* Each new label must preserve existing list members and values. */
        label->u.value = i + 1;
        label->present = label->has_value = true;
    }
    unsigned count = 0;
    QSIMPLEQ_FOREACH(label, &context.labels, next) {
        assert(count < ARRAY_SIZE(labels) && label == labels[count]);
        assert(label->present && label->has_value && label->u.value == count + 1);
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
    /* Existing callers without a local owner retain their TLS-based API. */
    context.pool_end = current_cursor + 8;
    unsigned calls = refill_calls;
    assert(tcg_malloc(1) == (void *)current_cursor);
    assert(context.pool_cur == current_cursor + 8 && refill_calls == calls);
    refill_owner = &context;
    refill_size = 16;
    assert(tcg_malloc(9) == refill_buffer && refill_calls == calls + 1);
    check_ops(&other, false); /* Explicit owner differs from TLS. */
    check_ops(&context, true);
    check_rollback(&context);
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
