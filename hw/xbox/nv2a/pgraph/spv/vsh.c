/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
/* GLSL.std.450 instruction numbers, matching the native geometry emitter. */
enum {
    GLSLstd450Trunc = 3, GLSLstd450FAbs = 4, GLSLstd450Floor = 8,
    GLSLstd450Exp2 = 29, GLSLstd450Log2 = 30, GLSLstd450Sqrt = 31, GLSLstd450InverseSqrt = 32,
    GLSLstd450FMin = 37, GLSLstd450FMax = 40, GLSLstd450FClamp = 43,
    GLSLstd450Length = 66, GLSLstd450Normalize = 69, GLSLstd450Reflect = 71,
};
#include "vsh.h"

_Static_assert(NV2A_VERTEXSHADER_CONSTANTS == 192, "review direct UBO layout");
_Static_assert(NV2A_VERTEXSHADER_ATTRIBUTES == 16, "review direct input layout");

/* Native microcode -> words. The count pass and write pass borrow the same
 * immutable key. Only the final words and layout allocations escape. */
typedef struct Writer {
    uint32_t *words, next;
    size_t count, capacity;
    uint32_t precise_mul[4];
    unsigned precise_count;
} Writer;

static uint32_t id(Writer *w) { return w->next++; }
static void word(Writer *w, uint32_t value)
{
    if (w->words) {
        if (w->count >= w->capacity) { abort(); }
        w->words[w->count] = value;
    }
    w->count++;
}
static void op(Writer *w, SpvOp code, unsigned n, ...)
{
    word(w, ((n + 1) << 16) | code);
    va_list args;
    va_start(args, n);
    for (unsigned i = 0; i < n; i++) { word(w, va_arg(args, uint32_t)); }
    va_end(args);
}
static uint32_t value(Writer *w, SpvOp code, uint32_t type, unsigned n, ...)
{
    uint32_t result = id(w);
    word(w, ((n + 3) << 16) | code);
    word(w, type); word(w, result);
    va_list args;
    va_start(args, n);
    for (unsigned i = 0; i < n; i++) { word(w, va_arg(args, uint32_t)); }
    va_end(args);
    return result;
}

enum FloatConstant {
    F_ZERO, F_ONE, F_TWO, F_SIXTEEN, F_THIRTYTWO, F_ONEHALF,
    F_MAX, F_NEGMAX, F_HUGE, F_TINY, F_NEGHUGE, F_NEGTINY,
    F_INF, F_NEGINF, F_ARLBIAS, F_LITMAX, F_NEGLITMAX, F_1023, F_511,
    F_COUNT
};
static const float floats[F_COUNT] = {
    0.0f, 1.0f, 2.0f, 16.0f, 32.0f, 1.5f,
    0x1.fffffep127f, -0x1.fffffep127f, 0x1p64f, 0x1p-64f,
    -0x1p64f, -0x1p-64f,
    __builtin_inff(), -__builtin_inff(), .001f,
    127.99609375f, -127.99609375f, 1023.0f, 511.0f
};
static const uint8_t mac_params[14] = {
    0, 1, 3, 5, 7, 3, 3, 3, 3, 3, 3, 3, 3, 1
};
static const uint8_t fog_masks[16] = {
    0, 8, 8, 12, 8, 12, 12, 14, 8, 12, 12, 14, 12, 14, 14, 15
};
static unsigned field(uint32_t v, unsigned shift, unsigned bits)
{ return (v >> shift) & ((1u << bits) - 1); }
static unsigned mac(const uint32_t *t) { return field(t[1], 21, 4); }
static unsigned ilu(const uint32_t *t) { return field(t[1], 25, 3); }
static unsigned mux(const uint32_t *t, unsigned arg)
{
    return arg == 0 ? field(t[2], 26, 2) :
           arg == 1 ? field(t[2], 11, 2) : field(t[3], 28, 2);
}
static unsigned reg(const uint32_t *t, unsigned arg)
{
    return arg == 0 ? field(t[2], 28, 4) : arg == 1 ? field(t[2], 13, 4) :
           (field(t[2], 0, 2) << 2) | field(t[3], 30, 2);
}

bool pgraph_spv_vsh_supported(const VshState *s, SpvVshOptions opts)
{
    /* Point scaling and the unlit matrix path use an explicit FP32 environment.
     * Device support is queried before worker startup, never on the draw path. */
    if ((s->point_params_enable && !opts.preserve_float32) ||
        opts.ubo_binding >= 32 ||
        (s->fog_enable && !s->fog_dynamic &&
         ((unsigned)s->fog_mode >= 8 || s->fog_mode == FOG_MODE_ERROR2 ||
          s->fog_mode == FOG_MODE_ERROR6)) ||
        (s->compressed_attrs & s->uniform_attrs) ||
        (s->swizzle_attrs & (s->uniform_attrs | s->compressed_attrs))) {
        return false;
    }
    if (s->is_fixed_function) {
        const FixedFunctionVshState *ff = &s->fixed_function;
        if (!opts.preserve_float32 ||
            (s->fog_enable && !s->fog_dynamic && (unsigned)ff->foggen > FOGGEN_FOG_X) ||
            ff->lighting || ff->csv0c_dynamic ||
            (unsigned)ff->skinning > SKINNING_4WEIGHTS4MATRICES) {
            return false;
        }
        for (unsigned t = 0; !ff->texgen_dynamic && t < 4; t++) {
            for (unsigned a = 0; a < 4; a++) {
                enum VshTexgen mode = ff->texgen[t][a];
                if ((unsigned)mode > TEXGEN_OBJECT_LINEAR &&
                    !(mode == TEXGEN_SPHERE_MAP && a < 2) &&
                    !((mode == TEXGEN_NORMAL_MAP || mode == TEXGEN_REFLECTION_MAP) && a < 3)) { return false; }
            }
        }
        return true;
    }
    if (s->programmable.program_length <= 0 ||
        s->programmable.program_length > NV2A_MAX_TRANSFORM_PROGRAM_LENGTH) {
        return false;
    }
    for (int i = 0; i < s->programmable.program_length; i++) {
        const uint32_t *t = s->programmable.program_data[i];
        unsigned m = mac(t), l = ilu(t), output = field(t[3], 3, 4);
        if (m >= 14) { return false; }
        if (m || l) {
            /* The established decoder forms C even for an unused operand. */
            unsigned used = (m ? mac_params[m] : 0) | 4;
            for (unsigned a = 0; a < 3; a++) {
                if (!(used & (1u << a))) { continue; }
                unsigned source = mux(t, a);
                if (!source || (source == PARAM_R && reg(t, a) > 12) ||
                    (source == PARAM_C && field(t[1], 13, 8) >=
                                             NV2A_VERTEXSHADER_CONSTANTS)) {
                    return false;
                }
            }
            if (field(t[3], 12, 4)) {
                if (!field(t[3], 11, 1) || output == 1 || output == 2 ||
                    output > 12 || (m == MAC_ARL && !field(t[3], 2, 1))) {
                    return false;
                }
            }
            unsigned r = field(t[3], 20, 4);
            if (r > 12 && ((m && m != MAC_ARL && field(t[3], 24, 4)) ||
                          (!m && l && field(t[3], 16, 4)))) {
                return false;
            }
        }
        if (t[3] & 1) { return true; }
    }
    return false;
}

static bool needs_precise_fog(const VshState *s)
{
    return s->is_fixed_function && (s->fog_dynamic ||
        (s->fog_enable && (s->fog_mode == FOG_MODE_EXP2 || s->fog_mode == FOG_MODE_EXP2_ABS) &&
         s->fixed_function.foggen >= FOGGEN_RADIAL && s->fixed_function.foggen <= FOGGEN_ABS_PLANAR));
}

/* The existing texgenMode uniform carries TEXGEN_REF even for static modes. */
static bool needs_texgen_viewer(const VshState *s)
{
    if (!s->is_fixed_function) { return false; }
    if (s->fixed_function.texgen_dynamic) { return true; }
    for (unsigned t = 0; t < 4; t++) {
        for (unsigned a = 0; a < 3; a++) {
            enum VshTexgen mode = s->fixed_function.texgen[t][a];
            if (mode == TEXGEN_SPHERE_MAP || mode == TEXGEN_REFLECTION_MAP) { return true; }
        }
    }
    return false;
}

typedef struct Context {
    Writer *w;
    const VshState *state;
    SpvVshOptions opts;
    uint32_t void_t, fn_t, f, v2, v3, v4, b, b4, i, u;
    uint32_t in_v4, in_i, out_v4, out_f, uniform_v4, uniform_v2;
    uint32_t uniform_f, uniform_u, push_v4;
    uint32_t c_array, inline_array, push_array, ubo_t, pc_t, ubo_p, pc_p;
    uint32_t ci[256], cu[8], cf[F_COUNT], zero4, initial4;
    uint32_t inputs[16], outputs[13], position, point_size, ubo, pc;
    uint32_t main, ext, mat4, texgen_array, viewer_mask, point_array, point_limit, texgen_half, texgen_shift9, texgen_bit8;
    uint32_t r[12], o[13], a0, input_values[16], skin_weights[4], fog_control, fog_distance;
} Context;

static uint32_t unary(Context *c, SpvOp code, uint32_t type, uint32_t a)
{ return value(c->w, code, type, 1, a); }
static uint32_t binary(Context *c, SpvOp code, uint32_t type, uint32_t a, uint32_t b)
{ return value(c->w, code, type, 2, a, b); }
static uint32_t ext1(Context *c, unsigned code, uint32_t type, uint32_t a)
{ return value(c->w, SpvOpExtInst, type, 3, c->ext, code, a); }
static uint32_t ext2(Context *c, unsigned code, uint32_t type, uint32_t a, uint32_t b)
{ return value(c->w, SpvOpExtInst, type, 4, c->ext, code, a, b); }
static uint32_t clamp(Context *c, uint32_t type, uint32_t a, uint32_t lo, uint32_t hi)
{ return value(c->w, SpvOpExtInst, type, 5, c->ext, GLSLstd450FClamp, a, lo, hi); }
static uint32_t choose(Context *c, uint32_t type, uint32_t test, uint32_t a, uint32_t b)
{ return value(c->w, SpvOpSelect, type, 3, test, a, b); }
static uint32_t extract(Context *c, uint32_t v, unsigned component)
{ return value(c->w, SpvOpCompositeExtract, c->f, 2, v, component); }
static uint32_t make_v4(Context *c, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{ return value(c->w, SpvOpCompositeConstruct, c->v4, 4, x, y, z, w); }
static uint32_t splat(Context *c, uint32_t x) { return make_v4(c, x, x, x, x); }
static uint32_t add(Context *c, uint32_t a, uint32_t b)
{ return binary(c, SpvOpFAdd, c->f, a, b); }
static uint32_t mul(Context *c, uint32_t a, uint32_t b)
{ return binary(c, SpvOpFMul, c->f, a, b); }
static uint32_t sub(Context *c, uint32_t a, uint32_t b)
{ return binary(c, SpvOpFSub, c->f, a, b); }
static uint32_t divf(Context *c, uint32_t a, uint32_t b)
{ return binary(c, SpvOpFDiv, c->f, a, b); }
static uint32_t squared_fog(Context *c, uint32_t distance, uint32_t density)
{
    uint32_t products[4];
    products[0] = mul(c, unary(c, SpvOpFNegate, c->f, distance), distance);
    products[1] = mul(c, products[0], density);
    products[2] = mul(c, products[1], density);
    products[3] = mul(c, products[2], c->cf[F_THIRTYTWO]);
    if (needs_precise_fog(c->state)) {
        for (unsigned i = 0; i < 4; i++) {
            if (c->w->precise_count >= 4) { abort(); }
            unsigned index = c->w->precise_count++;
            if (c->w->words && c->w->precise_mul[index] != products[i]) { abort(); }
            c->w->precise_mul[index] = products[i];
        }
    }
    return products[3];
}

static uint32_t load_uniform(Context *c, unsigned member, uint32_t type, uint32_t ptr)
{
    uint32_t p = value(c->w, SpvOpAccessChain, ptr, 2, c->ubo, c->ci[member]);
    return unary(c, SpvOpLoad, type, p);
}
static uint32_t point_parameter(Context *c, unsigned member, unsigned index)
{
    uint32_t p = value(c->w, SpvOpAccessChain, c->uniform_f, 3,
                       c->ubo, c->ci[member], c->ci[index]);
    return unary(c, SpvOpLoad, c->f, p);
}
static uint32_t read_reg(Context *c, unsigned n) { return n == 12 ? c->o[0] : c->r[n]; }
static void write_reg(Context *c, unsigned n, uint32_t v)
{ if (n == 12) { c->o[0] = v; } else { c->r[n] = v; } }
static uint32_t masked(Context *c, uint32_t old, uint32_t v, unsigned mask)
{
    uint32_t a[4];
    for (unsigned i = 0; i < 4; i++) {
        a[i] = extract(c, mask & (8u >> i) ? v : old, i);
    }
    return make_v4(c, a[0], a[1], a[2], a[3]);
}

static uint32_t input(Context *c, unsigned index)
{
    if (c->input_values[index]) { return c->input_values[index]; }
    uint32_t result;
    if (c->state->uniform_attrs & (1u << index)) {
        if (c->opts.push_constants) {
            unsigned slot = __builtin_popcount(c->state->uniform_attrs & ((1u << index) - 1));
            uint32_t p = value(c->w, SpvOpAccessChain, c->push_v4, 3,
                               c->pc, c->ci[0], c->ci[slot]);
            result = unary(c, SpvOpLoad, c->v4, p);
        } else {
            unsigned slot = __builtin_popcount(c->state->uniform_attrs & ((1u << index) - 1));
            uint32_t p = value(c->w, SpvOpAccessChain, c->uniform_v4, 3,
                               c->ubo, c->ci[7], c->ci[slot]);
            result = unary(c, SpvOpLoad, c->v4, p);
        }
    } else if (c->state->compressed_attrs & (1u << index)) {
        uint32_t raw = unary(c, SpvOpLoad, c->i, c->inputs[index]);
        uint32_t a[3];
        for (unsigned j = 0; j < 3; j++) {
            uint32_t bits = value(c->w, SpvOpBitFieldSExtract, c->i, 3,
                                  raw, c->ci[j * 11], c->ci[j == 2 ? 10 : 11]);
            a[j] = divf(c, unary(c, SpvOpConvertSToF, c->f, bits),
                         c->cf[j == 2 ? F_511 : F_1023]);
        }
        result = make_v4(c, a[0], a[1], a[2], c->cf[F_ONE]);
    } else {
        result = unary(c, SpvOpLoad, c->v4, c->inputs[index]);
        if (c->state->swizzle_attrs & (1u << index)) {
            result = value(c->w, SpvOpVectorShuffle, c->v4, 6,
                           result, result, 2u, 1u, 0u, 3u);
        }
    }
    c->input_values[index] = result;
    return result;
}

static uint32_t constant(Context *c, unsigned index)
{
    uint32_t ptr = value(c->w, SpvOpAccessChain, c->uniform_v4, 3,
                         c->ubo, c->ci[0], c->ci[index]);
    return unary(c, SpvOpLoad, c->v4, ptr);
}
static uint32_t transform(Context *c, uint32_t v, unsigned base)
{
    uint32_t column[4];
    for (unsigned i = 0; i < 4; i++) { column[i] = constant(c, base + i); }
    uint32_t matrix = value(c->w, SpvOpCompositeConstruct, c->mat4, 4,
                            column[0], column[1], column[2], column[3]);
    return binary(c, SpvOpVectorTimesMatrix, c->v4, v, matrix);
}
static unsigned skin_matrices(enum VshSkinning mode)
{
    static const unsigned counts[] = {0, 2, 2, 3, 3, 4, 4};
    return counts[mode];
}

static uint32_t skinned(Context *c, uint32_t source, bool normal)
{
    enum VshSkinning mode = c->state->fixed_function.skinning;
    unsigned count = skin_matrices(mode);
    unsigned base = normal ? NV_IGRAPH_XF_XFCTX_IMMAT0 : NV_IGRAPH_XF_XFCTX_MMAT0;
    if (!count) { return transform(c, source, base); }
    if (!c->skin_weights[0]) {
        uint32_t weights = input(c, 1), remaining = c->cf[F_ONE];
        for (unsigned i = 0; i < count; i++) {
            bool generated = (mode & 1) && i == count - 1;
            uint32_t weight = generated ? remaining : extract(c, weights, i);
            c->skin_weights[i] = weight;
            if ((mode & 1) && !generated) { remaining = sub(c, remaining, weight); }
        }
    }
    uint32_t type = normal ? c->v3 : c->v4;
    uint32_t sum = normal ? value(c->w, SpvOpCompositeConstruct, c->v3, 3,
                                  c->cf[F_ZERO], c->cf[F_ZERO], c->cf[F_ZERO]) : c->zero4;
    for (unsigned i = 0; i < count; i++) {
        uint32_t v = transform(c, source, base + i * 8);
        if (normal) { v = value(c->w, SpvOpVectorShuffle, c->v3, 5, v, v, 0u, 1u, 2u); }
        v = binary(c, SpvOpVectorTimesScalar, type, v, c->skin_weights[i]);
        /* Keep the initial +0 and sequential additions from the reference. */
        sum = binary(c, SpvOpFAdd, type, sum, v);
    }
    return sum;
}

static uint32_t transformed_normal(Context *c)
{
    uint32_t mapped_normal;
    uint32_t n = input(c, 2);
    n = make_v4(c, extract(c, n, 0), extract(c, n, 1),
                extract(c, n, 2), c->cf[F_ZERO]);
    if (c->state->fixed_function.skinning == SKINNING_OFF) {
        n = transform(c, n, NV_IGRAPH_XF_XFCTX_IMMAT0);
        mapped_normal = value(c->w, SpvOpVectorShuffle, c->v3, 5, n, n, 0u, 1u, 2u);
    } else {
        mapped_normal = skinned(c, n, true);
    }
    if (c->state->fixed_function.normalization) {
        mapped_normal = ext1(c, GLSLstd450Normalize, c->v3, mapped_normal);
    }
    return mapped_normal;
}

static uint32_t texgen_reflection(Context *c, uint32_t position, uint32_t normal, uint32_t packed)
{
    uint32_t xyz = value(c->w, SpvOpVectorShuffle, c->v3, 5, position, position, 0u, 1u, 2u);
    uint32_t eye;
    uint32_t forward = value(c->w, SpvOpCompositeConstruct, c->v3, 3, c->cf[F_ZERO], c->cf[F_ZERO], c->cf[F_ONE]);
    /* packed is an SSA ID; zero asks static callers to load the uniform. */
    uint32_t viewer = packed;
    if (!viewer) {
        unsigned member = 7 + (!c->opts.push_constants && c->state->uniform_attrs != 0);
        uint32_t ptr = value(c->w, SpvOpAccessChain, c->uniform_u, 3, c->ubo, c->ci[member], c->ci[0]);
        viewer = unary(c, SpvOpLoad, c->u, ptr);
    }
    viewer = binary(c, SpvOpBitwiseAnd, c->u, viewer, c->viewer_mask);
    viewer = binary(c, SpvOpINotEqual, c->b, viewer, c->cu[0]);
    /* Match the reference's lazy viewer branch. Eager Normalize
     * plus Select changed physical-GPU reflection rounding. */
    uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
    op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
    op(c->w, SpvOpBranchConditional, 3, viewer, yes, no);
    op(c->w, SpvOpLabel, 1, yes);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, no);
    uint32_t normalized = ext1(c, GLSLstd450Normalize, c->v3, xyz);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, merge);
    eye = value(c->w, SpvOpPhi, c->v3, 4, forward, yes, normalized, no);
    uint32_t reflected = value(c->w, SpvOpExtInst, c->v3, 4, c->ext, (uint32_t)GLSLstd450Reflect, eye, normal);
    return reflected;
}

static void dynamic_texgen(Context *c, uint32_t *eye_position)
{
    static const unsigned planes[] = {
        NV_IGRAPH_XF_XFCTX_TG0MAT, NV_IGRAPH_XF_XFCTX_TG1MAT,
        NV_IGRAPH_XF_XFCTX_TG2MAT, NV_IGRAPH_XF_XFCTX_TG3MAT,
    };
    static const unsigned matrices[] = {
        NV_IGRAPH_XF_XFCTX_T0MAT, NV_IGRAPH_XF_XFCTX_T1MAT,
        NV_IGRAPH_XF_XFCTX_T2MAT, NV_IGRAPH_XF_XFCTX_T3MAT,
    };
    if (!*eye_position) { *eye_position = transform(c, input(c, 0), NV_IGRAPH_XF_XFCTX_MMAT0); }
    uint32_t normal = transformed_normal(c), modes[4];
    unsigned mode_member = 7 + (!c->opts.push_constants && c->state->uniform_attrs != 0);
    for (unsigned t = 0; t < 4; t++) {
        uint32_t ptr = value(c->w, SpvOpAccessChain, c->uniform_u, 3, c->ubo, c->ci[mode_member], c->ci[t]);
        modes[t] = unary(c, SpvOpLoad, c->u, ptr);
    }
    uint32_t reflected = texgen_reflection(c, *eye_position, normal, modes[0]);
    uint32_t forward = value(c->w, SpvOpCompositeConstruct, c->v3, 3, c->cf[F_ZERO], c->cf[F_ZERO], c->cf[F_ONE]);
    uint32_t shifted = binary(c, SpvOpFAdd, c->v3, reflected, forward);
    uint32_t scale = divf(c, c->cf[F_ONE], mul(c, c->cf[F_TWO], ext1(c, GLSLstd450Length, c->f, shifted)));
    uint32_t sphere = binary(c, SpvOpVectorTimesScalar, c->v3, reflected, scale);
    uint32_t half = value(c->w, SpvOpCompositeConstruct, c->v3, 3, c->texgen_half, c->texgen_half, c->texgen_half);
    sphere = binary(c, SpvOpFAdd, c->v3, sphere, half);
    unsigned matrix_member = mode_member + 1 + c->state->point_params_enable;
    uint32_t enabled = load_uniform(c, matrix_member, c->u, c->uniform_u);
    const uint32_t shifts[] = {c->cu[0], c->cu[3], c->cu[6], c->texgen_shift9};
    const uint32_t bits[] = {c->cu[1], c->cu[2], c->cu[4], c->texgen_bit8};
    uint32_t original = input(c, 0);
    for (unsigned t = 0; t < 4; t++) {
        /* Load the shared input outside selection blocks so its cached ID
         * dominates every consumer, including inline/compressed inputs. */
        uint32_t texture = input(c, 9 + t), components[4];
        for (unsigned a = 0; a < 4; a++) {
            uint32_t mode = binary(c, SpvOpShiftRightLogical, c->u, modes[t], shifts[a]);
            mode = binary(c, SpvOpBitwiseAnd, c->u, mode, c->cu[7]);
            uint32_t merge = id(c->w), labels[6], selected[6];
            for (unsigned i = 0; i < 6; i++) { labels[i] = id(c->w); }
            op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
            op(c->w, SpvOpSwitch, 12, mode, labels[5], 0u, labels[0], 1u, labels[1], 2u, labels[2], 3u, labels[3], 4u, labels[4]);
            for (unsigned i = 0; i < 6; i++) {
                op(c->w, SpvOpLabel, 1, labels[i]);
                if (i == 0) { selected[i] = extract(c, texture, a); }
                else if (i < 3) { selected[i] = binary(c, SpvOpDot, c->f, constant(c, planes[t] + a), i == 1 ? *eye_position : original); }
                else if (i == 3) { selected[i] = a < 2 ? extract(c, sphere, a) : c->cf[F_ZERO]; }
                else if (i == 4) { selected[i] = a < 3 ? extract(c, normal, a) : c->cf[F_ZERO]; }
                else { selected[i] = a < 3 ? extract(c, reflected, a) : c->cf[F_ZERO]; }
                op(c->w, SpvOpBranch, 1, merge);
            }
            op(c->w, SpvOpLabel, 1, merge);
            components[a] = value(c->w, SpvOpPhi, c->f, 12,
                                  selected[0], labels[0], selected[1], labels[1], selected[2], labels[2],
                                  selected[3], labels[3], selected[4], labels[4], selected[5], labels[5]);
        }
        uint32_t v = make_v4(c, components[0], components[1], components[2], components[3]);
        uint32_t test = binary(c, SpvOpBitwiseAnd, c->u, enabled, bits[t]);
        test = binary(c, SpvOpINotEqual, c->b, test, c->cu[0]);
        uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
        op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
        op(c->w, SpvOpBranchConditional, 3, test, yes, no);
        op(c->w, SpvOpLabel, 1, yes);
        uint32_t transformed = transform(c, v, matrices[t]);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, no);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, merge);
        c->o[9 + t] = value(c->w, SpvOpPhi, c->v4, 4, transformed, yes, v, no);
    }
}

static uint32_t dynamic_fog_distance(Context *c, uint32_t *eye_position)
{
    if (!*eye_position) { *eye_position = transform(c, input(c, 0), NV_IGRAPH_XF_XFCTX_MMAT0); }
    /* Preload shared input IDs in the dominating block. */
    uint32_t specular = input(c, 4), coordinate = input(c, 5);
    c->fog_control = load_uniform(c, 2, c->u, c->uniform_u);
    uint32_t gen = binary(c, SpvOpShiftRightLogical, c->u, c->fog_control, c->cu[4]);
    gen = binary(c, SpvOpBitwiseAnd, c->u, gen, c->cu[7]);
    uint32_t spec = id(c->w), radial = id(c->w), explicit = id(c->w);
    uint32_t planar = id(c->w), merge = id(c->w);
    op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
    op(c->w, SpvOpSwitch, 8, gen, planar, 0u, spec, 1u, radial, 4u, explicit);
    op(c->w, SpvOpLabel, 1, spec);
    uint32_t alpha = clamp(c, c->f, extract(c, specular, 3), c->cf[F_ZERO], c->cf[F_ONE]);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, radial);
    uint32_t xyz = value(c->w, SpvOpVectorShuffle, c->v3, 5, *eye_position, *eye_position, 0u, 1u, 2u);
    uint32_t length = ext1(c, GLSLstd450Length, c->f, xyz);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, explicit);
    uint32_t fog_x = extract(c, coordinate, 0);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, planar);
    xyz = value(c->w, SpvOpVectorShuffle, c->v3, 5, *eye_position, *eye_position, 0u, 1u, 2u);
    uint32_t plane = constant(c, NV_IGRAPH_XF_XFCTX_FOG);
    uint32_t normal = value(c->w, SpvOpVectorShuffle, c->v3, 5, plane, plane, 0u, 1u, 2u);
    uint32_t distance = add(c, binary(c, SpvOpDot, c->f, normal, xyz), extract(c, plane, 3));
    uint32_t absolute = binary(c, SpvOpIEqual, c->b, gen, c->cu[3]);
    distance = choose(c, c->f, absolute, ext1(c, GLSLstd450FAbs, c->f, distance), distance);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, merge);
    return value(c->w, SpvOpPhi, c->f, 8, alpha, spec, length, radial, fog_x, explicit, distance, planar);
}

static void unlit(Context *c)
{
    static const unsigned tex_matrix[] = {
        NV_IGRAPH_XF_XFCTX_T0MAT, NV_IGRAPH_XF_XFCTX_T1MAT,
        NV_IGRAPH_XF_XFCTX_T2MAT, NV_IGRAPH_XF_XFCTX_T3MAT,
    };
    static const unsigned tex_plane[] = {
        NV_IGRAPH_XF_XFCTX_TG0MAT, NV_IGRAPH_XF_XFCTX_TG1MAT,
        NV_IGRAPH_XF_XFCTX_TG2MAT, NV_IGRAPH_XF_XFCTX_TG3MAT,
    };
    uint32_t eye_position = 0, mapped_normal = 0, reflected = 0, sphere_scale = 0;
    uint32_t position = input(c, 0);
    if (c->state->fixed_function.skinning != SKINNING_OFF) {
        eye_position = skinned(c, position, false);
        position = eye_position;
    }
    c->o[0] = transform(c, position, NV_IGRAPH_XF_XFCTX_CMAT0);
    c->o[3] = input(c, 3); c->o[4] = input(c, 4);
    c->o[7] = input(c, 7); c->o[8] = input(c, 8);
    if (c->state->fog_dynamic) {
        c->fog_distance = dynamic_fog_distance(c, &eye_position);
    } else if (c->state->fog_enable) {
        uint32_t distance;
        enum VshFoggen gen = c->state->fixed_function.foggen;
        if (gen == FOGGEN_SPEC_ALPHA) {
            distance = clamp(c, c->f, extract(c, input(c, 4), 3), c->cf[F_ZERO], c->cf[F_ONE]);
        } else if (gen == FOGGEN_FOG_X) {
            distance = extract(c, input(c, 5), 0);
        } else {
            if (!eye_position) { eye_position = transform(c, input(c, 0), NV_IGRAPH_XF_XFCTX_MMAT0); }
            uint32_t position = eye_position;
            uint32_t xyz = value(c->w, SpvOpVectorShuffle, c->v3, 5, position, position, 0u, 1u, 2u);
            if (gen == FOGGEN_RADIAL) {
                distance = ext1(c, GLSLstd450Length, c->f, xyz);
            } else {
                uint32_t plane = constant(c, NV_IGRAPH_XF_XFCTX_FOG);
                uint32_t normal = value(c->w, SpvOpVectorShuffle, c->v3, 5, plane, plane, 0u, 1u, 2u);
                distance = add(c, binary(c, SpvOpDot, c->f, normal, xyz), extract(c, plane, 3));
                if (gen == FOGGEN_ABS_PLANAR) {
                    distance = ext1(c, GLSLstd450FAbs, c->f, distance);
                }
            }
        }
        c->o[5] = splat(c, distance);
    }
    if (c->state->fixed_function.texgen_dynamic) { dynamic_texgen(c, &eye_position); }
    else {
        for (unsigned t = 0; t < 4; t++) {
            bool generated = false;
            for (unsigned a = 0; a < 4; a++) { generated |= c->state->fixed_function.texgen[t][a] != TEXGEN_DISABLE; }
            uint32_t v;
            if (!generated) {
                v = input(c, 9 + t);
            } else {
                uint32_t component[4];
                for (unsigned a = 0; a < 4; a++) {
                    enum VshTexgen mode = c->state->fixed_function.texgen[t][a];
                    if (mode == TEXGEN_DISABLE) {
                        component[a] = extract(c, input(c, 9 + t), a);
                    } else if (mode == TEXGEN_NORMAL_MAP || mode == TEXGEN_REFLECTION_MAP || mode == TEXGEN_SPHERE_MAP) {
                        if (!mapped_normal) {
                            mapped_normal = transformed_normal(c);
                        }
                        uint32_t generated_normal = mapped_normal;
                        if (mode != TEXGEN_NORMAL_MAP) {
                            if (!reflected) {
                                if (!eye_position) {
                                    eye_position = transform(c, input(c, 0), NV_IGRAPH_XF_XFCTX_MMAT0);
                                }
                                reflected = texgen_reflection(c, eye_position, mapped_normal, 0);
                            }
                            generated_normal = reflected;
                        }
                        component[a] = extract(c, generated_normal, a);
                        if (mode == TEXGEN_SPHERE_MAP) {
                            if (!sphere_scale) {
                                uint32_t forward = value(c->w, SpvOpCompositeConstruct, c->v3, 3, c->cf[F_ZERO], c->cf[F_ZERO], c->cf[F_ONE]);
                                uint32_t shifted = binary(c, SpvOpFAdd, c->v3, reflected, forward);
                                sphere_scale = divf(c, c->cf[F_ONE], mul(c, c->cf[F_TWO], ext1(c, GLSLstd450Length, c->f, shifted)));
                            }
                            component[a] = add(c, mul(c, component[a], sphere_scale), divf(c, c->cf[F_ONE], c->cf[F_TWO]));
                        }
                    } else {
                        uint32_t position = input(c, 0);
                        if (mode == TEXGEN_EYE_LINEAR) {
                            if (!eye_position) {
                                eye_position = transform(c, position, NV_IGRAPH_XF_XFCTX_MMAT0);
                            }
                            position = eye_position;
                        }
                        component[a] = binary(c, SpvOpDot, c->f,
                                              constant(c, tex_plane[t] + a), position);
                    }
                }
                v = make_v4(c, component[0], component[1], component[2], component[3]);
            }
            if (c->state->fixed_function.texture_matrix_enable[t]) {
                v = transform(c, v, tex_matrix[t]);
            }
            c->o[9 + t] = v;
        }
    }
    if (c->state->point_params_enable) {
        uint32_t point_position = eye_position;
        if (!point_position || c->state->fixed_function.skinning != SKINNING_OFF) {
            point_position = transform(c, input(c, 0), NV_IGRAPH_XF_XFCTX_MMAT0);
        }
        /* Point distance uses original position * matrix0, even with skinning. */
        uint32_t distance = ext1(c, GLSLstd450Length, c->f, point_position);
        unsigned member = 7 + (!c->opts.push_constants && c->state->uniform_attrs != 0)
                            + needs_texgen_viewer(c->state);
        uint32_t p0 = point_parameter(c, member, 0), p1 = point_parameter(c, member, 1);
        uint32_t p2 = point_parameter(c, member, 2), p3 = point_parameter(c, member, 3);
        uint32_t p6 = point_parameter(c, member, 6), p7 = point_parameter(c, member, 7);
        uint32_t lo = ext2(c, GLSLstd450FMin, c->f, p7, c->point_limit);
        uint32_t hi = ext2(c, GLSLstd450FMin, c->f, add(c, p3, lo), c->point_limit);
        uint32_t polynomial = add(c, add(c, p0, mul(c, p1, distance)),
                                  mul(c, mul(c, p2, distance), distance));
        uint32_t point = add(c, divf(c, c->cf[F_ONE], ext1(c, GLSLstd450Sqrt, c->f, polynomial)), p6);
        point = clamp(c, c->f, add(c, mul(c, point, p3), p7), lo, hi);
        /* The common postlude applies pointScale once. */
        c->o[6] = splat(c, point);
    }
}

static uint32_t operand(Context *c, const uint32_t *t, unsigned arg)
{
    uint32_t v;
    unsigned source = mux(t, arg);
    if (source == PARAM_R) {
        v = read_reg(c, reg(t, arg));
    } else if (source == PARAM_V) {
        v = input(c, field(t[1], 9, 4));
    } else {
        uint32_t index = c->ci[field(t[1], 13, 8)];
        if (t[3] & 2) { index = binary(c, SpvOpIAdd, c->i, c->a0, index); }
        uint32_t p = value(c->w, SpvOpAccessChain, c->uniform_v4, 3,
                           c->ubo, c->ci[0], index);
        v = unary(c, SpvOpLoad, c->v4, p);
    }
    unsigned shift = arg == 0 ? 0 : arg == 1 ? 17 : 2;
    uint32_t sw = arg == 0 ? t[1] : t[2];
    unsigned x = field(sw, shift + 6, 2), y = field(sw, shift + 4, 2);
    unsigned z = field(sw, shift + 2, 2), w = field(sw, shift, 2);
    unsigned scalar = ilu(t);
    if (arg == 2 && scalar >= ILU_RCP && scalar <= ILU_LOG) { y = z = w = x; }
    v = value(c->w, SpvOpVectorShuffle, c->v4, 6, v, v, x, y, z, w);
    unsigned negate = arg == 0 ? field(t[1], 8, 1) :
                      arg == 1 ? field(t[2], 25, 1) : field(t[2], 10, 1);
    return negate ? unary(c, SpvOpFNegate, c->v4, v) : v;
}

static uint32_t clamp_away(Context *c, uint32_t t)
{
    uint32_t positive = binary(c, SpvOpFOrdGreaterThan, c->b, t, c->cf[F_ZERO]);
    uint32_t bits = unary(c, SpvOpBitcast, c->u, t);
    uint32_t plus_zero = binary(c, SpvOpIEqual, c->b, bits, c->cu[0]);
    uint32_t test = binary(c, SpvOpLogicalOr, c->b, positive, plus_zero);
    uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
    op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
    op(c->w, SpvOpBranchConditional, 3, test, yes, no);
    op(c->w, SpvOpLabel, 1, yes);
    uint32_t pos = clamp(c, c->f, t, c->cf[F_TINY], c->cf[F_HUGE]);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, no);
    uint32_t neg = clamp(c, c->f, t, c->cf[F_NEGHUGE], c->cf[F_NEGTINY]);
    op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, merge);
    return value(c->w, SpvOpPhi, c->f, 4, pos, yes, neg, no);
}
static uint32_t calculate(Context *c, const uint32_t *t, bool scalar)
{
    unsigned code = scalar ? ilu(t) : mac(t);
    uint32_t a = 0, b = 0, d = 0;
    if (scalar || (mac_params[code] & 4)) { d = operand(c, t, 2); }
    if (!scalar && (mac_params[code] & 1)) { a = operand(c, t, 0); }
    if (!scalar && (mac_params[code] & 2)) { b = operand(c, t, 1); }
    if (!scalar) {
        switch (code) {
        case MAC_MOV: return a;
        case MAC_MUL:
        case MAC_MAD: {
            uint32_t rhs = b;
            uint32_t az = binary(c, SpvOpFOrdEqual, c->b4, a, c->zero4);
            uint32_t bz = binary(c, SpvOpFOrdEqual, c->b4, rhs, c->zero4);
            uint32_t zero = binary(c, SpvOpLogicalOr, c->b4, az, bz);
            uint32_t product = binary(c, SpvOpFMul, c->v4, a, rhs);
            product = choose(c, c->v4, zero, c->zero4, product);
            return code == MAC_MAD ? binary(c, SpvOpFAdd, c->v4, product, d) : product;
        }
        case MAC_ADD: return binary(c, SpvOpFAdd, c->v4, a, d);
        case MAC_DP3: {
            uint32_t av = value(c->w, SpvOpVectorShuffle, c->v3, 5, a, a, 0u, 1u, 2u);
            uint32_t bv = value(c->w, SpvOpVectorShuffle, c->v3, 5, b, b, 0u, 1u, 2u);
            return splat(c, binary(c, SpvOpDot, c->f, av, bv));
        }
        case MAC_DPH: a = make_v4(c, extract(c, a, 0), extract(c, a, 1),
                               extract(c, a, 2), c->cf[F_ONE]);
            /* fall through */
        case MAC_DP4: return splat(c, binary(c, SpvOpDot, c->f, a, b));
        case MAC_DST: return make_v4(c, c->cf[F_ONE], mul(c, extract(c, a, 1), extract(c, b, 1)),
                                  extract(c, a, 2), extract(c, b, 3));
        case MAC_MIN: return ext2(c, GLSLstd450FMin, c->v4, a, b);
        case MAC_MAX: return ext2(c, GLSLstd450FMax, c->v4, a, b);
        case MAC_SLT:
        case MAC_SGE: {
            uint32_t test = binary(c, code == MAC_SLT ? SpvOpFOrdLessThan : SpvOpFOrdGreaterThanEqual,
                                   c->b4, a, b);
            return choose(c, c->v4, test, splat(c, c->cf[F_ONE]), c->zero4);
        }
        case MAC_ARL: {
            uint32_t x = add(c, extract(c, a, 0), c->cf[F_ARLBIAS]);
            return unary(c, SpvOpConvertFToS, c->i, ext1(c, GLSLstd450Floor, c->f, x));
        }
        default: abort();
        }
    }
    uint32_t x = extract(c, d, 0), one = c->cf[F_ONE], zero = c->cf[F_ZERO];
    switch (code) {
    case ILU_MOV: return d;
    case ILU_RCP: return splat(c, divf(c, one, x));
    case ILU_RCC: return splat(c, clamp_away(c, divf(c, one, x)));
    case ILU_RSQ: {
        uint32_t special = binary(c, SpvOpLogicalOr, c->b,
            binary(c, SpvOpFOrdEqual, c->b, x, zero), unary(c, SpvOpIsInf, c->b, x));
        uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
        op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
        op(c->w, SpvOpBranchConditional, 3, special, yes, no);
        op(c->w, SpvOpLabel, 1, yes);
        uint32_t limit = choose(c, c->f, binary(c, SpvOpFOrdEqual, c->b, x, zero), c->cf[F_INF], zero);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, no);
        uint32_t normal = ext1(c, GLSLstd450InverseSqrt, c->f, ext1(c, GLSLstd450FAbs, c->f, x));
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, merge);
        return splat(c, value(c->w, SpvOpPhi, c->f, 4, limit, yes, normal, no));
    }
    case ILU_EXP: {
        uint32_t floor = ext1(c, GLSLstd450Floor, c->f, x);
        return make_v4(c, ext1(c, GLSLstd450Exp2, c->f, floor), sub(c, x, floor),
                     ext1(c, GLSLstd450Exp2, c->f, x), one);
    }
    case ILU_LOG: {
        uint32_t absolute = ext1(c, GLSLstd450FAbs, c->f, x);
        uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
        uint32_t test = binary(c, SpvOpFOrdEqual, c->b, absolute, zero);
        op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
        op(c->w, SpvOpBranchConditional, 3, test, yes, no);
        op(c->w, SpvOpLabel, 1, yes);
        uint32_t special = make_v4(c, c->cf[F_NEGINF], one, c->cf[F_NEGINF], one);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, no);
        uint32_t log = ext1(c, GLSLstd450Log2, c->f, absolute);
        uint32_t floor = ext1(c, GLSLstd450Floor, c->f, log);
        uint32_t result = make_v4(c, floor, divf(c, absolute, ext1(c, GLSLstd450Exp2, c->f, floor)), log, one);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, merge);
        return value(c->w, SpvOpPhi, c->v4, 4, special, yes, result, no);
    }
    case ILU_LIT: {
        uint32_t sx = ext2(c, GLSLstd450FMax, c->f, extract(c, d, 0), zero);
        uint32_t sy = ext2(c, GLSLstd450FMax, c->f, extract(c, d, 1), zero);
        uint32_t sw = clamp(c, c->f, extract(c, d, 3), c->cf[F_NEGLITMAX], c->cf[F_LITMAX]);
        uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
        uint32_t test = binary(c, SpvOpFOrdGreaterThan, c->b, sx, zero);
        op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
        op(c->w, SpvOpBranchConditional, 3, test, yes, no);
        op(c->w, SpvOpLabel, 1, yes);
        uint32_t lit = ext1(c, GLSLstd450Exp2, c->f, mul(c, sw, ext1(c, GLSLstd450Log2, c->f, sy)));
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, no);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, merge);
        uint32_t z = value(c->w, SpvOpPhi, c->f, 4, lit, yes, zero, no);
        return make_v4(c, one, sx, z, one);
    }
    default: abort();
    }
}

static void program(Context *c)
{
    for (int i = 0; i < c->state->programmable.program_length; i++) {
        const uint32_t *t = c->state->programmable.program_data[i];
        unsigned m = mac(t), l = ilu(t), r = field(t[3], 20, 4);
        unsigned omask = field(t[3], 12, 4), target = field(t[3], 3, 4);
        if (target == 5) { omask = fog_masks[omask]; }
        uint32_t pending = 0;
        unsigned mmask = field(t[3], 24, 4);
        if (m) {
            if (!field(t[3], 2, 1) && omask) {
                uint32_t v = calculate(c, t, false);
                c->o[target] = masked(c, c->o[target], v, omask);
            }
            if (m == MAC_ARL) {
                pending = calculate(c, t, false);
                if (!l) { c->a0 = pending; }
            } else {
                if (l && r == 1) { mmask = 0; }
                if (mmask) {
                    pending = calculate(c, t, false);
                    if (!l) { write_reg(c, r, masked(c, read_reg(c, r), pending, mmask)); }
                }
            }
        }
        if (l) {
            if (field(t[3], 2, 1) && omask) {
                uint32_t v = calculate(c, t, true);
                c->o[target] = masked(c, c->o[target], v, omask);
            }
            unsigned mask = field(t[3], 16, 4), dest = m ? 1 : r;
            if (mask) {
                uint32_t v = calculate(c, t, true);
                write_reg(c, dest, masked(c, read_reg(c, dest), v, mask));
            }
            if (m == MAC_ARL) { c->a0 = pending; }
            else if (m && mmask) { write_reg(c, r, masked(c, read_reg(c, r), pending, mmask)); }
        }
        if (t[3] & 1) { break; }
    }
}

static uint32_t color(Context *c, uint32_t v)
{
    uint32_t nan = unary(c, SpvOpIsNan, c->b4, v);
    v = choose(c, c->v4, nan, splat(c, c->cf[F_ONE]), v);
    return clamp(c, c->v4, v, c->zero4, splat(c, c->cf[F_ONE]));
}
static void finish(Context *c)
{
    uint32_t raw;
    if (c->state->is_fixed_function) {
        uint32_t p = c->o[0], w = clamp_away(c, extract(c, p, 3));
        uint32_t viewport = constant(c, NV_IGRAPH_XF_XFCTX_VPOFF);
        uint32_t x = add(c, divf(c, extract(c, p, 0), w), extract(c, viewport, 0));
        uint32_t y = add(c, divf(c, extract(c, p, 1), w), extract(c, viewport, 1));
        x = divf(c, ext1(c, GLSLstd450Trunc, c->f, mul(c, x, c->cf[F_SIXTEEN])), c->cf[F_SIXTEEN]);
        y = divf(c, ext1(c, GLSLstd450Trunc, c->f, mul(c, y, c->cf[F_SIXTEEN])), c->cf[F_SIXTEEN]);
        uint32_t z = extract(c, p, 2);
        raw = make_v4(c, x, y, divf(c, z, w), w);
        uint32_t surface = load_uniform(c, 6, c->v2, c->uniform_v2);
        uint32_t clip = load_uniform(c, 1, c->v4, c->uniform_v4);
        uint32_t sx = value(c->w, SpvOpCompositeExtract, c->f, 2, surface, 0u);
        uint32_t sy = value(c->w, SpvOpCompositeExtract, c->f, 2, surface, 1u);
        x = mul(c, divf(c, sub(c, mul(c, c->cf[F_TWO], x), sx), sx), w);
        y = mul(c, divf(c, sub(c, mul(c, c->cf[F_TWO], y), sy), sy), w);
        z = divf(c, z, extract(c, clip, 1));
        op(c->w, SpvOpStore, 2, c->position, make_v4(c, x, y, z, w));
    } else {
        uint32_t p = c->o[0];
        uint32_t x = divf(c, ext1(c, GLSLstd450Trunc, c->f, mul(c, extract(c, p, 0), c->cf[F_SIXTEEN])), c->cf[F_SIXTEEN]);
        uint32_t y = divf(c, ext1(c, GLSLstd450Trunc, c->f, mul(c, extract(c, p, 1), c->cf[F_SIXTEEN])), c->cf[F_SIXTEEN]);
        uint32_t z = extract(c, p, 2), w = clamp_away(c, extract(c, p, 3));
        raw = make_v4(c, x, y, z, w);
        uint32_t surface = load_uniform(c, 6, c->v2, c->uniform_v2);
        uint32_t clip = load_uniform(c, 1, c->v4, c->uniform_v4);
        uint32_t sx = value(c->w, SpvOpCompositeExtract, c->f, 2, surface, 0u);
        uint32_t sy = value(c->w, SpvOpCompositeExtract, c->f, 2, surface, 1u);
        x = mul(c, divf(c, sub(c, mul(c, c->cf[F_TWO], x), sx), sx), w);
        y = mul(c, divf(c, sub(c, mul(c, c->cf[F_TWO], y), sy), sy), w);
        z = mul(c, divf(c, z, extract(c, clip, 1)), w);
        op(c->w, SpvOpStore, 2, c->position, make_v4(c, x, y, z, w));
    }
    uint32_t point = extract(c, c->o[6], 0);
    if (!c->state->point_params_enable) {
        point = load_uniform(c, 4, c->f, c->uniform_f);
        if (c->state->is_fixed_function) {
            point = ext2(c, GLSLstd450FMax, c->f, c->cf[F_ONE], point);
        } else {
            point = choose(c, c->f, binary(c, SpvOpFOrdLessThanEqual, c->b, point, c->cf[F_ZERO]), c->cf[F_ONE], point);
        }
    }
    point = mul(c, point, load_uniform(c, 5, c->f, c->uniform_f));
    op(c->w, SpvOpStore, 2, c->point_size, point);
    op(c->w, SpvOpStore, 2, c->outputs[0], color(c, c->o[3]));
    op(c->w, SpvOpStore, 2, c->outputs[2], color(c, c->o[7]));
    uint32_t d1 = c->initial4, b1 = c->initial4;
    if (c->state->specular_enable) {
        d1 = color(c, c->o[4]); b1 = color(c, c->o[8]);
        if (c->state->ignore_specular_alpha) {
            d1 = make_v4(c, extract(c, d1, 0), extract(c, d1, 1), extract(c, d1, 2), c->cf[F_ONE]);
            b1 = make_v4(c, extract(c, b1, 0), extract(c, b1, 1), extract(c, b1, 2), c->cf[F_ONE]);
        }
    }
    op(c->w, SpvOpStore, 2, c->outputs[1], d1);
    op(c->w, SpvOpStore, 2, c->outputs[3], b1);
    uint32_t fog = c->cf[F_ONE];
    if (c->state->fog_dynamic) {
        uint32_t ctl = c->fog_control ? c->fog_control : load_uniform(c, 2, c->u, c->uniform_u);
        uint32_t mode = binary(c, SpvOpBitwiseAnd, c->u,
                              binary(c, SpvOpShiftRightLogical, c->u, ctl, c->cu[1]), c->cu[7]);
        uint32_t param = load_uniform(c, 3, c->v2, c->uniform_v2);
        uint32_t px = value(c->w, SpvOpCompositeExtract, c->f, 2, param, 0u);
        uint32_t py = value(c->w, SpvOpCompositeExtract, c->f, 2, param, 1u);
        uint32_t distance = c->fog_distance ? c->fog_distance : extract(c, c->o[5], 0);
        uint32_t kind = binary(c, SpvOpBitwiseAnd, c->u, mode, c->cu[3]);
        uint32_t lin_label = id(c->w), exp_label = id(c->w);
        uint32_t sq_label = id(c->w), merge = id(c->w);
        op(c->w, SpvOpSelectionMerge, 2, merge, 0u);
        op(c->w, SpvOpSwitch, 6, kind, sq_label, 0u, lin_label, 1u, exp_label);
        op(c->w, SpvOpLabel, 1, lin_label);
        uint32_t linear = sub(c, add(c, px, mul(c, distance, py)), c->cf[F_ONE]);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, exp_label);
        uint32_t exp = sub(c, add(c, px, ext1(c, GLSLstd450Exp2, c->f,
                                  mul(c, mul(c, distance, py), c->cf[F_SIXTEEN]))), c->cf[F_ONEHALF]);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, sq_label);
        uint32_t squared = squared_fog(c, distance, py);
        squared = sub(c, add(c, px, ext1(c, GLSLstd450Exp2, c->f, squared)), c->cf[F_ONEHALF]);
        op(c->w, SpvOpBranch, 1, merge);
        op(c->w, SpvOpLabel, 1, merge);
        fog = value(c->w, SpvOpPhi, c->f, 6, linear, lin_label, exp, exp_label, squared, sq_label);
        fog = choose(c, c->f, binary(c, SpvOpUGreaterThanEqual, c->b, mode, c->cu[4]),
                     ext1(c, GLSLstd450FAbs, c->f, fog), fog);
        uint32_t special_test = binary(c, SpvOpLogicalOr, c->b,
            binary(c, SpvOpULessThanEqual, c->b, mode, c->cu[1]),
            binary(c, SpvOpIEqual, c->b, mode, c->cu[4]));
        uint32_t special = choose(c, c->f, special_test, c->cf[F_ONE], c->cf[F_ZERO]);
        fog = choose(c, c->f, unary(c, SpvOpIsNan, c->b, fog), special, fog);
        fog = clamp(c, c->f, fog, c->cf[F_NEGMAX], c->cf[F_MAX]);
        fog = choose(c, c->f, unary(c, SpvOpIsInf, c->b, distance), special, fog);
        uint32_t enabled = binary(c, SpvOpINotEqual, c->b,
                                  binary(c, SpvOpBitwiseAnd, c->u, ctl, c->cu[1]), c->cu[0]);
        fog = choose(c, c->f, enabled, fog, c->cf[F_ONE]);
    } else if (c->state->fog_enable) {
        /* Specialize only the static mode. Parameter values and distance
         * still come from uniforms/registers, just as in the GLSL path. */
        uint32_t param = load_uniform(c, 3, c->v2, c->uniform_v2);
        uint32_t px = value(c->w, SpvOpCompositeExtract, c->f, 2, param, 0u);
        uint32_t py = value(c->w, SpvOpCompositeExtract, c->f, 2, param, 1u);
        uint32_t distance = extract(c, c->o[5], 0);
        unsigned mode = c->state->fog_mode;
        if ((mode & 3) == 0) {
            fog = sub(c, add(c, px, mul(c, distance, py)), c->cf[F_ONE]);
        } else if ((mode & 3) == 1) {
            fog = sub(c, add(c, px, ext1(c, GLSLstd450Exp2, c->f,
                mul(c, mul(c, distance, py), c->cf[F_SIXTEEN]))), c->cf[F_ONEHALF]);
        } else {
            uint32_t squared = squared_fog(c, distance, py);
            fog = sub(c, add(c, px, ext1(c, GLSLstd450Exp2, c->f, squared)), c->cf[F_ONEHALF]);
        }
        if (mode >= 4) { fog = ext1(c, GLSLstd450FAbs, c->f, fog); }
        uint32_t special = c->cf[mode == FOG_MODE_LINEAR || mode == FOG_MODE_LINEAR_ABS ||
                                 mode == FOG_MODE_EXP ? F_ONE : F_ZERO];
        fog = choose(c, c->f, unary(c, SpvOpIsNan, c->b, fog), special, fog);
        fog = clamp(c, c->f, fog, c->cf[F_NEGMAX], c->cf[F_MAX]);
        fog = choose(c, c->f, unary(c, SpvOpIsInf, c->b, distance), special, fog);
    }
    op(c->w, SpvOpStore, 2, c->outputs[4], fog);
    for (unsigned i = 0; i < 4; i++) { op(c->w, SpvOpStore, 2, c->outputs[5 + i], c->o[9 + i]); }
    for (unsigned i = 9; i < 12; i++) { op(c->w, SpvOpStore, 2, c->outputs[i], raw); }
    op(c->w, SpvOpStore, 2, c->outputs[12], c->cf[F_ZERO]);
}

static void allocate_ids(Context *c)
{
    Writer *w = c->w;
    uint32_t *types[] = { &c->void_t, &c->fn_t, &c->f, &c->v2, &c->v3, &c->v4,
        &c->b, &c->b4, &c->i, &c->u, &c->in_v4, &c->in_i, &c->out_v4, &c->out_f,
        &c->uniform_v4, &c->uniform_v2, &c->uniform_f, &c->uniform_u, &c->push_v4,
        &c->c_array, &c->inline_array, &c->push_array, &c->ubo_t, &c->pc_t, &c->ubo_p, &c->pc_p };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) { *types[i] = id(w); }
    for (unsigned i = 0; i < 256; i++) { c->ci[i] = id(w); }
    for (unsigned i = 0; i < 8; i++) { c->cu[i] = id(w); }
    for (unsigned i = 0; i < F_COUNT; i++) { c->cf[i] = id(w); }
    c->zero4 = id(w); c->initial4 = id(w);
    for (unsigned i = 0; i < 16; i++) { c->inputs[i] = id(w); }
    for (unsigned i = 0; i < 13; i++) { c->outputs[i] = id(w); }
    c->position = id(w); c->point_size = id(w); c->ubo = id(w);
    c->pc = id(w); c->main = id(w); c->ext = id(w);
    if (c->state->is_fixed_function) { c->mat4 = id(w); }
    if (needs_texgen_viewer(c->state)) { c->texgen_array = id(w); c->viewer_mask = id(w); }
    if (c->state->is_fixed_function && c->state->point_params_enable) {
        c->point_array = id(w); c->point_limit = id(w);
    }
    if (c->state->is_fixed_function && c->state->fixed_function.texgen_dynamic) {
        c->texgen_half = id(w); c->texgen_shift9 = id(w); c->texgen_bit8 = id(w);
    }
}
static void emit(Writer *w, const VshState *state, SpvVshOptions opts)
{
    Context c = { .w = w, .state = state, .opts = opts };
    allocate_ids(&c);
    bool pc = opts.push_constants && state->uniform_attrs;
    bool inline_ubo = !opts.push_constants && state->uniform_attrs;
    unsigned pc_count = __builtin_popcount(state->uniform_attrs);
    bool viewer = needs_texgen_viewer(state);
    bool points = state->is_fixed_function && state->point_params_enable;
    bool dynamic = state->is_fixed_function && state->fixed_function.texgen_dynamic;
    word(w, SpvMagicNumber); word(w, opts.spirv_1_6 ? 0x00010600u : 0x00010500u);
    word(w, 0); word(w, 0); word(w, 0);
    op(w, SpvOpCapability, 1, (uint32_t)SpvCapabilityShader);
    if (state->point_params_enable || state->is_fixed_function) {
        op(w, SpvOpCapability, 1, (uint32_t)SpvCapabilitySignedZeroInfNanPreserve);
    }
    op(w, SpvOpExtInstImport, 5, c.ext, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0u);
    op(w, SpvOpMemoryModel, 2, (uint32_t)SpvAddressingModelLogical, (uint32_t)SpvMemoryModelGLSL450);
    unsigned interfaces = 16 + 13 + 2 + 1 + pc;
    word(w, ((5 + interfaces) << 16) | SpvOpEntryPoint);
    word(w, SpvExecutionModelVertex); word(w, c.main); word(w, 0x6e69616du); word(w, 0);
    for (unsigned i = 0; i < 16; i++) { word(w, c.inputs[i]); }
    for (unsigned i = 0; i < 13; i++) { word(w, c.outputs[i]); }
    word(w, c.position); word(w, c.point_size); word(w, c.ubo);
    if (pc) { word(w, c.pc); }
    if (state->point_params_enable || state->is_fixed_function) {
        op(w, SpvOpExecutionMode, 3, c.main,
           (uint32_t)SpvExecutionModeSignedZeroInfNanPreserve, 32u);
    }
    for (unsigned i = 0; i < 16; i++) { op(w, SpvOpDecorate, 3, c.inputs[i], (uint32_t)SpvDecorationLocation, i); }
    for (unsigned i = 0; i < 13; i++) {
        op(w, SpvOpDecorate, 3, c.outputs[i], (uint32_t)SpvDecorationLocation, i);
        if (i >= 9 || (i < 4 && !state->smooth_shading)) {
            op(w, SpvOpDecorate, 2, c.outputs[i], (uint32_t)SpvDecorationFlat);
        }
    }
    op(w, SpvOpDecorate, 3, c.position, (uint32_t)SpvDecorationBuiltIn, (uint32_t)SpvBuiltInPosition);
    op(w, SpvOpDecorate, 3, c.point_size, (uint32_t)SpvDecorationBuiltIn, (uint32_t)SpvBuiltInPointSize);
    op(w, SpvOpDecorate, 2, c.ubo_t, (uint32_t)SpvDecorationBlock);
    static const unsigned offsets[] = { 0, 3072, 3088, 3096, 3104, 3108, 3112, 3120 };
    for (unsigned i = 0; i < 7u + inline_ubo; i++) {
        op(w, SpvOpMemberDecorate, 4, c.ubo_t, i, (uint32_t)SpvDecorationOffset, offsets[i]);
    }
    if (viewer) {
        op(w, SpvOpMemberDecorate, 4, c.ubo_t, 7u + inline_ubo, (uint32_t)SpvDecorationOffset, 3120u + inline_ubo * 256u);
        op(w, SpvOpDecorate, 3, c.texgen_array, (uint32_t)SpvDecorationArrayStride, 16u);
    }
    if (points) {
        op(w, SpvOpMemberDecorate, 4, c.ubo_t, 7u + inline_ubo + viewer,
           (uint32_t)SpvDecorationOffset, 3120u + inline_ubo * 256u + viewer * 64u);
        op(w, SpvOpDecorate, 3, c.point_array, (uint32_t)SpvDecorationArrayStride, 16u);
    }
    if (dynamic) {
        op(w, SpvOpMemberDecorate, 4, c.ubo_t, 7u + inline_ubo + viewer + points,
           (uint32_t)SpvDecorationOffset, 3120u + inline_ubo * 256u + viewer * 64u + points * 128u);
    }
    op(w, SpvOpDecorate, 3, c.c_array, (uint32_t)SpvDecorationArrayStride, 16u);
    op(w, SpvOpDecorate, 3, c.inline_array, (uint32_t)SpvDecorationArrayStride, 16u);
    op(w, SpvOpDecorate, 3, c.ubo, (uint32_t)SpvDecorationDescriptorSet, 0u);
    op(w, SpvOpDecorate, 3, c.ubo, (uint32_t)SpvDecorationBinding, opts.ubo_binding);
    if (pc) {
        op(w, SpvOpDecorate, 3, c.push_array, (uint32_t)SpvDecorationArrayStride, 16u);
        op(w, SpvOpDecorate, 2, c.pc_t, (uint32_t)SpvDecorationBlock);
        op(w, SpvOpMemberDecorate, 4, c.pc_t, 0u, (uint32_t)SpvDecorationOffset, 0u);
    }
    /* The count pass records the four result IDs. Annotations consume no IDs,
     * so the write pass can emit them directly before types/functions. */
    if (needs_precise_fog(state)) {
        for (unsigned i = 0; i < 4; i++) {
            op(w, SpvOpDecorate, 2, w->precise_mul[i], (uint32_t)SpvDecorationNoContraction);
        }
    }
    op(w, SpvOpTypeVoid, 1, c.void_t);
    op(w, SpvOpTypeFunction, 2, c.fn_t, c.void_t);
    op(w, SpvOpTypeFloat, 2, c.f, 32u);
    op(w, SpvOpTypeVector, 3, c.v2, c.f, 2u);
    op(w, SpvOpTypeVector, 3, c.v3, c.f, 3u);
    op(w, SpvOpTypeVector, 3, c.v4, c.f, 4u);
    if (state->is_fixed_function) { op(w, SpvOpTypeMatrix, 3, c.mat4, c.v4, 4u); }
    op(w, SpvOpTypeBool, 1, c.b);
    op(w, SpvOpTypeVector, 3, c.b4, c.b, 4u);
    op(w, SpvOpTypeInt, 3, c.i, 32u, 1u);
    op(w, SpvOpTypeInt, 3, c.u, 32u, 0u);
    bool used[256] = {0};
    for (unsigned i = 0; i <= 32; i++) { used[i] = true; }
    used[NV2A_VERTEXSHADER_CONSTANTS] = true;
    if (state->is_fixed_function) {
        used[NV_IGRAPH_XF_XFCTX_VPOFF] = true;
        if (state->fog_dynamic) { used[NV_IGRAPH_XF_XFCTX_FOG] = true; }
        unsigned bones = skin_matrices(state->fixed_function.skinning);
        for (unsigned b = 0; b < bones; b++) {
            for (unsigned j = 0; j < 4; j++) { used[NV_IGRAPH_XF_XFCTX_MMAT0 + b * 8 + j] = true; }
        }
        if (state->fog_enable) {
            enum VshFoggen gen = state->fixed_function.foggen;
            if (gen == FOGGEN_PLANAR || gen == FOGGEN_ABS_PLANAR) {
                used[NV_IGRAPH_XF_XFCTX_FOG] = true;
            }
            if (gen >= FOGGEN_RADIAL && gen <= FOGGEN_ABS_PLANAR) {
                for (unsigned j = 0; j < 4; j++) { used[NV_IGRAPH_XF_XFCTX_MMAT0 + j] = true; }
            }
        }
        for (unsigned j = 0; j < 4; j++) { used[NV_IGRAPH_XF_XFCTX_CMAT0 + j] = true; }
        static const unsigned planes[] = {
            NV_IGRAPH_XF_XFCTX_TG0MAT, NV_IGRAPH_XF_XFCTX_TG1MAT,
            NV_IGRAPH_XF_XFCTX_TG2MAT, NV_IGRAPH_XF_XFCTX_TG3MAT,
        };
        static const unsigned bases[] = {
            NV_IGRAPH_XF_XFCTX_T0MAT, NV_IGRAPH_XF_XFCTX_T1MAT,
            NV_IGRAPH_XF_XFCTX_T2MAT, NV_IGRAPH_XF_XFCTX_T3MAT,
        };
        for (unsigned t = 0; t < 4; t++) {
            for (unsigned a = 0; a < 4; a++) {
                enum VshTexgen mode = state->fixed_function.texgen[t][a];
                if (dynamic || mode == TEXGEN_EYE_LINEAR || mode == TEXGEN_OBJECT_LINEAR) { used[planes[t] + a] = true; }
                if (dynamic || mode == TEXGEN_NORMAL_MAP || mode == TEXGEN_REFLECTION_MAP || mode == TEXGEN_SPHERE_MAP) {
                    for (unsigned b = 0; b < (bones ? bones : 1); b++) {
                        for (unsigned j = 0; j < 4; j++) { used[NV_IGRAPH_XF_XFCTX_IMMAT0 + b * 8 + j] = true; }
                    }
                }
                if (dynamic || mode == TEXGEN_EYE_LINEAR || mode == TEXGEN_REFLECTION_MAP || mode == TEXGEN_SPHERE_MAP) {
                    for (unsigned j = 0; j < 4; j++) { used[NV_IGRAPH_XF_XFCTX_MMAT0 + j] = true; }
                }
            }
            if (dynamic || state->fixed_function.texture_matrix_enable[t]) {
                for (unsigned j = 0; j < 4; j++) { used[bases[t] + j] = true; }
            }
        }
    } else {
        for (int i = 0; i < state->programmable.program_length; i++) {
            const uint32_t *t = state->programmable.program_data[i];
            used[field(t[1], 13, 8)] = true;
            if (t[3] & 1) { break; }
        }
    }
    for (unsigned i = 0; i < 256; i++) { if (used[i]) { op(w, SpvOpConstant, 3, c.i, c.ci[i], i); } }
    for (unsigned i = 0; i < 8; i++) { op(w, SpvOpConstant, 3, c.u, c.cu[i], i); }
    for (unsigned i = 0; i < F_COUNT; i++) {
        uint32_t bits; memcpy(&bits, &floats[i], sizeof(bits));
        op(w, SpvOpConstant, 3, c.f, c.cf[i], bits);
    }
    op(w, SpvOpConstantComposite, 6, c.v4, c.zero4, c.cf[F_ZERO], c.cf[F_ZERO], c.cf[F_ZERO], c.cf[F_ZERO]);
    op(w, SpvOpConstantComposite, 6, c.v4, c.initial4, c.cf[F_ZERO], c.cf[F_ZERO], c.cf[F_ZERO], c.cf[F_ONE]);
    op(w, SpvOpTypeArray, 3, c.c_array, c.v4, c.ci[NV2A_VERTEXSHADER_CONSTANTS]);
    op(w, SpvOpTypeArray, 3, c.inline_array, c.v4, c.ci[16]);
    if (viewer) {
        op(w, SpvOpConstant, 3, c.u, c.viewer_mask, 4096u);
        op(w, SpvOpTypeArray, 3, c.texgen_array, c.u, c.ci[4]);
    }
    if (points) {
        op(w, SpvOpConstant, 3, c.f, c.point_limit, 0x427f8000u); /* 63.875 */
        op(w, SpvOpTypeArray, 3, c.point_array, c.f, c.ci[8]);
    }
    if (dynamic) {
        op(w, SpvOpConstant, 3, c.f, c.texgen_half, 0x3f000000u);
        op(w, SpvOpConstant, 3, c.u, c.texgen_shift9, 9u);
        op(w, SpvOpConstant, 3, c.u, c.texgen_bit8, 8u);
    }
    word(w, ((9 + inline_ubo + viewer + points + dynamic) << 16) | SpvOpTypeStruct);
    word(w, c.ubo_t); word(w, c.c_array); word(w, c.v4); word(w, c.u); word(w, c.v2);
    word(w, c.f); word(w, c.f); word(w, c.v2);
    if (inline_ubo) { word(w, c.inline_array); }
    if (viewer) { word(w, c.texgen_array); }
    if (points) { word(w, c.point_array); }
    if (dynamic) { word(w, c.u); }
    if (pc) {
        op(w, SpvOpTypeArray, 3, c.push_array, c.v4, c.ci[pc_count]);
        op(w, SpvOpTypeStruct, 2, c.pc_t, c.push_array);
        op(w, SpvOpTypePointer, 3, c.pc_p, (uint32_t)SpvStorageClassPushConstant, c.pc_t);
    }
    op(w, SpvOpTypePointer, 3, c.ubo_p, (uint32_t)SpvStorageClassUniform, c.ubo_t);
    op(w, SpvOpTypePointer, 3, c.in_v4, (uint32_t)SpvStorageClassInput, c.v4);
    op(w, SpvOpTypePointer, 3, c.in_i, (uint32_t)SpvStorageClassInput, c.i);
    op(w, SpvOpTypePointer, 3, c.out_v4, (uint32_t)SpvStorageClassOutput, c.v4);
    op(w, SpvOpTypePointer, 3, c.out_f, (uint32_t)SpvStorageClassOutput, c.f);
    op(w, SpvOpTypePointer, 3, c.uniform_v4, (uint32_t)SpvStorageClassUniform, c.v4);
    op(w, SpvOpTypePointer, 3, c.uniform_v2, (uint32_t)SpvStorageClassUniform, c.v2);
    op(w, SpvOpTypePointer, 3, c.uniform_f, (uint32_t)SpvStorageClassUniform, c.f);
    op(w, SpvOpTypePointer, 3, c.uniform_u, (uint32_t)SpvStorageClassUniform, c.u);
    op(w, SpvOpTypePointer, 3, c.push_v4, (uint32_t)SpvStorageClassPushConstant, c.v4);
    op(w, SpvOpVariable, 3, c.ubo_p, c.ubo, (uint32_t)SpvStorageClassUniform);
    if (pc) { op(w, SpvOpVariable, 3, c.pc_p, c.pc, (uint32_t)SpvStorageClassPushConstant); }
    for (unsigned i = 0; i < 16; i++) {
        op(w, SpvOpVariable, 3, state->compressed_attrs & (1u << i) ? c.in_i : c.in_v4,
           c.inputs[i], (uint32_t)SpvStorageClassInput);
    }
    for (unsigned i = 0; i < 13; i++) {
        op(w, SpvOpVariable, 3, i == 4 || i == 12 ? c.out_f : c.out_v4,
           c.outputs[i], (uint32_t)SpvStorageClassOutput);
    }
    op(w, SpvOpVariable, 3, c.out_v4, c.position, (uint32_t)SpvStorageClassOutput);
    op(w, SpvOpVariable, 3, c.out_f, c.point_size, (uint32_t)SpvStorageClassOutput);
    op(w, SpvOpFunction, 4, c.void_t, c.main, (uint32_t)SpvFunctionControlMaskNone, c.fn_t);
    op(w, SpvOpLabel, 1, id(w));
    for (unsigned i = 0; i < 12; i++) { c.r[i] = c.zero4; }
    for (unsigned i = 0; i < 13; i++) { c.o[i] = c.initial4; }
    c.a0 = c.ci[0];
    if (state->is_fixed_function) { unlit(&c); } else { program(&c); }
    finish(&c);
    op(w, SpvOpReturn, 0); op(w, SpvOpFunctionEnd, 0);
    if (w->words) { w->words[3] = w->next; }
}

static GlslcLayout layout(bool push, bool inline_ubo, unsigned count, bool viewer, bool points, bool dynamic)
{
    static const char *const names[] = {
        "c", "clipRange", "fogCtl", "fogParam", "pointSizeReg", "pointScale", "surfaceSize", "inlineValue", "texgenMode", "pointParams", "texMatEnable"
    };
    const size_t offsets[] = {0, 3072, 3088, 3096, 3104, 3108, 3112, 3120,
                             3120u + inline_ubo * 256u, 3120u + inline_ubo * 256u + viewer * 64u,
                             3120u + inline_ubo * 256u + viewer * 64u + points * 128u};
    static const unsigned dimensions[] = {4, 4, 1, 2, 1, 1, 2, 4, 1, 1, 1};
    unsigned indices[11] = {0, 1, 2, 3, 4, 5, 6}, n = 7;
    if (push) { indices[0] = 7; n = 1; }
    else {
        if (inline_ubo) { indices[n++] = 7; }
        if (viewer) { indices[n++] = 8; }
        if (points) { indices[n++] = 9; }
        if (dynamic) { indices[n++] = 10; }
    }
    size_t bytes = n * sizeof(GlslcUniform);
    for (unsigned i = 0; i < n; i++) { bytes += strlen(names[indices[i]]) + 1; }
    GlslcUniform *members = malloc(bytes);
    if (!members) { abort(); }
    char *text = (char *)(members + n);
    for (unsigned i = 0; i < n; i++) {
        unsigned index = indices[i];
        size_t len = strlen(names[index]) + 1;
        memcpy(text, names[index], len);
        members[i] = (GlslcUniform) {
            .name = text, .dim_v = dimensions[index],
            .dim_a = push ? count : index == 0 ? NV2A_VERTEXSHADER_CONSTANTS : index == 7 ? 16 : index == 8 ? 4 : index == 9 ? 8 : 1,
            .stride = index == 8 || index == 9 ? 16 : dimensions[index] * sizeof(float),
            .offset = push ? 0 : offsets[index],
        };
        text += len;
    }
    return (GlslcLayout) { .members = members, .count = n,
                          .size = push ? count * 16 : 3120u + inline_ubo * 256u + viewer * 64u + points * 128u + dynamic * 16u };
}

bool pgraph_spv_vsh(const VshState *state, SpvVshOptions opts, GlslcResult *out)
{
    memset(out, 0, sizeof(*out));
    if (!pgraph_spv_vsh_supported(state, opts)) { return false; }
    Writer count = { .next = 1 };
    emit(&count, state, opts);
    if (count.precise_count != (needs_precise_fog(state) ? 4u : 0u)) { abort(); }
    uint32_t *words = malloc(count.count * sizeof(*words));
    if (!words) { abort(); }
    Writer output = { .words = words, .capacity = count.count, .next = 1,
                      .precise_mul = {count.precise_mul[0], count.precise_mul[1], count.precise_mul[2], count.precise_mul[3]} };
    emit(&output, state, opts);
    if (output.count != count.count || output.next != count.next || output.precise_count != count.precise_count) { abort(); }
    out->words = words;
    out->num_words = out->allocation_words = output.count;
    out->uniforms = layout(false, !opts.push_constants && state->uniform_attrs, 0, needs_texgen_viewer(state), state->is_fixed_function && state->point_params_enable, state->is_fixed_function && state->fixed_function.texgen_dynamic);
    if (opts.push_constants && state->uniform_attrs) {
        out->push_constants = layout(true, false, __builtin_popcount(state->uniform_attrs), false, false, false);
    }
    return true;
}
