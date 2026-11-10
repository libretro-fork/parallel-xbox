/*
 * Optimizations for Tiny Code Generator for QEMU
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
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


/* Frozen operand preparation from 3bca7c4524. */
typedef struct MemCopyInfo {
    IntervalTreeNode itree;
    QSIMPLEQ_ENTRY (MemCopyInfo) next;
    TCGTemp *ts;
    TCGType type;
} MemCopyInfo;

typedef struct TempOptInfo {
    TCGTemp *prev_copy;
    TCGTemp *next_copy;
    QSIMPLEQ_HEAD(, MemCopyInfo) mem_copy;
    uint64_t z_mask;  /* mask bit is 0 if and only if value bit is 0 */
    uint64_t o_mask;  /* mask bit is 1 if and only if value bit is 1 */
    uint64_t s_mask;  /* mask bit is 1 if value bit matches msb */
} TempOptInfo;

typedef struct OptContext {
    TCGContext *tcg;
    TCGOp *prev_mb;
    TCGTempSet temps_used;

    IntervalTreeRoot mem_copy;
    QSIMPLEQ_HEAD(, MemCopyInfo) mem_free;

    /* In flight values from optimization. */
    TCGType type;
    int carry_state;  /* -1 = non-constant, {0,1} = constant carry-in */
} OptContext;

static inline TempOptInfo *ts_info(TCGTemp *ts)
{
    return ts->state_ptr;
}
static inline bool ts_is_copy(TCGTemp *ts)
{
    return ts_info(ts)->next_copy != ts;
}
static TCGTemp *cmp_better_copy(TCGTemp *a, TCGTemp *b)
{
    return a->kind < b->kind ? b : a;
}
static void init_ts_info(OptContext *ctx, TCGTemp *ts)
{
    size_t idx = temp_idx_context(ctx->tcg, ts);
    TempOptInfo *ti;

    if (test_bit(idx, ctx->temps_used.l)) {
        return;
    }
    set_bit(idx, ctx->temps_used.l);

    ti = ts->state_ptr;
    if (ti == NULL) {
        ti = tcg_malloc_context(ctx->tcg, sizeof(TempOptInfo));
        ts->state_ptr = ti;
    }

    ti->next_copy = ts;
    ti->prev_copy = ts;
    QSIMPLEQ_INIT(&ti->mem_copy);
    if (ts->kind == TEMP_CONST) {
        ti->z_mask = ts->val;
        ti->o_mask = ts->val;
        ti->s_mask = INT64_MIN >> clrsb64(ts->val);
    } else {
        ti->z_mask = -1;
        ti->o_mask = 0;
        ti->s_mask = 0;
    }
}
static TCGTemp *find_better_copy(TCGTemp *ts)
{
    TCGTemp *i, *ret;

    /* If this is already readonly, we can't do better. */
    if (temp_readonly(ts)) {
        return ts;
    }

    ret = ts;
    for (i = ts_info(ts)->next_copy; i != ts; i = ts_info(i)->next_copy) {
        ret = cmp_better_copy(ret, i);
    }
    return ret;
}
static void init_arguments(OptContext *ctx, TCGOp *op, int nb_args)
{
    for (int i = 0; i < nb_args; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        init_ts_info(ctx, ts);
    }
}
static void copy_propagate(OptContext *ctx, TCGOp *op,
                           int nb_oargs, int nb_iargs)
{
    for (int i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        if (ts_is_copy(ts)) {
            op->args[i] = temp_arg(find_better_copy(ts));
        }
    }
}
