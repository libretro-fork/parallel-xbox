/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SSA IDs only: the classifier borrows existing geometry inputs and emits no
 * host payload, uniform, descriptor or GPU allocation. See append_ui_prepare. */
typedef struct GeomUi {
    Writer *w;
    uint32_t ft, bt, ext, zero, one, half, epsilon, negative_zero, yes, no;
    uint32_t block, valid, lo[2], hi[2];
} GeomUi;

static uint32_t ui_binary(GeomUi *u, SpvOp op_, uint32_t a, uint32_t b)
{
    bool boolean = op_ == SpvOpLogicalAnd || op_ == SpvOpLogicalOr ||
        op_ == SpvOpFOrdEqual || op_ == SpvOpFOrdNotEqual ||
        op_ == SpvOpFOrdGreaterThan || op_ == SpvOpFOrdGreaterThanEqual ||
        op_ == SpvOpFOrdLessThanEqual;
    return value(u->w, op_, boolean ? u->bt : u->ft, 2, a, b);
}

static uint32_t ui_ext(GeomUi *u, unsigned instruction, uint32_t a)
{
    return value(u->w, SpvOpExtInst, u->ft, 3, u->ext, instruction, a);
}

static uint32_t ui_minmax(GeomUi *u, unsigned instruction, uint32_t a, uint32_t b)
{
    return value(u->w, SpvOpExtInst, u->ft, 4, u->ext, instruction, a, b);
}

static uint32_t ui_finite(GeomUi *u, uint32_t a)
{
    uint32_t nan = value(u->w, SpvOpIsNan, u->bt, 1, a);
    uint32_t inf = value(u->w, SpvOpIsInf, u->bt, 1, a);
    return value(u->w, SpvOpLogicalNot, u->bt, 1,
        ui_binary(u, SpvOpLogicalOr, nan, inf));
}

static void ui_require(GeomUi *u, uint32_t condition)
{
    u->valid = ui_binary(u, SpvOpLogicalAnd, u->valid, condition);
}

/* Preserve the first qualifying edge and do not divide on an unselected edge. */
static void ui_edge(GeomUi *u, uint32_t condition, uint32_t denominator,
                    const uint32_t delta[2], uint32_t gradient[2])
{
    Writer *w = u->w;
    uint32_t before = u->block, body = id(w), merge = id(w), divided[2];
    op(w, SpvOpSelectionMerge, 2, merge, SpvSelectionControlMaskNone);
    op(w, SpvOpBranchConditional, 3, condition, body, merge);
    op(w, SpvOpLabel, 1, body);
    for (unsigned c = 0; c < 2; c++) {
        divided[c] = ui_binary(u, SpvOpFDiv, delta[c], denominator);
    }
    op(w, SpvOpBranch, 1, merge);
    op(w, SpvOpLabel, 1, merge);
    for (unsigned c = 0; c < 2; c++) {
        gradient[c] = value(w, SpvOpPhi, u->ft, 4,
            gradient[c], before, divided[c], body);
    }
    u->block = merge;
}

static void ui_prepare(GeomUi *u, unsigned count,
                        const uint32_t positions[4], const uint32_t textures[4])
{
    Writer *w = u->w;
    bool precise = w->precise_math;
    w->precise_math = false; /* The reference classifier is not precise. */
    uint32_t p[4][4], t[4][4], low[2], high[2];
    for (unsigned i = 0; i < count; i++) {
        for (unsigned c = 0; c < 4; c++) {
            p[i][c] = component(w, positions[i], u->ft, c);
            t[i][c] = component(w, textures[i], u->ft, c);
        }
    }
    u->valid = ui_binary(u, SpvOpFOrdGreaterThan, t[0][3], u->zero);
    for (unsigned c = 0; c < 2; c++) { low[c] = high[c] = p[0][c]; }
    for (unsigned i = 0; i < count; i++) {
        for (unsigned c = 0; c < 2; c++) {
            low[c] = ui_minmax(u, GLSLstd450FMin, low[c], p[i][c]);
            high[c] = ui_minmax(u, GLSLstd450FMax, high[c], p[i][c]);
            ui_require(u, ui_binary(u, SpvOpFOrdEqual, p[i][c+2], p[0][c+2]));
        }
        ui_require(u, ui_binary(u, SpvOpFOrdEqual, t[i][3], t[0][3]));
        for (unsigned c = 0; c < 4; c++) {
            ui_require(u, ui_finite(u, p[i][c]));
            ui_require(u, ui_finite(u, t[i][c]));
        }
    }
    for (unsigned c = 0; c < 2; c++) {
        uint32_t extent = ui_binary(u, SpvOpFSub, high[c], low[c]);
        ui_require(u, ui_binary(u, SpvOpFOrdGreaterThanEqual, extent, u->one));
        ui_require(u, ui_finite(u, extent));
        for (unsigned i = 0; i < count; i++) {
            ui_require(u, ui_binary(u, SpvOpLogicalOr,
                ui_binary(u, SpvOpFOrdEqual, p[i][c], low[c]),
                ui_binary(u, SpvOpFOrdEqual, p[i][c], high[c])));
        }
    }
    uint32_t before = u->block, body = id(w), merge = id(w);
    op(w, SpvOpSelectionMerge, 2, merge, SpvSelectionControlMaskNone);
    op(w, SpvOpBranchConditional, 3, u->valid, body, merge);
    op(w, SpvOpLabel, 1, body);
    u->block = body;
    uint32_t gradients[2][2] = {{u->zero,u->zero},{u->zero,u->zero}};
    uint32_t found[2] = {u->no,u->no};
    for (unsigned i = 0; i < count; i++) {
        for (unsigned j = i + 1; j < count; j++) {
            uint32_t d[2], delta[2];
            for (unsigned c = 0; c < 2; c++) {
                d[c] = ui_binary(u, SpvOpFSub, p[j][c], p[i][c]);
                delta[c] = ui_binary(u, SpvOpFSub, t[j][c], t[i][c]);
            }
            for (unsigned axis = 0; axis < 2; axis++) {
                uint32_t candidate = ui_binary(u, SpvOpLogicalAnd,
                    ui_binary(u, SpvOpFOrdEqual, d[1-axis], u->zero),
                    ui_binary(u, SpvOpFOrdGreaterThanEqual, ui_ext(u, GLSLstd450FAbs, d[axis]), u->one));
                uint32_t take = ui_binary(u, SpvOpLogicalAnd, candidate,
                    value(w, SpvOpLogicalNot, u->bt, 1, found[axis]));
                ui_edge(u, take, d[axis], delta, gradients[axis]);
                found[axis] = ui_binary(u, SpvOpLogicalOr, found[axis], take);
            }
        }
    }
    ui_require(u, ui_binary(u, SpvOpLogicalAnd, found[0], found[1]));
    ui_require(u, ui_binary(u, SpvOpLogicalOr,
        ui_binary(u, SpvOpLogicalAnd,
            ui_binary(u, SpvOpFOrdEqual, gradients[0][1], u->zero),
            ui_binary(u, SpvOpFOrdEqual, gradients[1][0], u->zero)),
        ui_binary(u, SpvOpLogicalAnd,
            ui_binary(u, SpvOpFOrdEqual, gradients[0][0], u->zero),
            ui_binary(u, SpvOpFOrdEqual, gradients[1][1], u->zero))));
    for (unsigned axis = 0; axis < 2; axis++) {
        for (unsigned c = 0; c < 2; c++) { ui_require(u, ui_finite(u, gradients[axis][c])); }
    }
    if (count == 4) {
        for (unsigned i = 1; i < count; i++) {
            uint32_t opposite = ui_binary(u, SpvOpLogicalAnd,
                ui_binary(u, SpvOpFOrdNotEqual, p[i][0], p[0][0]),
                ui_binary(u, SpvOpFOrdNotEqual, p[i][1], p[0][1]));
            uint32_t affine = u->yes;
            for (unsigned c = 0; c < 2; c++) {
                uint32_t expected = t[0][c];
                for (unsigned axis = 0; axis < 2; axis++) {
                    expected = ui_binary(u, SpvOpFAdd, expected,
                        ui_binary(u, SpvOpFMul, gradients[axis][c],
                            ui_binary(u, SpvOpFSub, p[i][axis], p[0][axis])));
                }
                uint32_t error = ui_ext(u, GLSLstd450FAbs, ui_binary(u, SpvOpFSub, expected, t[i][c]));
                uint32_t tolerance = ui_binary(u, SpvOpFMul, u->epsilon,
                    ui_minmax(u, GLSLstd450FMax, u->one, ui_ext(u, GLSLstd450FAbs, t[i][c])));
                affine = ui_binary(u, SpvOpLogicalAnd, affine,
                    ui_binary(u, SpvOpFOrdLessThanEqual, error, tolerance));
            }
            ui_require(u, ui_binary(u, SpvOpLogicalOr, affine,
                value(w, SpvOpLogicalNot, u->bt, 1, opposite)));
        }
    }
    uint32_t first[2], last[2], result_low[2], result_high[2];
    for (unsigned c = 0; c < 2; c++) {
        first[c] = ui_binary(u, SpvOpFAdd, ui_ext(u, GLSLstd450Ceil,
            ui_binary(u, SpvOpFSub, low[c], u->half)), u->half);
        last[c] = ui_binary(u, SpvOpFSub, ui_ext(u, GLSLstd450Ceil,
            ui_binary(u, SpvOpFSub, high[c], u->half)), u->half);
        ui_require(u, ui_binary(u, SpvOpFOrdLessThanEqual, first[c], last[c]));
    }
    for (unsigned c = 0; c < 2; c++) {
        uint32_t a = t[0][c], b = a;
        for (unsigned axis = 0; axis < 2; axis++) {
            a = ui_binary(u, SpvOpFAdd, a, ui_binary(u, SpvOpFMul,
                gradients[axis][c], ui_binary(u, SpvOpFSub, first[axis], p[0][axis])));
            b = ui_binary(u, SpvOpFAdd, b, ui_binary(u, SpvOpFMul,
                gradients[axis][c], ui_binary(u, SpvOpFSub, last[axis], p[0][axis])));
        }
        ui_require(u, ui_finite(u, a)); ui_require(u, ui_finite(u, b));
        result_low[c] = ui_minmax(u, GLSLstd450FMin, a, b);
        result_high[c] = ui_minmax(u, GLSLstd450FMax, a, b);
    }
    uint32_t end = u->block;
    op(w, SpvOpBranch, 1, merge);
    op(w, SpvOpLabel, 1, merge);
    u->valid = value(w, SpvOpPhi, u->bt, 4, u->no, before, u->valid, end);
    for (unsigned c = 0; c < 2; c++) {
        u->lo[c] = value(w, SpvOpPhi, u->ft, 4, u->zero, before, result_low[c], end);
        u->hi[c] = value(w, SpvOpPhi, u->ft, 4, u->zero, before, result_high[c], end);
    }
    u->block = merge;
    w->precise_math = precise;
}
