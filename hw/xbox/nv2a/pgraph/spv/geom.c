/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include "geom.h"

/* The same deterministic emitter first counts, then fills the final owned
 * allocation. Counting repeats instruction construction, not shader parsing.
 * Nothing is reallocated or concatenated while building a module. */
typedef struct Writer {
    uint32_t *words;
    size_t count, capacity;
    uint32_t next_id;
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

static void emit_line(Writer *w, SpvLineGeomOptions opts)
{
    uint32_t main_id = id(w), void_t = id(w), float_t = id(w), uint_t = id(w);
    uint32_t vec4_t = id(w), fn_t = id(w), zero = id(w), one = id(w);
    uint32_t two = id(w), fzero = id(w), pervertex_t = id(w);
    uint32_t in_vec4_array_t = id(w), in_float_array_t = id(w);
    uint32_t in_pervertex_array_t = id(w);
    uint32_t in_vec4_array_ptr = id(w), in_float_array_ptr = id(w);
    uint32_t in_pervertex_array_ptr = id(w), out_pervertex_ptr = id(w);
    uint32_t in_vec4_ptr = id(w), in_float_ptr = id(w);
    uint32_t out_vec4_ptr = id(w), out_float_ptr = id(w);
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
    op(w, SpvOpMemoryModel, 2, SpvAddressingModelLogical, SpvMemoryModelGLSL450);

    /* "main" occupies two words including its terminator. */
    word(w, (30u << 16) | SpvOpEntryPoint);
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
    op(w, SpvOpExecutionMode, 2, main_id, SpvExecutionModeInputLines);
    op(w, SpvOpExecutionMode, 2, main_id, SpvExecutionModeOutputLineStrip);
    op(w, SpvOpExecutionMode, 3, main_id, SpvExecutionModeOutputVertices, 2u);
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

    op(w, SpvOpTypeVoid, 1, void_t);
    op(w, SpvOpTypeFloat, 2, float_t, 32u);
    op(w, SpvOpTypeInt, 3, uint_t, 32u, 0u);
    op(w, SpvOpTypeVector, 3, vec4_t, float_t, 4u);
    op(w, SpvOpTypeFunction, 2, fn_t, void_t);
    op(w, SpvOpConstant, 3, uint_t, zero, 0u);
    op(w, SpvOpConstant, 3, uint_t, one, 1u);
    op(w, SpvOpConstant, 3, uint_t, two, 2u);
    op(w, SpvOpConstant, 3, float_t, fzero, 0u);
    op(w, SpvOpTypeStruct, 3, pervertex_t, vec4_t, float_t);
    op(w, SpvOpTypeArray, 3, in_vec4_array_t, vec4_t, two);
    op(w, SpvOpTypeArray, 3, in_float_array_t, float_t, two);
    op(w, SpvOpTypeArray, 3, in_pervertex_array_t, pervertex_t, two);
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
    op(w, SpvOpFunction, 4, void_t, main_id, SpvFunctionControlMaskNone, fn_t);
    op(w, SpvOpLabel, 1, id(w));

    uint32_t p0 = load_at(w, inputs[9], zero, in_vec4_ptr, vec4_t);
    uint32_t p1 = load_at(w, inputs[9], one, in_vec4_ptr, vec4_t);
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
    uint32_t p2 = value(w, SpvOpCompositeConstruct, vec4_t, 4, x2, y2, z0, w0);
    for (unsigned vertex = 0; vertex < 2; vertex++) {
        uint32_t index = vertex ? one : zero;
        uint32_t colour_index = opts.smooth_shading ? index :
                                opts.provoking_vertex ? one : zero;
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
        op(w, SpvOpStore, 2, outputs[12], fzero);
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
    emit_line(&count, opts);
    uint32_t *words = malloc(count.count * sizeof(*words));
    if (!words) {
        abort();
    }
    Writer output = { .words = words, .capacity = count.count, .next_id = 1 };
    emit_line(&output, opts);
    if (output.count != count.count) {
        abort();
    }
    out->words = words;
    out->num_words = output.count;
    return true;
}
