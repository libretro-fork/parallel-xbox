/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include "geom.h"

/* The three GLSL.std.450 instructions used by triangle depth slopes. */
enum { GLSLstd450FAbs = 4, GLSLstd450FMax = 40, GLSLstd450Fma = 50 };

/* The same deterministic emitter first counts, then fills the final owned
 * allocation. Counting repeats instruction construction, not shader parsing.
 * Nothing is reallocated or concatenated while building a module. */
typedef struct Writer {
    uint32_t *words;
    size_t count, capacity;
    uint32_t next_id;
    bool precise_math;
    uint32_t *no_contraction;
    unsigned num_no_contraction;
    const struct Writer *annotations;
} Writer;

static void word(Writer *w, uint32_t value)
{
    if (w->words) {
        if (w->count >= w->capacity) {
            abort();
        }
        w->words[w->count] = value;
    }
    w->count++;
}

static uint32_t id(Writer *w)
{
    return w->next_id++;
}

static void op(Writer *w, SpvOp opcode, unsigned operands, ...)
{
    word(w, ((operands + 1) << 16) | opcode);
    va_list args;
    va_start(args, operands);
    for (unsigned i = 0; i < operands; i++) {
        word(w, va_arg(args, uint32_t));
    }
    va_end(args);
}

static uint32_t value(Writer *w, SpvOp opcode, uint32_t type,
                      unsigned operands, ...)
{
    uint32_t result = id(w);
    if (w->precise_math && !w->words &&
        (opcode == SpvOpFAdd || opcode == SpvOpFSub ||
         opcode == SpvOpFMul || opcode == SpvOpFDiv)) {
        if (w->num_no_contraction == 64) {
            abort();
        }
        w->no_contraction[w->num_no_contraction++] = result;
        /* Count annotations here; the write pass places them before types. */
        w->count += 3;
    }
    word(w, ((operands + 3) << 16) | opcode);
    word(w, type);
    word(w, result);
    va_list args;
    va_start(args, operands);
    for (unsigned i = 0; i < operands; i++) {
        word(w, va_arg(args, uint32_t));
    }
    va_end(args);
    return result;
}

static void location(Writer *w, uint32_t variable, unsigned loc, bool flat)
{
    op(w, SpvOpDecorate, 3, variable, SpvDecorationLocation, loc);
    if (flat) {
        op(w, SpvOpDecorate, 2, variable, SpvDecorationFlat);
    }
}

static uint32_t load_at(Writer *w, uint32_t variable, uint32_t index,
                        uint32_t pointer_type, uint32_t type)
{
    uint32_t pointer = value(w, SpvOpAccessChain, pointer_type, 2,
                             variable, index);
    return value(w, SpvOpLoad, type, 1, pointer);
}

static uint32_t component(Writer *w, uint32_t vector, uint32_t type,
                          unsigned index)
{
    return value(w, SpvOpCompositeExtract, type, 2, vector, index);
}

/* Kahan determinant: preserve each rounded product and use explicit FMA. */
static uint32_t determinant(Writer *w, uint32_t ft, uint32_t ext,
                            uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t cd = value(w, SpvOpFMul, ft, 2, c, d);
    uint32_t nc = value(w, SpvOpFNegate, ft, 1, c);
    uint32_t err = value(w, SpvOpExtInst, ft, 5, ext, (uint32_t)GLSLstd450Fma, nc, d, cd);
    uint32_t ncd = value(w, SpvOpFNegate, ft, 1, cd);
    uint32_t res = value(w, SpvOpExtInst, ft, 5, ext, (uint32_t)GLSLstd450Fma, a, b, ncd);
    return value(w, SpvOpFAdd, ft, 2, res, err);
}

static void emit_geom(Writer *w, SpvLineGeomOptions opts,
                       const SpvTriangleGeomOptions *tri)
{
    bool parity_needed = tri && (tri->rotation_even != tri->rotation_odd ||
                         (!opts.smooth_shading && tri->provoking_even != tri->provoking_odd));
    uint32_t main_id = id(w), void_t = id(w), float_t = id(w), uint_t = id(w);
    uint32_t vec4_t = id(w), fn_t = id(w), zero = id(w), one = id(w);
    uint32_t two = id(w), fzero = id(w), pervertex_t = id(w);
    uint32_t in_vec4_array_t = id(w), in_float_array_t = id(w);
    uint32_t in_pervertex_array_t = id(w);
    uint32_t in_vec4_array_ptr = id(w), in_float_array_ptr = id(w);
    uint32_t in_pervertex_array_ptr = id(w), out_pervertex_ptr = id(w);
    uint32_t in_vec4_ptr = id(w), in_float_ptr = id(w);
    uint32_t out_vec4_ptr = id(w), out_float_ptr = id(w);
    uint32_t bool_t = 0, three = 0, in_uint_ptr = 0, primitive = 0, ext = 0;
    if (tri) {
        bool_t = id(w); three = id(w); ext = id(w);
        if (parity_needed) { in_uint_ptr = id(w); primitive = id(w); }
    }
    uint32_t inputs[10], outputs[13];
    for (unsigned i = 0; i < 10; i++) {
        inputs[i] = id(w);
    }
    for (unsigned i = 0; i < 13; i++) {
        outputs[i] = id(w);
    }
    uint32_t gl_in = id(w), gl_out = id(w);

    word(w, SpvMagicNumber);
    word(w, opts.spirv_1_6 ? 0x00010600 : 0x00010500);
    word(w, 0);
    word(w, 0); /* bound patched after all result IDs are known */
    word(w, 0);
    op(w, SpvOpCapability, 1, SpvCapabilityShader);
    op(w, SpvOpCapability, 1, SpvCapabilityGeometry);
    op(w, SpvOpCapability, 1, SpvCapabilityGeometryPointSize);
    if (tri) {
        op(w, SpvOpExtInstImport, 5, ext, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0u);
    }
    op(w, SpvOpMemoryModel, 2, SpvAddressingModelLogical, SpvMemoryModelGLSL450);

    /* "main" occupies two words including its terminator. */
    word(w, ((parity_needed ? 31u : 30u) << 16) | SpvOpEntryPoint);
    word(w, SpvExecutionModelGeometry);
    word(w, main_id);
    word(w, 0x6e69616d);
    word(w, 0);
    for (unsigned i = 0; i < 10; i++) {
        word(w, inputs[i]);
    }
    for (unsigned i = 0; i < 13; i++) {
        word(w, outputs[i]);
    }
    word(w, gl_in);
    word(w, gl_out);
    if (parity_needed) { word(w, primitive); }
    op(w, SpvOpExecutionMode, 2, main_id, tri ? SpvExecutionModeTriangles : SpvExecutionModeInputLines);
    op(w, SpvOpExecutionMode, 2, main_id, tri ? SpvExecutionModeOutputTriangleStrip : SpvExecutionModeOutputLineStrip);
    op(w, SpvOpExecutionMode, 3, main_id, SpvExecutionModeOutputVertices, tri ? 3u : 2u);
    op(w, SpvOpExecutionMode, 3, main_id, SpvExecutionModeInvocations, 1u);

    op(w, SpvOpDecorate, 2, pervertex_t, SpvDecorationBlock);
    op(w, SpvOpMemberDecorate, 4, pervertex_t, 0u,
       SpvDecorationBuiltIn, SpvBuiltInPosition);
    op(w, SpvOpMemberDecorate, 4, pervertex_t, 1u,
       SpvDecorationBuiltIn, SpvBuiltInPointSize);
    for (unsigned i = 0; i < 10; i++) {
        location(w, inputs[i], i, i == 9 || (i < 4 && !opts.smooth_shading));
    }
    for (unsigned i = 0; i < 13; i++) {
        location(w, outputs[i], i, i >= 9 || (i < 4 && !opts.smooth_shading));
    }

    if (parity_needed) {
        op(w, SpvOpDecorate, 3, primitive, SpvDecorationBuiltIn, SpvBuiltInPrimitiveId);
    }
    if (w->annotations) {
        for (unsigned i = 0; i < w->annotations->num_no_contraction; i++) {
            op(w, SpvOpDecorate, 2, w->annotations->no_contraction[i], SpvDecorationNoContraction);
        }
    }
    op(w, SpvOpTypeVoid, 1, void_t);
    op(w, SpvOpTypeFloat, 2, float_t, 32u);
    op(w, SpvOpTypeInt, 3, uint_t, 32u, 0u);
    op(w, SpvOpTypeVector, 3, vec4_t, float_t, 4u);
    if (tri) {
        op(w, SpvOpTypeBool, 1, bool_t);
        if (parity_needed) {
            op(w, SpvOpTypePointer, 3, in_uint_ptr, SpvStorageClassInput, uint_t);
        }
    }
    op(w, SpvOpTypeFunction, 2, fn_t, void_t);
    op(w, SpvOpConstant, 3, uint_t, zero, 0u);
    op(w, SpvOpConstant, 3, uint_t, one, 1u);
    op(w, SpvOpConstant, 3, uint_t, two, 2u);
    op(w, SpvOpConstant, 3, float_t, fzero, 0u);
    if (tri) { op(w, SpvOpConstant, 3, uint_t, three, 3u); }
    op(w, SpvOpTypeStruct, 3, pervertex_t, vec4_t, float_t);
    op(w, SpvOpTypeArray, 3, in_vec4_array_t, vec4_t, tri ? three : two);
    op(w, SpvOpTypeArray, 3, in_float_array_t, float_t, tri ? three : two);
    op(w, SpvOpTypeArray, 3, in_pervertex_array_t, pervertex_t, tri ? three : two);
    op(w, SpvOpTypePointer, 3, in_vec4_array_ptr, SpvStorageClassInput, in_vec4_array_t);
    op(w, SpvOpTypePointer, 3, in_float_array_ptr, SpvStorageClassInput, in_float_array_t);
    op(w, SpvOpTypePointer, 3, in_pervertex_array_ptr, SpvStorageClassInput, in_pervertex_array_t);
    op(w, SpvOpTypePointer, 3, out_pervertex_ptr, SpvStorageClassOutput, pervertex_t);
    op(w, SpvOpTypePointer, 3, in_vec4_ptr, SpvStorageClassInput, vec4_t);
    op(w, SpvOpTypePointer, 3, in_float_ptr, SpvStorageClassInput, float_t);
    op(w, SpvOpTypePointer, 3, out_vec4_ptr, SpvStorageClassOutput, vec4_t);
    op(w, SpvOpTypePointer, 3, out_float_ptr, SpvStorageClassOutput, float_t);
    for (unsigned i = 0; i < 10; i++) {
        op(w, SpvOpVariable, 3, i == 4 ? in_float_array_ptr : in_vec4_array_ptr,
           inputs[i], SpvStorageClassInput);
    }
    for (unsigned i = 0; i < 13; i++) {
        op(w, SpvOpVariable, 3, i == 4 || i == 12 ? out_float_ptr : out_vec4_ptr,
           outputs[i], SpvStorageClassOutput);
    }
    op(w, SpvOpVariable, 3, in_pervertex_array_ptr, gl_in, SpvStorageClassInput);
    op(w, SpvOpVariable, 3, out_pervertex_ptr, gl_out, SpvStorageClassOutput);
    if (parity_needed) { op(w, SpvOpVariable, 3, in_uint_ptr, primitive, SpvStorageClassInput); }
    op(w, SpvOpFunction, 4, void_t, main_id, SpvFunctionControlMaskNone, fn_t);
    op(w, SpvOpLabel, 1, id(w));

    uint32_t indices[3] = {zero, one, two};
    uint32_t p0, p1, p2, dz = fzero, flat_index = opts.provoking_vertex ? one : zero;
    if (tri) {
        uint32_t even = 0;
        if (parity_needed) {
            uint32_t pid = value(w, SpvOpLoad, uint_t, 1, primitive);
            uint32_t parity = value(w, SpvOpBitwiseAnd, uint_t, 2, pid, one);
            even = value(w, SpvOpIEqual, bool_t, 2, parity, zero);
        }
        const unsigned rotations[3][3] = {{0,1,2}, {2,0,1}, {1,2,0}};
        const uint32_t constants[3] = {zero, one, two};
        for (unsigned i = 0; i < 3; i++) {
            indices[i] = constants[rotations[tri->rotation_even][i]];
            if (tri->rotation_even != tri->rotation_odd) {
                indices[i] = value(w, SpvOpSelect, uint_t, 3, even, indices[i],
                                   constants[rotations[tri->rotation_odd][i]]);
            }
        }
        flat_index = indices[tri->provoking_even];
        if (!opts.smooth_shading && tri->provoking_even != tri->provoking_odd) {
            flat_index = value(w, SpvOpSelect, uint_t, 3, even, flat_index,
                               indices[tri->provoking_odd]);
        }
        p0 = load_at(w, inputs[9], indices[0], in_vec4_ptr, vec4_t);
        p1 = load_at(w, inputs[9], indices[1], in_vec4_ptr, vec4_t);
        p2 = load_at(w, inputs[9], indices[2], in_vec4_ptr, vec4_t);
        uint32_t x[3], y[3], z[3];
        const uint32_t positions[3] = {p0, p1, p2};
        for (unsigned i = 0; i < 3; i++) {
            x[i] = component(w, positions[i], float_t, 0);
            y[i] = component(w, positions[i], float_t, 1);
            z[i] = component(w, positions[i], float_t, tri->z_perspective ? 3 : 2);
        }
        uint32_t ax = value(w, SpvOpFSub, float_t, 2, x[1], x[0]);
        uint32_t ay = value(w, SpvOpFSub, float_t, 2, y[1], y[0]);
        uint32_t bx = value(w, SpvOpFSub, float_t, 2, x[2], x[0]);
        uint32_t by = value(w, SpvOpFSub, float_t, 2, y[2], y[0]);
        uint32_t u = value(w, SpvOpFSub, float_t, 2,
                           z[tri->z_perspective ? 0 : 1], z[tri->z_perspective ? 1 : 0]);
        uint32_t v = value(w, SpvOpFSub, float_t, 2,
                           z[tri->z_perspective ? 0 : 2], z[tri->z_perspective ? 2 : 0]);
        if (tri->z_perspective) {
            uint32_t uw = value(w, SpvOpFMul, float_t, 2, z[1], z[0]);
            uint32_t vw = value(w, SpvOpFMul, float_t, 2, z[2], z[0]);
            u = value(w, SpvOpFDiv, float_t, 2, u, uw);
            v = value(w, SpvOpFDiv, float_t, 2, v, vw);
        }
        uint32_t det = determinant(w, float_t, ext, ax, by, bx, ay);
        uint32_t nx = determinant(w, float_t, ext, u, by, v, ay);
        uint32_t ny = determinant(w, float_t, ext, v, ax, u, bx);
        uint32_t sx = value(w, SpvOpFDiv, float_t, 2, nx, det);
        uint32_t sy = value(w, SpvOpFDiv, float_t, 2, ny, det);
        sx = value(w, SpvOpExtInst, float_t, 3, ext, (uint32_t)GLSLstd450FAbs, sx);
        sy = value(w, SpvOpExtInst, float_t, 3, ext, (uint32_t)GLSLstd450FAbs, sy);
        dz = value(w, SpvOpExtInst, float_t, 4, ext, (uint32_t)GLSLstd450FMax, sx, sy);
        uint32_t nan = value(w, SpvOpIsNan, bool_t, 1, dz);
        uint32_t inf = value(w, SpvOpIsInf, bool_t, 1, dz);
        uint32_t invalid = value(w, SpvOpLogicalOr, bool_t, 2, nan, inf);
        dz = value(w, SpvOpSelect, float_t, 3, invalid, fzero, dz);
    } else {
        p0 = load_at(w, inputs[9], zero, in_vec4_ptr, vec4_t);
        p1 = load_at(w, inputs[9], one, in_vec4_ptr, vec4_t);
        uint32_t x0 = component(w, p0, float_t, 0);
        uint32_t y0 = component(w, p0, float_t, 1);
        uint32_t x1 = component(w, p1, float_t, 0);
        uint32_t y1 = component(w, p1, float_t, 1);
        uint32_t z0 = component(w, p0, float_t, 2);
        uint32_t w0 = component(w, p0, float_t, 3);
        uint32_t dx = value(w, SpvOpFSub, float_t, 2, x1, x0);
        uint32_t dy = value(w, SpvOpFSub, float_t, 2, y1, y0);
        uint32_t neg_dy = value(w, SpvOpFNegate, float_t, 1, dy);
        uint32_t x2 = value(w, SpvOpFAdd, float_t, 2, neg_dy, x0);
        uint32_t y2 = value(w, SpvOpFAdd, float_t, 2, dx, y0);
        p2 = value(w, SpvOpCompositeConstruct, vec4_t, 4, x2, y2, z0, w0);
    }
    for (unsigned vertex = 0; vertex < (tri ? 3u : 2u); vertex++) {
        uint32_t index = indices[vertex];
        uint32_t colour_index = opts.smooth_shading ? index : flat_index;
        for (unsigned member = 0; member < 2; member++) {
            uint32_t type = member ? float_t : vec4_t;
            uint32_t src = value(w, SpvOpAccessChain,
                                 member ? in_float_ptr : in_vec4_ptr, 3,
                                 gl_in, index, member ? one : zero);
            uint32_t data = value(w, SpvOpLoad, type, 1, src);
            uint32_t dst = value(w, SpvOpAccessChain,
                                 member ? out_float_ptr : out_vec4_ptr, 2,
                                 gl_out, member ? one : zero);
            op(w, SpvOpStore, 2, dst, data);
        }
        for (unsigned i = 0; i < 9; i++) {
            uint32_t data = load_at(w, inputs[i], i < 4 ? colour_index : index,
                                    i == 4 ? in_float_ptr : in_vec4_ptr,
                                    i == 4 ? float_t : vec4_t);
            op(w, SpvOpStore, 2, outputs[i], data);
        }
        op(w, SpvOpStore, 2, outputs[9], p0);
        op(w, SpvOpStore, 2, outputs[10], p1);
        op(w, SpvOpStore, 2, outputs[11], p2);
        op(w, SpvOpStore, 2, outputs[12], dz);
        op(w, SpvOpEmitVertex, 0);
    }
    op(w, SpvOpEndPrimitive, 0);
    op(w, SpvOpReturn, 0);
    op(w, SpvOpFunctionEnd, 0);
    if (w->words) {
        w->words[3] = w->next_id;
    }
}

bool pgraph_spv_line_geom(SpvLineGeomOptions opts, SpvDirectModule *out)
{
    memset(out, 0, sizeof(*out));
    if (opts.provoking_vertex > 1) {
        return false;
    }
    Writer count = { .next_id = 1 };
    emit_geom(&count, opts, NULL);
    uint32_t *words = malloc(count.count * sizeof(*words));
    if (!words) {
        abort();
    }
    Writer output = { .words = words, .capacity = count.count, .next_id = 1 };
    emit_geom(&output, opts, NULL);
    if (output.count != count.count) {
        abort();
    }
    out->words = words;
    out->num_words = output.count;
    return true;
}

bool pgraph_spv_triangle_geom(SpvTriangleGeomOptions opts, SpvDirectModule *out)
{
    memset(out, 0, sizeof(*out));
    if (opts.rotation_even > 2 || opts.rotation_odd > 2 ||
        opts.provoking_even > 2 || opts.provoking_odd > 2) {
        return false;
    }
    SpvLineGeomOptions common = {
        .smooth_shading = opts.smooth_shading, .spirv_1_6 = opts.spirv_1_6,
    };
    uint32_t annotations[64];
    Writer count = { .next_id = 1, .precise_math = true,
                     .no_contraction = annotations };
    emit_geom(&count, common, &opts);
    uint32_t *words = malloc(count.count * sizeof(*words));
    if (!words) { abort(); }
    Writer output = { .words = words, .capacity = count.count, .next_id = 1,
                      .precise_math = true, .annotations = &count };
    emit_geom(&output, common, &opts);
    if (output.count != count.count) { abort(); }
    out->words = words;
    out->num_words = output.count;
    return true;
}
