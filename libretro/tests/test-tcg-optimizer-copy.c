/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/interval-tree.h"
#include "tcg/tcg.h"
G_NORETURN void g_assertion_message_expr(const char *domain, const char *file,
                                       int line, const char *func, const char *expr)
{
    fprintf(stderr, "%s:%d: %s: %s\n", file, line, func, expr ? expr : "unreachable");
    abort();
}
#ifdef _WIN32
unsigned long tls_key_tcg_ctx;
#else
__thread TCGContext *tcg_ctx;
#endif
void *tcg_malloc_internal(TCGContext *s, int size) { abort(); }
/* PRODUCTION */
static uint32_t seed = 42;
static unsigned random_value(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static TCGContext old, fresh, foreign;
static unsigned char old_arena[16384] QEMU_ALIGNED(16), new_arena[16384] QEMU_ALIGNED(16);
typedef union {
    max_align_t align;
    unsigned char data[sizeof(TCGOp) + 8 * sizeof(TCGArg)];
} OpStorage;
int main(void)
{
#ifdef _WIN32
    tls_key_tcg_ctx = TlsAlloc(); assert(tls_key_tcg_ctx != TLS_OUT_OF_INDEXES);
#endif
    SET_tcg_ctx(&foreign);
    for (unsigned trial = 0; trial < 100000; ++trial) {
        memset(&old, 0, sizeof(old)); memset(&fresh, 0, sizeof(fresh));
        old.pool_cur = (uintptr_t)old_arena; old.pool_end = old.pool_cur + sizeof(old_arena);
        fresh.pool_cur = (uintptr_t)new_arena; fresh.pool_end = fresh.pool_cur + sizeof(new_arena);
        reference_OptContext a = {.tcg = &old};
        OptContext b = {.tcg = &fresh};
        for (unsigned i = 0; i < 32; ++i) {
            old.temps[i].kind = fresh.temps[i].kind = i % 5;
            old.temps[i].val = fresh.temps[i].val = i * 17;
        }
        /* Populate copy rings, then sometimes cross an EBB boundary so their
         * allocated metadata remains present but must be initialized again. */
        for (unsigned epoch = 0; epoch < 3; ++epoch) {
            if (epoch) { memset(&a.temps_used, 0, sizeof(a.temps_used)); ++b.ebb; }
            for (unsigned group = 0; group < 8; ++group) if (random_value() & 1) {
                for (unsigned j = 0; j < 4; ++j) {
                    unsigned i = group * 4 + j;
                    reference_init_ts_info(&a, &old.temps[i]);
                    init_ts_info(&b, &fresh.temps[i]);
                }
                for (unsigned j = 0; j < 4; ++j) {
                    unsigned i = group * 4 + j;
                    reference_TempOptInfo *x = old.temps[i].state_ptr;
                    TempOptInfo *y = fresh.temps[i].state_ptr;
                    x->next_copy = &old.temps[group * 4 + (j + 1) % 4];
                    y->next_copy = &fresh.temps[group * 4 + (j + 1) % 4];
                    x->prev_copy = &old.temps[group * 4 + (j + 3) % 4];
                    y->prev_copy = &fresh.temps[group * 4 + (j + 3) % 4];
                }
            }
            OpStorage ao = {0}, bo = {0};
            TCGOp *x = (TCGOp *)ao.data, *y = (TCGOp *)bo.data;
            unsigned outputs = random_value() % 3, inputs = random_value() % 6;
            for (unsigned i = 0; i < outputs + inputs; ++i) {
                unsigned index = random_value() % 32;
                x->args[i] = temp_arg(&old.temps[index]);
                y->args[i] = temp_arg(&fresh.temps[index]);
            }
            reference_init_arguments(&a, x, outputs + inputs);
            reference_copy_propagate(&a, x, outputs, inputs);
            init_and_copy_propagate(&b, y, outputs, inputs);
            for (unsigned i = 0; i < outputs + inputs; ++i)
                assert(arg_temp(x->args[i]) - old.temps == arg_temp(y->args[i]) - fresh.temps);
            for (unsigned i = 0; i < 32; ++i) {
                reference_TempOptInfo *u = old.temps[i].state_ptr;
                TempOptInfo *v = fresh.temps[i].state_ptr;
                assert((u == NULL) == (v == NULL));
                if (!u) continue;
                assert(u->z_mask == v->z_mask && u->o_mask == v->o_mask && u->s_mask == v->s_mask);
                assert(u->next_copy - old.temps == v->next_copy - fresh.temps);
                assert(u->prev_copy - old.temps == v->prev_copy - fresh.temps);
                assert(test_bit(i, a.temps_used.l) == (v->ebb == b.ebb));
                assert(QSIMPLEQ_EMPTY(&u->mem_copy) && QSIMPLEQ_EMPTY(&v->mem_copy));
            }
        }
    }
    assert(!foreign.pool_cur);
    puts("TCG optimizer: 300000 differential alias/copy-ring/readonly/duplicate-operand/EBB-generation cases with foreign TLS PASS");
    printf("Optimizer scratch metadata per initialized temp: reference=%zu candidate=%zu bytes\n",
           sizeof(reference_TempOptInfo), sizeof(TempOptInfo));
#ifdef _WIN32
    TlsFree(tls_key_tcg_ctx);
#endif
    return 0;
}
