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
/* PRODUCTION */

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
    printf("TCG global layout: %d-bit direct/indirect globals, %shandles and frame PASS\n",
           TCG_TARGET_REG_BITS, TCG_TARGET_REG_BITS == 32 ? "split halves, " : "");
    puts("TCG labels: pool allocation, queue heads, identity, order and values PASS");
    puts("TCG pools: explicit/TLS owners, alignment, exact fit and refill forwarding PASS");
#ifdef _WIN32
    TlsFree(tls_key_tcg_ctx);
#endif
    return 0;
}
