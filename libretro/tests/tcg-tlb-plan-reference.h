/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Frozen planner from 6a988810a3 for differential checks. */
static void reference_tlbm_plan(TCGContext *s)
{
    TCGOp *op;
    int n = 0, next = 1, k = 0, seg = 0, cur[TLBM_SLOTS] = { -1, -1 };
    int *vroot;
    uint32_t *voff;
    TLBMAccess *acc;
    bool *used;

    s->tlbm_ihint = s->tlbm_nhint = 0;
    s->tlbm_filled = 0;
    if (!tlbm_call_regs || s->addr_type != TCG_TYPE_I32
        || TARGET_PAGE_BITS > 12) {
        return;
    }
    QTAILQ_FOREACH(op, &s->ops, link) {
        n += op->opc == INDEX_op_qemu_ld || op->opc == INDEX_op_qemu_st ||
             op->opc == INDEX_op_qemu_ld2 || op->opc == INDEX_op_qemu_st2;
    }
    if (n < 2) {
        return;
    }
    s->tlbm_hint = tcg_malloc_context(s, n);
    s->tlbm_nhint = n;
    acc = tcg_malloc_context(s, sizeof(*acc) * n);
    used = tcg_malloc_context(s, n);
    memset(used, 0, n);
    vroot = tcg_malloc_context(s, sizeof(int) * s->nb_temps);
    voff = tcg_malloc_context(s, sizeof(uint32_t) * s->nb_temps);
    memset(vroot, 0, sizeof(int) * s->nb_temps);

    /* Every access's address as (root, offset), and its segment. */
    QTAILQ_FOREACH(op, &s->ops, link) {
        TCGOpcode opc = op->opc;
        int r, r2;
        uint32_t o, o2;

        switch (opc) {
        case INDEX_op_insn_start:
            break;
        case INDEX_op_set_label:
        case INDEX_op_br:
        case INDEX_op_exit_tb:
        case INDEX_op_goto_tb:
        case INDEX_op_goto_ptr:
            seg++;
            break;
        case INDEX_op_call:
            /* The helper may write any global. */
            seg++;
            memset(vroot, 0, sizeof(int) * s->nb_globals);
            for (int i = 0; i < TCGOP_CALLO(op); i++) {
                tlbm_def(s, vroot, voff, op->args[i], next++, 0);
            }
            break;
        case INDEX_op_mov:
            tlbm_val(s, vroot, voff, &next, op->args[1], &r, &o);
            tlbm_def(s, vroot, voff, op->args[0], r, o);
            break;
        case INDEX_op_add:
        case INDEX_op_sub:
            tlbm_val(s, vroot, voff, &next, op->args[1], &r, &o);
            tlbm_val(s, vroot, voff, &next, op->args[2], &r2, &o2);
            if (r2 == -1) {
                o = opc == INDEX_op_add ? o + o2 : o - o2;
            } else if (r == -1 && opc == INDEX_op_add) {
                r = r2;
                o += o2;
            } else {
                r = next++;
                o = 0;
            }
            tlbm_def(s, vroot, voff, op->args[0], r, o);
            break;
        case INDEX_op_qemu_ld:
        case INDEX_op_qemu_st:
        case INDEX_op_qemu_ld2:
        case INDEX_op_qemu_st2: {
            /* One data register, or two (the i128 accesses) */
            int nd = opc == INDEX_op_qemu_ld2 || opc == INDEX_op_qemu_st2
                     ? 2 : 1;
            bool is_st = opc == INDEX_op_qemu_st || opc == INDEX_op_qemu_st2;
            MemOpIdx oi = op->args[nd + 1];
            MemOp mop = get_memop(oi);
            TLBMAccess *x = &acc[k++];

            tlbm_val(s, vroot, voff, &next, op->args[nd], &x->root, &x->off);
            x->seg = seg;
            x->slot = is_st;
            x->mmu = get_mmuidx(oi);
            x->size = 1u << (mop & MO_SIZE);
            if (!is_st) {
                for (int i = 0; i < nd; i++) {
                    tlbm_def(s, vroot, voff, op->args[i], next++, 0);
                }
            }
            break;
        }
        default:
            for (int i = 0; i < tcg_op_defs[opc].nb_oargs; i++) {
                tlbm_def(s, vroot, voff, op->args[i], next++, 0);
            }
            break;
        }
    }

    /* Share, fill or plain walk; a fill replaces the page needed later. */
    for (int i = 0; i < n; i++) {
        const TLBMAccess *x = &acc[i];
        int c = cur[x->slot];

        s->tlbm_hint[i] = TLBM_WALK;
        if (c >= 0 && tlbm_hits(&acc[c], x)) {
            s->tlbm_hint[i] = TLBM_SHARE;
            used[c] = true;
        } else if (c < 0 || acc[c].seg != x->seg
                   || tlbm_next_use(acc, n, i, i)
                      <= tlbm_next_use(acc, n, i, c)) {
            s->tlbm_hint[i] = TLBM_FILL;
            cur[x->slot] = i;
        }
    }
    /* A fill that nothing shares is a plain walk. */
    for (int i = 0; i < n; i++) {
        if (s->tlbm_hint[i] == TLBM_FILL && !used[i]) {
            s->tlbm_hint[i] = TLBM_WALK;
        }
    }
}
