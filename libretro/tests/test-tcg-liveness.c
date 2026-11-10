/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-internal.h"
#include "tcg/tcg-has.h"

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
#undef tcg_debug_assert
#define tcg_debug_assert(x) assert(x)
static TCGRegSet tcg_target_available_regs[TCG_TYPE_COUNT];
static TCGRegSet tcg_target_call_clobber_regs = 0x55;
static const TCGReg tcg_target_call_iarg_regs[] = {1, 2, 3, 4};
static bool arg_slot_reg_p(unsigned slot) { return slot < 4; }
static TCGArgConstraint constraints[10];
static const TCGArgConstraint *opcode_args_ct(const TCGOp *op) { return constraints; }
bool tcg_op_supported(TCGOpcode op, TCGType type, unsigned flags) { return true; }
const TCGOpDef tcg_op_defs[] = {
#define DEF(name, o, i, c, flags) {o, i, c, o + i + c, flags, 0},
#include "tcg/tcg-opc.h"
#undef DEF
};
void *tcg_malloc_internal(TCGContext *s, int size) { abort(); }
TCGTemp *tcg_constant_internal(TCGType type, int64_t value)
{
    TCGContext *s = tcg_ctx;
    for (int i = 0; i < s->nb_temps; ++i) {
        TCGTemp *t = &s->temps[i];
        if (t->kind == TEMP_CONST && t->type == type && t->val == value) return t;
    }
    assert(s->nb_temps < TCG_MAX_TEMPS);
    TCGTemp *t = &s->temps[s->nb_temps++];
    *t = (TCGTemp){.kind = TEMP_CONST, .type = type, .base_type = type, .val = value};
    return t;
}
TCGOp *tcg_op_insert_before(TCGContext *s, TCGOp *op, TCGOpcode opc,
                             TCGType type, unsigned nargs) { abort(); }
static unsigned long long reference_visits, candidate_visits;
/* PRODUCTION */

enum { OPS = 96, LABELS = 8 };
typedef union {
    max_align_t align;
    unsigned char bytes[sizeof(TCGOp) + 10 * sizeof(TCGArg)];
} OpSlot;
typedef struct Fixture {
    TCGContext context;
    OpSlot ops[OPS];
    TCGLabel labels[LABELS];
    TCGLabelUse uses[OPS];
    TCGHelperInfo helpers[OPS];
    unsigned char arena[16384] QEMU_ALIGNED(16);
} Fixture;
static Fixture old, fresh;
static uint32_t seed;
static unsigned random_value(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static TCGOp *slot(Fixture *f, unsigned i) { return (TCGOp *)f->ops[i].bytes; }
static void setup(Fixture *f, unsigned input)
{
    static const unsigned counts[] = {32, 63, 64, 65, 127, 128, 129, 255, 256, 511, 512};
    memset(f, 0, sizeof(*f));
    seed = input + 1;
    TCGContext *s = &f->context;
    s->nb_temps = counts[input % ARRAY_SIZE(counts)];
    s->nb_globals = input % (s->nb_temps / 2 + 1);
    s->pool_cur = (uintptr_t)f->arena;
    s->pool_end = s->pool_cur + sizeof(f->arena);
    QTAILQ_INIT(&s->ops); QTAILQ_INIT(&s->free_ops);
    for (unsigned i = 0; i < LABELS; ++i) {
        QSIMPLEQ_INIT(&f->labels[i].branches);
    }
    for (int i = 0; i < s->nb_temps; ++i) {
        TCGTemp *t = &s->temps[i];
        t->kind = i < s->nb_globals ? (i ? TEMP_GLOBAL : TEMP_FIXED) :
                  (i & 1 ? TEMP_TB : TEMP_EBB);
        t->type = t->base_type = i & 1 ? TCG_TYPE_I64 : TCG_TYPE_I32;
        if (i >= s->nb_temps - 2) { t->kind = TEMP_CONST; t->val = i * 17; }
    }
    unsigned label_number = 0;
    for (unsigned i = 0; i < OPS; ++i) {
        static const TCGOpcode codes[] = { INDEX_op_mov, INDEX_op_add,
            INDEX_op_sub, INDEX_op_mul, INDEX_op_st, INDEX_op_ld,
            INDEX_op_discard, INDEX_op_mb, INDEX_op_call, INDEX_op_br,
            INDEX_op_brcond, INDEX_op_insn_start, INDEX_op_mulu2,
            INDEX_op_muls2, INDEX_op_addco, INDEX_op_subbo };
        TCGOp *op = slot(f, i);
        op->opc = codes[random_value() % ARRAY_SIZE(codes)];
        if (s->nb_temps > 500 && op->opc == INDEX_op_subbo) op->opc = INDEX_op_add;
        if (!i) op->opc = INDEX_op_ld; /* reachability's first-op contract */
        if (i == OPS - 1) op->opc = INDEX_op_exit_tb;
        if (i && i < OPS - 1 && ((i % 11 == 0 && i != 33) || i == 23))
            op->opc = INDEX_op_set_label;
        TCGOP_TYPE(op) = TCG_TYPE_I64;
        const TCGOpDef *def = &tcg_op_defs[op->opc];
        op->nargs = def->nb_args;
        for (unsigned j = 0; j < def->nb_oargs + def->nb_iargs; ++j)
            op->args[j] = temp_arg(&s->temps[random_value() %
                (s->nb_temps - (j < def->nb_oargs ? 2 : 0))]);
        if (op->opc == INDEX_op_call) {
            unsigned flags[] = {0, TCG_CALL_NO_WG, TCG_CALL_NO_RWG,
                TCG_CALL_NO_RWG | TCG_CALL_NO_SIDE_EFFECTS, TCG_CALL_NO_RETURN};
            TCGHelperInfo *h = &f->helpers[i];
            h->flags = flags[random_value() % ARRAY_SIZE(flags)];
            TCGOP_CALLO(op) = random_value() % 2;
            TCGOP_CALLI(op) = random_value() % 5;
            op->nargs = TCGOP_CALLO(op) + TCGOP_CALLI(op) + 2;
            for (unsigned j = 0; j < TCGOP_CALLO(op) + TCGOP_CALLI(op); ++j)
                op->args[j] = temp_arg(&s->temps[random_value() %
                    (s->nb_temps - (j < TCGOP_CALLO(op) ? 2 : 0))]);
            for (unsigned j = 0; j < TCGOP_CALLI(op); ++j) {
                h->in[j].kind = random_value() % 3;
                h->in[j].arg_slot = random_value() % 6;
            }
            op->args[op->nargs - 1] = (uintptr_t)h;
        } else if (op->opc == INDEX_op_set_label) {
            assert(label_number < LABELS);
            op->args[0] = label_arg(&f->labels[label_number++]);
        } else if (op->opc == INDEX_op_br || op->opc == INDEX_op_brcond) {
            unsigned arg = op->opc == INDEX_op_br ? 0 : 3;
            TCGLabel *label = &f->labels[random_value() % LABELS];
            op->args[arg] = label_arg(label);
            f->uses[i].op = op;
            QSIMPLEQ_INSERT_TAIL(&label->branches, &f->uses[i], next);
        }
        QTAILQ_INSERT_TAIL(&s->ops, op, link); ++s->nb_ops;
    }
}
static unsigned op_id(Fixture *f, TCGOp *op)
{
    return ((unsigned char *)op - (unsigned char *)f->ops) / sizeof(OpSlot);
}
static void compare(void)
{
    TCGOp *a = QTAILQ_FIRST(&old.context.ops), *b = QTAILQ_FIRST(&fresh.context.ops);
    assert(old.context.nb_ops == fresh.context.nb_ops);
    assert(old.context.nb_temps == fresh.context.nb_temps);
    while (a && b) {
        assert(op_id(&old, a) == op_id(&fresh, b));
        assert(a->opc == b->opc && a->life == b->life);
        assert(!memcmp(a->output_pref, b->output_pref, sizeof(a->output_pref)));
        unsigned args = tcg_op_defs[a->opc].nb_oargs + tcg_op_defs[a->opc].nb_iargs;
        if (a->opc == INDEX_op_call) args = TCGOP_CALLO(a) + TCGOP_CALLI(a);
        for (unsigned j = 0; j < args; ++j)
            assert(arg_temp(a->args[j]) - old.context.temps ==
                   arg_temp(b->args[j]) - fresh.context.temps);
        if (a->opc == INDEX_op_br || a->opc == INDEX_op_brcond || a->opc == INDEX_op_set_label) {
            unsigned index = a->opc == INDEX_op_brcond ? 3 : 0;
            assert(arg_label(a->args[index]) - old.labels ==
                   arg_label(b->args[index]) - fresh.labels);
        }
        a = QTAILQ_NEXT(a, link); b = QTAILQ_NEXT(b, link);
    }
    assert(!a && !b);
    for (int i = 0; i < old.context.nb_temps; ++i) {
        TCGTemp *x = &old.context.temps[i], *y = &fresh.context.temps[i];
        assert(x->kind == y->kind && x->state == y->state);
        assert(*(TCGRegSet *)x->state_ptr == *(TCGRegSet *)y->state_ptr);
    }
}
int main(void)
{
#ifdef _WIN32
    tls_key_tcg_ctx = TlsAlloc(); assert(tls_key_tcg_ctx != TLS_OUT_OF_INDEXES);
#endif
    for (unsigned i = 0; i < TCG_TYPE_COUNT; ++i) tcg_target_available_regs[i] = 0xff;
    for (unsigned i = 0; i < ARRAY_SIZE(constraints); ++i) constraints[i].regs = 0xff;
    for (unsigned i = 0; i < 10000; ++i) {
        setup(&old, i); setup(&fresh, i);
        SET_tcg_ctx(&old.context);
        reference_reachable_code_pass(&old.context);
        reference_liveness_pass_0(&old.context);
        reference_liveness_pass_1(&old.context);
        /* Dense boundary cases also exercise bitmap bookkeeping with foreign
         * TLS. Constant creation retains the existing TLS allocator contract. */
        if (fresh.context.nb_temps <= 500) SET_tcg_ctx(&fresh.context);
        reachable_code_pass(&fresh.context);
        liveness_pass_1(&fresh.context);
        compare();
    }
    puts("TCG liveness: 10000 differential TBs, label folding, dead code, calls, register preferences, sparse/dense temps and non-TLS owners PASS");
    assert(candidate_visits < reference_visits);
    printf("Call-boundary temp visits: reference=%llu candidate=%llu (synthetic work count, not gameplay timing)\n",
           reference_visits, candidate_visits);
#ifdef _WIN32
    TlsFree(tls_key_tcg_ctx);
#endif
    return 0;
}
