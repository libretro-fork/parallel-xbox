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
    uint32_t precise_distance;
    uint32_t block;
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
    for (unsigned i = 0; i < n; i++) {
        uint32_t operand = va_arg(args, uint32_t);
        if (code == SpvOpLabel && i == 0) { w->block = operand; }
        word(w, operand);
    }
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
    /* Point scaling and the fixed-function matrix path use an explicit FP32 environment.
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
            (unsigned)ff->skinning > SKINNING_4WEIGHTS4MATRICES) {
            return false;
        }
        if (ff->lighting && !ff->csv0c_dynamic) {
            if ((unsigned)ff->ambient_src > 2 || (unsigned)ff->emission_src > 2 ||
                (unsigned)ff->diffuse_src > 2 || (unsigned)ff->specular_src > 2) { return false; }
            for (unsigned i = 0; !ff->light_dynamic && i < NV2A_MAX_LIGHTS; i++) {
                if ((unsigned)ff->light[i] > LIGHT_SPOT) { return false; }
            }
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
            /* The decoder always forms C, so its mux must be valid. Its
             * register/constant text is discarded when neither pipe uses C. */
            unsigned used = (m ? mac_params[m] : 0) | (l ? 4 : 0);
            unsigned decoded = used | 4;
            for (unsigned a = 0; a < 3; a++) {
                if (!(decoded & (1u << a))) { continue; }
                unsigned source = mux(t, a);
                if (!source || ((used & (1u << a)) &&
                    ((source == PARAM_R && reg(t, a) > 12) ||
                     (source == PARAM_C && field(t[1], 13, 8) >=
                                              NV2A_VERTEXSHADER_CONSTANTS)))) {
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

enum LightingUniform { L_CONTEXT, L_ALPHA, L_BACK_ALPHA, L_COLORS, L_DIRECTION, L_HALF,
                       L_SPECULAR, L_BACK_SPECULAR, L_RANGE, L_ATTENUATION, L_POSITION, L_MODE, L_CONTROL, L_COUNT };
typedef struct LightingInfo { const char *name; unsigned width, count; } LightingInfo;
static const LightingInfo lighting_info[L_COUNT] = {
    {"ltctxa",4,NV2A_LTCTXA_COUNT}, {"material_alpha",1,1}, {"back_material_alpha",1,1},
    {"ltctxb",4,NV2A_LTCTXB_COUNT}, {"lightInfiniteDirection",3,NV2A_MAX_LIGHTS},
    {"lightInfiniteHalfVector",3,NV2A_MAX_LIGHTS}, {"specularParams",3,1}, {"specularParamsBack",3,1},
    {"ltc1",4,NV2A_LTC1_COUNT}, {"lightLocalAttenuation",3,NV2A_MAX_LIGHTS}, {"lightLocalPosition",3,NV2A_MAX_LIGHTS},
    {"lightMode",1,1}, {"csv0cCtl",1,1},
};
/* Select existing uniform groups; unused groups need no final-block upload. */
static unsigned lighting_profile(const VshState *s)
{
    if (!s->is_fixed_function) { return 0; }
    if (s->fixed_function.csv0c_dynamic) { return (1u << L_COUNT) - 1u; }
    if (!s->fixed_function.lighting) { return 0; }
    const FixedFunctionVshState *f = &s->fixed_function;
    unsigned mask = 1u << L_CONTEXT;
    if (f->diffuse_src == MATERIAL_COLOR_SRC_MATERIAL) { mask |= 1u << L_ALPHA; }
    if (f->two_sided) { mask |= 1u << L_BACK_ALPHA; }
    bool active = false, infinite = false, local = false;
    for (unsigned i = 0; i < NV2A_MAX_LIGHTS; i++) {
        active |= f->light[i] != LIGHT_OFF;
        infinite |= f->light[i] == LIGHT_INFINITE;
        local |= f->light[i] == LIGHT_LOCAL || f->light[i] == LIGHT_SPOT;
    }
    if (f->light_dynamic) { active = infinite = local = true; mask |= 1u << L_MODE; if (f->two_sided) { mask |= 1u << L_CONTROL; } }
    if (infinite) { mask |= 1u << L_DIRECTION; }
    if (local) { mask |= (1u << L_RANGE) | (1u << L_ATTENUATION) | (1u << L_POSITION); }
    if (active) {
        mask |= 1u << L_COLORS;
        if (s->specular_enable) {
            mask |= 1u << L_SPECULAR;
            if (f->two_sided) { mask |= 1u << L_BACK_SPECULAR; }
            if (infinite && !f->local_eye) { mask |= 1u << L_HALF; }
        }
    }
    return mask;
}
static size_t lighting_offsets(unsigned mask, size_t base, size_t offsets[L_COUNT])
{
    for (unsigned i = 0; i < L_COUNT; i++) {
        if (!(mask & (1u << i))) { continue; }
        const LightingInfo *info = &lighting_info[i];
        unsigned alignment = info->width >= 3 || info->count > 1 ? 16 : 4;
        base = (base + alignment - 1) & ~(size_t)(alignment - 1);
        offsets[i] = base;
        base += info->count > 1 ? info->count * 16 : info->width * 4;
    }
    return (base + 15) & ~(size_t)15;
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
    uint32_t r[12], o[13], a0, input_values[16], skin_weights[4], fog_control, fog_distance, normal_value;
    unsigned lighting_mask, lighting_members[L_COUNT];
    uint32_t lighting_arrays[4], uniform_v3, specular_max;
    uint32_t light_shift, light_mask, back_bit, true_value, false_value, light_control;
} Context;

static uint32_t unary(Context *c, SpvOp code, uint32_t type, uint32_t a)
{ return value(c->w, code, type, 1, a); }
static uint32_t binary(Context *c, SpvOp code, uint32_t type, uint32_t a, uint32_t b)
{ return value(c->w, code, type, 2, a, b); }
static bool csv_live(Context *c) { return c->state->is_fixed_function && c->state->fixed_function.csv0c_dynamic; }
static bool lighting_specular(Context *c) { return csv_live(c) || c->state->specular_enable; }
static bool lighting_back(Context *c) { return csv_live(c) || c->state->fixed_function.two_sided; }
static uint32_t csv_field(Context *c, unsigned shift, uint32_t mask)
{
    uint32_t bits=binary(c,SpvOpShiftRightLogical,c->u,c->light_control,c->ci[shift]);
    return binary(c,SpvOpBitwiseAnd,c->u,bits,mask);
}
static uint32_t csv_flag(Context *c, unsigned shift)
{ return binary(c,SpvOpINotEqual,c->b,csv_field(c,shift,c->cu[1]),c->cu[0]); }
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
    if (c->normal_value) { return c->normal_value; }
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
    if(csv_live(c)){
        uint32_t test=csv_flag(c,2),yes=id(c->w),no=id(c->w),merge=id(c->w);
        op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,test,yes,no);op(c->w,SpvOpLabel,1,yes);
        uint32_t normalized=ext1(c,GLSLstd450Normalize,c->v3,mapped_normal);
        op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
        mapped_normal=value(c->w,SpvOpPhi,c->v3,4,normalized,yes,mapped_normal,no);
    }else if (c->state->fixed_function.normalization) {
        mapped_normal = ext1(c, GLSLstd450Normalize, c->v3, mapped_normal);
    }
    c->normal_value = mapped_normal;
    return mapped_normal;
}

static uint32_t xyz(Context *c, uint32_t v)
{ return value(c->w, SpvOpVectorShuffle, c->v3, 5, v, v, 0u, 1u, 2u); }
static uint32_t with_xyz(Context *c, uint32_t v, uint32_t color)
{ return make_v4(c, extract(c,color,0), extract(c,color,1), extract(c,color,2), extract(c,v,3)); }
static uint32_t lighting_uniform_at(Context *c, unsigned which, uint32_t index)
{
    const LightingInfo *info = &lighting_info[which];
    uint32_t type = which >= L_MODE ? c->u : info->width == 4 ? c->v4 : info->width == 3 ? c->v3 : c->f;
    uint32_t pointer = which >= L_MODE ? c->uniform_u : info->width == 4 ? c->uniform_v4 : info->width == 3 ? c->uniform_v3 : c->uniform_f;
    uint32_t p = info->count > 1 ? value(c->w, SpvOpAccessChain, pointer, 3, c->ubo, c->ci[c->lighting_members[which]], index) :
                                  value(c->w, SpvOpAccessChain, pointer, 2, c->ubo, c->ci[c->lighting_members[which]]);
    return unary(c, SpvOpLoad, type, p);
}
static uint32_t lighting_uniform(Context *c, unsigned which, unsigned index)
{ return lighting_uniform_at(c, which, c->ci[index]); }
static uint32_t light_color(Context *c, unsigned light, unsigned component)
{ return xyz(c, lighting_uniform(c, L_COLORS, light * 6 + component)); }
/* Keep the reciprocal inside the valid coefficient branch, including NaN guards. */
static uint32_t specular_factor(Context *c, uint32_t light_dot, uint32_t half_dot, uint32_t coefficients)
{
    uint32_t x=extract(c,coefficients,0), y=extract(c,coefficients,1), z=extract(c,coefficients,2);
    uint32_t valid=binary(c,SpvOpLogicalAnd,c->b,
        binary(c,SpvOpFOrdGreaterThan,c->b,light_dot,c->cf[F_ZERO]),
        binary(c,SpvOpFOrdGreaterThan,c->b,half_dot,unary(c,SpvOpFNegate,c->f,x)));
    uint32_t yes=id(c->w), no=id(c->w), merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,valid,yes,no);
    op(c->w,SpvOpLabel,1,yes);
    uint32_t denominator=add(c,mul(c,y,half_dot),z);
    uint32_t zero=binary(c,SpvOpFOrdEqual,c->b,denominator,c->cf[F_ZERO]);
    uint32_t zero_label=id(c->w), nonzero=id(c->w), inner=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,inner,0u);op(c->w,SpvOpBranchConditional,3,zero,zero_label,nonzero);
    op(c->w,SpvOpLabel,1,zero_label);op(c->w,SpvOpBranch,1,inner);
    op(c->w,SpvOpLabel,1,nonzero);
    uint32_t positive=binary(c,SpvOpFOrdGreaterThan,c->b,denominator,c->cf[F_ZERO]);
    uint32_t p_label=id(c->w), bad_label=id(c->w), quotient_merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,quotient_merge,0u);op(c->w,SpvOpBranchConditional,3,positive,p_label,bad_label);
    op(c->w,SpvOpLabel,1,p_label);
    uint32_t quotient=clamp(c,c->f,divf(c,add(c,half_dot,x),denominator),c->cf[F_ZERO],c->specular_max);
    op(c->w,SpvOpBranch,1,quotient_merge);op(c->w,SpvOpLabel,1,bad_label);op(c->w,SpvOpBranch,1,quotient_merge);
    op(c->w,SpvOpLabel,1,quotient_merge);
    uint32_t q=value(c->w,SpvOpPhi,c->f,4,quotient,p_label,c->cf[F_ZERO],bad_label);
    op(c->w,SpvOpBranch,1,inner);op(c->w,SpvOpLabel,1,inner);
    uint32_t result=value(c->w,SpvOpPhi,c->f,4,c->specular_max,zero_label,q,quotient_merge);
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,merge);
    op(c->w,SpvOpLabel,1,merge);
    return value(c->w,SpvOpPhi,c->f,4,result,inner,c->cf[F_ZERO],no);
}
/* Bounded host recursion emits the reference's five lazy cutoff branches. */
static uint32_t spot_stage(Context *c, unsigned stage, uint32_t ramp, uint32_t coefficients, uint32_t numerator, uint32_t denominator)
{
    if (stage == 2) { numerator = add(c, ramp, extract(c, coefficients, 0)); }
    if (stage == 3) { denominator = add(c, mul(c, extract(c, coefficients, 1), ramp), extract(c, coefficients, 2)); }
    if (stage == 5) { return clamp(c, c->f, divf(c, numerator, denominator), c->cf[F_ZERO], c->cf[F_ONE]); }
    uint32_t test, yes_value = c->cf[F_ZERO];
    if (stage == 0) { test = binary(c, SpvOpFOrdLessThanEqual, c->b, ramp, c->cf[F_ZERO]); }
    else if (stage == 1) { test = binary(c, SpvOpFOrdGreaterThanEqual, c->b, ramp, c->cf[F_ONE]); yes_value = c->cf[F_ONE]; }
    else if (stage == 2) { test = binary(c, SpvOpFOrdLessThanEqual, c->b, numerator, c->cf[F_ZERO]); }
    else if (stage == 3) { test = binary(c, SpvOpFOrdEqual, c->b, denominator, c->cf[F_ZERO]); yes_value = c->cf[F_ONE]; }
    else { test = binary(c, SpvOpFOrdLessThan, c->b, denominator, c->cf[F_ZERO]); }
    uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
    op(c->w, SpvOpSelectionMerge, 2, merge, 0u); op(c->w, SpvOpBranchConditional, 3, test, yes, no);
    op(c->w, SpvOpLabel, 1, yes); op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, no);
    uint32_t next = spot_stage(c, stage + 1, ramp, coefficients, numerator, denominator);
    uint32_t last = c->w->block;
    op(c->w, SpvOpBranch, 1, merge); op(c->w, SpvOpLabel, 1, merge);
    return value(c->w, SpvOpPhi, c->f, 4, yes_value, yes, next, last);
}
static uint32_t spotlight_attenuation(Context *c, unsigned light, uint32_t direction, uint32_t attenuation, bool dynamic)
{
    uint32_t spot, k;
    if (dynamic) {
        uint32_t base = binary(c, SpvOpIMul, c->i, light, c->ci[2]);
        spot = lighting_uniform_at(c, L_CONTEXT, binary(c, SpvOpIAdd, c->i, base, c->ci[NV_IGRAPH_XF_LTCTXA_L0_SPT]));
        k = lighting_uniform_at(c, L_CONTEXT, binary(c, SpvOpIAdd, c->i, base, c->ci[NV_IGRAPH_XF_LTCTXA_L0_K]));
    } else {
        spot = lighting_uniform(c, L_CONTEXT, NV_IGRAPH_XF_LTCTXA_L0_SPT + light * 2);
        k = lighting_uniform(c, L_CONTEXT, NV_IGRAPH_XF_LTCTXA_L0_K + light * 2);
    }
    uint32_t ramp = add(c, binary(c, SpvOpDot, c->f, xyz(c, spot), direction), extract(c, spot, 3));
    uint32_t factor = spot_stage(c, 0, ramp, k, 0, 0);
    /* A cutoff selects zero; 0 * infinite attenuation would leak a NaN. */
    uint32_t zero = binary(c, SpvOpFOrdEqual, c->b, factor, c->cf[F_ZERO]);
    uint32_t yes = id(c->w), no = id(c->w), merge = id(c->w);
    op(c->w, SpvOpSelectionMerge, 2, merge, 0u); op(c->w, SpvOpBranchConditional, 3, zero, yes, no);
    op(c->w, SpvOpLabel, 1, yes); op(c->w, SpvOpBranch, 1, merge);
    op(c->w, SpvOpLabel, 1, no); uint32_t result = mul(c, attenuation, factor);
    op(c->w, SpvOpBranch, 1, merge); op(c->w, SpvOpLabel, 1, merge);
    return value(c->w, SpvOpPhi, c->f, 4, c->cf[F_ZERO], yes, result, no);
}

static uint32_t light_color_for(Context *c, unsigned light, bool dynamic, unsigned component)
{
    if(!dynamic)return light_color(c,light,component);
    uint32_t row=binary(c,SpvOpIAdd,c->i,binary(c,SpvOpIMul,c->i,light,c->ci[6]),c->ci[component]);
    return xyz(c,lighting_uniform_at(c,L_COLORS,row));
}
static void accumulate_light(Context *c, unsigned i, bool dynamic, uint32_t attenuation, uint32_t raw, uint32_t dot, uint32_t half_raw, uint32_t pf, uint32_t back_params, uint32_t d, uint32_t s)
{
    const FixedFunctionVshState *f=&c->state->fixed_function;
        uint32_t ambient=binary(c,SpvOpVectorTimesScalar,c->v3,light_color_for(c,i,dynamic,NV_IGRAPH_XF_LTCTXB_L0_AMB),attenuation);
        uint32_t lit_diffuse=binary(c,SpvOpVectorTimesScalar,c->v3,binary(c,SpvOpVectorTimesScalar,c->v3,light_color_for(c,i,dynamic,NV_IGRAPH_XF_LTCTXB_L0_DIF),attenuation),dot);
        if(csv_live(c)){
            uint32_t source=csv_field(c,6,c->cu[3]);
            uint32_t one=value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ONE],c->cf[F_ONE],c->cf[F_ONE]);
            uint32_t multiplier=choose(c,c->v3,binary(c,SpvOpIEqual,c->b,source,c->cu[1]),d,choose(c,c->v3,binary(c,SpvOpIEqual,c->b,source,c->cu[2]),s,one));
            ambient=binary(c,SpvOpFMul,c->v3,ambient,multiplier);
        }else if(f->ambient_src!=MATERIAL_COLOR_SRC_MATERIAL)ambient=binary(c,SpvOpFMul,c->v3,ambient,f->ambient_src==MATERIAL_COLOR_SRC_SPECULAR?s:d);
        c->o[3]=with_xyz(c,c->o[3],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[3]),ambient));
        if(lighting_back(c)){
            uint32_t back_before[2]={c->o[7],c->o[8]}, skip=0, merge=0;
            if(dynamic){
                uint32_t enabled=binary(c,SpvOpINotEqual,c->b,binary(c,SpvOpBitwiseAnd,c->u,c->light_control,c->back_bit),c->cu[0]);
                uint32_t yes=id(c->w);skip=id(c->w);merge=id(c->w);
                op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,enabled,yes,skip);op(c->w,SpvOpLabel,1,yes);
            }
            /* Back registers do not use the front material-source multipliers. */
            uint32_t bd=ext2(c,GLSLstd450FMax,c->f,c->cf[F_ZERO],unary(c,SpvOpFNegate,c->f,raw));
            uint32_t a=binary(c,SpvOpVectorTimesScalar,c->v3,light_color_for(c,i,dynamic,NV_IGRAPH_XF_LTCTXB_L0_BAMB),attenuation);
            c->o[7]=with_xyz(c,c->o[7],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[7]),a));
            uint32_t dd=binary(c,SpvOpVectorTimesScalar,c->v3,binary(c,SpvOpVectorTimesScalar,c->v3,light_color_for(c,i,dynamic,NV_IGRAPH_XF_LTCTXB_L0_BDIF),attenuation),bd);
            c->o[7]=with_xyz(c,c->o[7],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[7]),dd));
            if(lighting_specular(c)){
                uint32_t bh=ext2(c,GLSLstd450FMax,c->f,c->cf[F_ZERO],unary(c,SpvOpFNegate,c->f,half_raw));
                uint32_t bpf=specular_factor(c,bd,bh,back_params);
                uint32_t sp=binary(c,SpvOpVectorTimesScalar,c->v3,binary(c,SpvOpVectorTimesScalar,c->v3,light_color_for(c,i,dynamic,NV_IGRAPH_XF_LTCTXB_L0_BSPC),attenuation),bpf);
                c->o[8]=with_xyz(c,c->o[8],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[8]),sp));
            }
            if(dynamic){
                uint32_t last=c->w->block;op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,skip);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
                c->o[7]=value(c->w,SpvOpPhi,c->v4,4,c->o[7],last,back_before[0],skip);
                if(lighting_specular(c))c->o[8]=value(c->w,SpvOpPhi,c->v4,4,c->o[8],last,back_before[1],skip);
            }
        }
        if(csv_live(c)){
            uint32_t source=csv_field(c,8,c->cu[3]);
            uint32_t one=value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ONE],c->cf[F_ONE],c->cf[F_ONE]);
            uint32_t multiplier=choose(c,c->v3,binary(c,SpvOpIEqual,c->b,source,c->cu[1]),d,choose(c,c->v3,binary(c,SpvOpIEqual,c->b,source,c->cu[2]),s,one));
            lit_diffuse=binary(c,SpvOpFMul,c->v3,multiplier,lit_diffuse);
        }else if(f->diffuse_src!=MATERIAL_COLOR_SRC_MATERIAL)lit_diffuse=binary(c,SpvOpFMul,c->v3,f->diffuse_src==MATERIAL_COLOR_SRC_SPECULAR?s:d,lit_diffuse);
        c->o[3]=with_xyz(c,c->o[3],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[3]),lit_diffuse));
        if(lighting_specular(c)){
            uint32_t sp=binary(c,SpvOpVectorTimesScalar,c->v3,binary(c,SpvOpVectorTimesScalar,c->v3,light_color_for(c,i,dynamic,NV_IGRAPH_XF_LTCTXB_L0_SPC),attenuation),pf);
            if(csv_live(c)){
                uint32_t source=csv_field(c,10,c->cu[3]);
                uint32_t one=value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ONE],c->cf[F_ONE],c->cf[F_ONE]);
                uint32_t multiplier=choose(c,c->v3,binary(c,SpvOpIEqual,c->b,source,c->cu[1]),d,choose(c,c->v3,binary(c,SpvOpIEqual,c->b,source,c->cu[2]),s,one));
                sp=binary(c,SpvOpFMul,c->v3,multiplier,sp);
            }else if(f->specular_src!=MATERIAL_COLOR_SRC_MATERIAL)sp=binary(c,SpvOpFMul,c->v3,f->specular_src==MATERIAL_COLOR_SRC_SPECULAR?s:d,sp);
            c->o[4]=with_xyz(c,c->o[4],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[4]),sp));
        }
}

/* Loop temporaries are SSA IDs only; no input/key payload is retained. */
typedef struct LightTerms { uint32_t raw, half_raw, attenuation, valid; } LightTerms;
static uint32_t light_half_dot(Context *c, uint32_t index, bool local, uint32_t direction, uint32_t normal, uint32_t eye)
{
    if(!lighting_specular(c))return c->cf[F_ZERO];
    uint32_t half;
    if(csv_live(c)){
        if(local){half=ext1(c,GLSLstd450Normalize,c->v3,binary(c,SpvOpFAdd,c->v3,direction,eye));}
        else{
            uint32_t test=csv_flag(c,1),yes=id(c->w),no=id(c->w),merge=id(c->w);
            op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,test,yes,no);op(c->w,SpvOpLabel,1,yes);
            uint32_t h=ext1(c,GLSLstd450Normalize,c->v3,binary(c,SpvOpFAdd,c->v3,direction,eye));
            op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);
            uint32_t programmed=lighting_uniform_at(c,L_HALF,index);
            op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);half=value(c->w,SpvOpPhi,c->v3,4,h,yes,programmed,no);
        }
    }else if(local || c->state->fixed_function.local_eye){
        uint32_t other=eye;
        if(!c->state->fixed_function.local_eye)other=value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO]);
        half=ext1(c,GLSLstd450Normalize,c->v3,binary(c,SpvOpFAdd,c->v3,direction,other));
    }else{half=lighting_uniform_at(c,L_HALF,index);}
    return binary(c,SpvOpDot,c->f,normal,half);
}
static LightTerms dynamic_light_terms(Context *c, uint32_t index, uint32_t mode, uint32_t normal, uint32_t eye, uint32_t position)
{
    uint32_t infinite=id(c->w), local=id(c->w), merge=id(c->w);
    uint32_t test=binary(c,SpvOpIEqual,c->b,mode,c->cu[1]);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,test,infinite,local);
    op(c->w,SpvOpLabel,1,infinite);
    uint32_t direction=ext1(c,GLSLstd450Normalize,c->v3,lighting_uniform_at(c,L_DIRECTION,index));
    LightTerms inf={.raw=binary(c,SpvOpDot,c->f,normal,direction),.attenuation=c->cf[F_ONE],.valid=c->true_value};
    inf.half_raw=light_half_dot(c,index,false,direction,normal,eye);
    uint32_t inf_last=c->w->block;op(c->w,SpvOpBranch,1,merge);
    op(c->w,SpvOpLabel,1,local);
    uint32_t w=extract(c,position,3), divisor=value(c->w,SpvOpCompositeConstruct,c->v3,3,w,w,w);
    uint32_t view=binary(c,SpvOpFDiv,c->v3,xyz(c,position),divisor);
    uint32_t delta=binary(c,SpvOpFSub,c->v3,lighting_uniform_at(c,L_POSITION,index),view);
    /* A static local viewer shares the rounded divide with VPeye even when
     * specular is dead. Live CSV0_C computes VPeye conditionally and does not
     * impose that boundary on its local-light subtraction. */
    if(!csv_live(c) && c->state->fixed_function.local_eye){
        if(c->w->words && c->w->precise_distance!=delta){abort();}
        c->w->precise_distance=delta;
    }
    uint32_t distance=ext1(c,GLSLstd450Length,c->f,delta);
    uint32_t range_index=binary(c,SpvOpIAdd,c->i,index,c->ci[NV_IGRAPH_XF_LTC1_r0]);
    uint32_t range=extract(c,lighting_uniform_at(c,L_RANGE,range_index),0);
    /* Dynamic GLSL continues only for d > range: NaN must reach the body. */
    uint32_t outside=binary(c,SpvOpFOrdGreaterThan,c->b,distance,range);
    uint32_t yes=id(c->w), no=id(c->w), range_merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,range_merge,0u);op(c->w,SpvOpBranchConditional,3,outside,no,yes);
    op(c->w,SpvOpLabel,1,yes);
    direction=ext1(c,GLSLstd450Normalize,c->v3,delta);
    uint32_t a=lighting_uniform_at(c,L_ATTENUATION,index);
    uint32_t polynomial=add(c,add(c,extract(c,a,0),mul(c,extract(c,a,1),distance)),mul(c,mul(c,extract(c,a,2),distance),distance));
    LightTerms loc={.attenuation=divf(c,c->cf[F_ONE],polynomial)};
    loc.raw=binary(c,SpvOpDot,c->f,normal,direction);
    loc.half_raw=light_half_dot(c,index,true,direction,normal,eye);
    uint32_t spot=id(c->w), not_spot=id(c->w), spot_merge=id(c->w);
    test=binary(c,SpvOpIEqual,c->b,mode,c->cu[3]);
    op(c->w,SpvOpSelectionMerge,2,spot_merge,0u);op(c->w,SpvOpBranchConditional,3,test,spot,not_spot);
    op(c->w,SpvOpLabel,1,spot);
    uint32_t atten=spotlight_attenuation(c,index,direction,loc.attenuation,true);
    uint32_t spot_last=c->w->block;op(c->w,SpvOpBranch,1,spot_merge);
    op(c->w,SpvOpLabel,1,not_spot);op(c->w,SpvOpBranch,1,spot_merge);op(c->w,SpvOpLabel,1,spot_merge);
    loc.attenuation=value(c->w,SpvOpPhi,c->f,4,atten,spot_last,loc.attenuation,not_spot);
    uint32_t local_last=c->w->block;op(c->w,SpvOpBranch,1,range_merge);
    op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,range_merge);op(c->w,SpvOpLabel,1,range_merge);
    loc.raw=value(c->w,SpvOpPhi,c->f,4,loc.raw,local_last,c->cf[F_ZERO],no);
    loc.half_raw=value(c->w,SpvOpPhi,c->f,4,loc.half_raw,local_last,c->cf[F_ZERO],no);
    loc.attenuation=value(c->w,SpvOpPhi,c->f,4,loc.attenuation,local_last,c->cf[F_ZERO],no);
    loc.valid=value(c->w,SpvOpPhi,c->b,4,c->true_value,local_last,c->false_value,no);
    local_last=c->w->block;op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
    return (LightTerms){
        .raw=value(c->w,SpvOpPhi,c->f,4,inf.raw,inf_last,loc.raw,local_last),
        .half_raw=value(c->w,SpvOpPhi,c->f,4,inf.half_raw,inf_last,loc.half_raw,local_last),
        .attenuation=value(c->w,SpvOpPhi,c->f,4,inf.attenuation,inf_last,loc.attenuation,local_last),
        .valid=value(c->w,SpvOpPhi,c->b,4,inf.valid,inf_last,loc.valid,local_last)};
}
static void dynamic_lights(Context *c, uint32_t normal, uint32_t eye, uint32_t params, uint32_t back_params, uint32_t d, uint32_t s, uint32_t *position)
{
    /* Uniform production bounds count to the highest active slot, at most 8. */
    uint32_t modes=lighting_uniform(c,L_MODE,0);
    if(lighting_back(c) && !c->light_control)c->light_control=lighting_uniform(c,L_CONTROL,0);
    uint32_t count=binary(c,SpvOpBitwiseAnd,c->u,binary(c,SpvOpShiftRightLogical,c->u,modes,c->light_shift),c->light_mask);
    count=unary(c,SpvOpBitcast,c->i,count);
    if(!*position)*position=transform(c,input(c,0),NV_IGRAPH_XF_XFCTX_MMAT0);
    uint32_t entry=c->w->block, header=id(c->w), body=id(c->w), cont=id(c->w), merge=id(c->w), index=id(c->w), next=id(c->w);
    static const unsigned regs[]={3,4,7,8};
    uint32_t initial[4],carried[4],result[4];
    for(unsigned j=0;j<4;j++){initial[j]=c->o[regs[j]];carried[j]=id(c->w);result[j]=id(c->w);}
    op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,header);
    op(c->w,SpvOpPhi,6,c->i,index,c->ci[0],entry,next,cont);
    for(unsigned j=0;j<4;j++){op(c->w,SpvOpPhi,6,c->v4,carried[j],initial[j],entry,result[j],cont);c->o[regs[j]]=carried[j];}
    uint32_t within=binary(c,SpvOpSLessThan,c->b,index,count);
    op(c->w,SpvOpLoopMerge,3,merge,cont,0u);op(c->w,SpvOpBranchConditional,3,within,body,merge);
    op(c->w,SpvOpLabel,1,body);
    uint32_t shift=binary(c,SpvOpIMul,c->i,index,c->ci[2]);
    uint32_t mode=binary(c,SpvOpBitwiseAnd,c->u,binary(c,SpvOpShiftRightLogical,c->u,modes,shift),c->cu[3]);
    uint32_t enabled=binary(c,SpvOpINotEqual,c->b,mode,c->cu[0]);
    uint32_t active=id(c->w), active_merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,active_merge,0u);op(c->w,SpvOpBranchConditional,3,enabled,active,active_merge);
    op(c->w,SpvOpLabel,1,active);
    LightTerms terms=dynamic_light_terms(c,index,mode,normal,eye,*position);
    uint32_t geometry_last=c->w->block, use=id(c->w), use_merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,use_merge,0u);op(c->w,SpvOpBranchConditional,3,terms.valid,use,use_merge);
    op(c->w,SpvOpLabel,1,use);
    uint32_t dot=ext2(c,GLSLstd450FMax,c->f,c->cf[F_ZERO],terms.raw), pf=0;
    if(lighting_specular(c))pf=specular_factor(c,dot,ext2(c,GLSLstd450FMax,c->f,c->cf[F_ZERO],terms.half_raw),params);
    accumulate_light(c,index,true,terms.attenuation,terms.raw,dot,terms.half_raw,pf,back_params,d,s);
    uint32_t last=c->w->block;op(c->w,SpvOpBranch,1,use_merge);op(c->w,SpvOpLabel,1,use_merge);
    for(unsigned j=0;j<4;j++)c->o[regs[j]]=value(c->w,SpvOpPhi,c->v4,4,c->o[regs[j]],last,carried[j],geometry_last);
    last=c->w->block;op(c->w,SpvOpBranch,1,active_merge);op(c->w,SpvOpLabel,1,active_merge);
    for(unsigned j=0;j<4;j++)op(c->w,SpvOpPhi,6,c->v4,result[j],c->o[regs[j]],last,carried[j],body);
    op(c->w,SpvOpBranch,1,cont);op(c->w,SpvOpLabel,1,cont);
    op(c->w,SpvOpIAdd,4,c->i,next,index,c->ci[1]);op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,merge);
    for(unsigned j=0;j<4;j++)c->o[regs[j]]=carried[j];
}

static uint32_t csv_base_color(Context *c, uint32_t ambient_source, uint32_t emission_source, uint32_t scene, uint32_t multiplier, uint32_t d, uint32_t s)
{
    uint32_t ambient=choose(c,c->v3,binary(c,SpvOpIEqual,c->b,ambient_source,c->cu[2]),s,d);
    uint32_t emission=choose(c,c->v3,binary(c,SpvOpIEqual,c->b,emission_source,c->cu[2]),s,d);
    uint32_t material=binary(c,SpvOpIEqual,c->b,emission_source,c->cu[0]);
    uint32_t test=binary(c,SpvOpIEqual,c->b,ambient_source,c->cu[0]),yes=id(c->w),no=id(c->w),merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,test,yes,no);op(c->w,SpvOpLabel,1,yes);
    uint32_t zero=id(c->w),nonzero=id(c->w),term_merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,term_merge,0u);op(c->w,SpvOpBranchConditional,3,material,zero,nonzero);
    op(c->w,SpvOpLabel,1,zero);uint32_t z=value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO]);
    op(c->w,SpvOpBranch,1,term_merge);op(c->w,SpvOpLabel,1,nonzero);
    uint32_t product=binary(c,SpvOpFMul,c->v3,emission,multiplier);
    op(c->w,SpvOpBranch,1,term_merge);op(c->w,SpvOpLabel,1,term_merge);
    uint32_t term=value(c->w,SpvOpPhi,c->v3,4,z,zero,product,nonzero);
    uint32_t a=binary(c,SpvOpFAdd,c->v3,scene,term),last=c->w->block;op(c->w,SpvOpBranch,1,merge);
    op(c->w,SpvOpLabel,1,no);
    uint32_t b=binary(c,SpvOpFAdd,c->v3,binary(c,SpvOpFMul,c->v3,ambient,multiplier),choose(c,c->v3,material,scene,emission));
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
    return value(c->w,SpvOpPhi,c->v3,4,a,last,b,no);
}
static void merge_colors(Context *c, const uint32_t before[4], uint32_t yes, uint32_t no)
{
    static const unsigned regs[]={3,4,7,8};
    for(unsigned j=0;j<4;j++)c->o[regs[j]]=value(c->w,SpvOpPhi,c->v4,4,c->o[regs[j]],yes,before[j],no);
}
static void csv_lighting(Context *c, uint32_t *position)
{
    static const unsigned regs[]={3,4,7,8};
    uint32_t before[4];for(unsigned j=0;j<4;j++)before[j]=c->o[regs[j]];
    uint32_t diffuse=input(c,3),specular=input(c,4),back_specular=input(c,8),d=xyz(c,diffuse),s=xyz(c,specular);
    /* This SSA position also serves point/fog consumers after the master merge. */
    if(!*position)*position=transform(c,input(c,0),NV_IGRAPH_XF_XFCTX_MMAT0);
    uint32_t master=csv_flag(c,0),yes=id(c->w),no=id(c->w),merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,master,yes,no);op(c->w,SpvOpLabel,1,yes);
    uint32_t scene=xyz(c,lighting_uniform(c,L_CONTEXT,NV_IGRAPH_XF_LTCTXA_FR_AMB));
    uint32_t multiplier=xyz(c,lighting_uniform(c,L_CONTEXT,NV_IGRAPH_XF_LTCTXA_CM_COL));
    uint32_t base=csv_base_color(c,csv_field(c,6,c->cu[3]),csv_field(c,4,c->cu[3]),scene,multiplier,d,s);
    uint32_t source=csv_field(c,8,c->cu[3]);
    uint32_t alpha=choose(c,c->f,binary(c,SpvOpIEqual,c->b,source,c->cu[0]),lighting_uniform(c,L_ALPHA,0),choose(c,c->f,binary(c,SpvOpIEqual,c->b,source,c->cu[2]),extract(c,specular,3),extract(c,diffuse,3)));
    c->o[3]=make_v4(c,extract(c,base,0),extract(c,base,1),extract(c,base,2),alpha);
    c->o[4]=make_v4(c,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO],extract(c,specular,3));
    uint32_t back_before[4];for(unsigned j=0;j<4;j++)back_before[j]=c->o[regs[j]];
    uint32_t back=csv_flag(c,15),by=id(c->w),bn=id(c->w),bm=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,bm,0u);op(c->w,SpvOpBranchConditional,3,back,by,bn);op(c->w,SpvOpLabel,1,by);
    uint32_t back_base=xyz(c,lighting_uniform(c,L_CONTEXT,NV_IGRAPH_XF_LTCTXA_BR_AMB));
    c->o[7]=make_v4(c,extract(c,back_base,0),extract(c,back_base,1),extract(c,back_base,2),lighting_uniform(c,L_BACK_ALPHA,0));
    c->o[8]=make_v4(c,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO],extract(c,back_specular,3));
    op(c->w,SpvOpBranch,1,bm);op(c->w,SpvOpLabel,1,bn);op(c->w,SpvOpBranch,1,bm);op(c->w,SpvOpLabel,1,bm);merge_colors(c,back_before,by,bn);
    uint32_t eye_test=csv_flag(c,1),ey=id(c->w),en=id(c->w),em=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,em,0u);op(c->w,SpvOpBranchConditional,3,eye_test,ey,en);op(c->w,SpvOpLabel,1,ey);
    uint32_t e=constant(c,NV_IGRAPH_XF_XFCTX_EYEP),ew=extract(c,e,3),pw=extract(c,*position,3);
    uint32_t ev=value(c->w,SpvOpCompositeConstruct,c->v3,3,ew,ew,ew),pv=value(c->w,SpvOpCompositeConstruct,c->v3,3,pw,pw,pw);
    uint32_t ep=binary(c,SpvOpFDiv,c->v3,xyz(c,e),ev),vp=binary(c,SpvOpFDiv,c->v3,xyz(c,*position),pv);
    uint32_t eye=ext1(c,GLSLstd450Normalize,c->v3,binary(c,SpvOpFSub,c->v3,ep,vp));
    op(c->w,SpvOpBranch,1,em);op(c->w,SpvOpLabel,1,en);
    uint32_t zero=value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO]);
    op(c->w,SpvOpBranch,1,em);op(c->w,SpvOpLabel,1,em);eye=value(c->w,SpvOpPhi,c->v3,4,eye,ey,zero,en);
    uint32_t normal=transformed_normal(c),params=lighting_uniform(c,L_SPECULAR,0),back_params=lighting_uniform(c,L_BACK_SPECULAR,0);
    dynamic_lights(c,normal,eye,params,back_params,d,s,position);
    uint32_t last=c->w->block;op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);merge_colors(c,before,last,no);
    /* The live specular postlude also applies when lighting is disabled. */
    for(unsigned j=0;j<4;j++)before[j]=c->o[regs[j]];
    uint32_t enabled=csv_flag(c,12),sy=id(c->w),sn=id(c->w),sm=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,sm,0u);op(c->w,SpvOpBranchConditional,3,enabled,sy,sn);op(c->w,SpvOpLabel,1,sy);
    uint32_t sep_before[4];for(unsigned j=0;j<4;j++)sep_before[j]=c->o[regs[j]];
    uint32_t separate=csv_flag(c,13),keep=id(c->w),combine=id(c->w),cm=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,cm,0u);op(c->w,SpvOpBranchConditional,3,separate,keep,combine);op(c->w,SpvOpLabel,1,combine);
    uint32_t lit_before[4];for(unsigned j=0;j<4;j++)lit_before[j]=c->o[regs[j]];
    uint32_t add_yes=id(c->w),add_no=id(c->w),am=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,am,0u);op(c->w,SpvOpBranchConditional,3,master,add_yes,add_no);op(c->w,SpvOpLabel,1,add_yes);
    c->o[3]=with_xyz(c,c->o[3],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[3]),xyz(c,c->o[4])));
    c->o[7]=with_xyz(c,c->o[7],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[7]),xyz(c,c->o[8])));
    op(c->w,SpvOpBranch,1,am);op(c->w,SpvOpLabel,1,add_no);op(c->w,SpvOpBranch,1,am);op(c->w,SpvOpLabel,1,am);merge_colors(c,lit_before,add_yes,add_no);
    c->o[4]=specular;c->o[8]=back_specular;last=c->w->block;op(c->w,SpvOpBranch,1,cm);
    op(c->w,SpvOpLabel,1,keep);op(c->w,SpvOpBranch,1,cm);op(c->w,SpvOpLabel,1,cm);merge_colors(c,sep_before,last,keep);
    uint32_t ignore=csv_flag(c,14);
    c->o[4]=make_v4(c,extract(c,c->o[4],0),extract(c,c->o[4],1),extract(c,c->o[4],2),choose(c,c->f,ignore,c->cf[F_ONE],extract(c,c->o[4],3)));
    c->o[8]=make_v4(c,extract(c,c->o[8],0),extract(c,c->o[8],1),extract(c,c->o[8],2),choose(c,c->f,ignore,c->cf[F_ONE],extract(c,c->o[8],3)));
    uint32_t spec_yes[4];for(unsigned j=0;j<4;j++)spec_yes[j]=c->o[regs[j]];last=c->w->block;op(c->w,SpvOpBranch,1,sm);
    op(c->w,SpvOpLabel,1,sn);c->o[4]=c->initial4;c->o[8]=c->initial4;
    uint32_t spec_no[4];for(unsigned j=0;j<4;j++)spec_no[j]=j==1||j==3?c->initial4:before[j];
    op(c->w,SpvOpBranch,1,sm);op(c->w,SpvOpLabel,1,sm);
    for(unsigned j=0;j<4;j++)c->o[regs[j]]=value(c->w,SpvOpPhi,c->v4,4,spec_yes[j],last,spec_no[j],sn);
}

static void static_lighting(Context *c, uint32_t *eye_position)
{
    const FixedFunctionVshState *f=&c->state->fixed_function;
    uint32_t diffuse=input(c,3), specular=input(c,4), back_diffuse=input(c,7), back_specular=input(c,8);
    uint32_t d=xyz(c,diffuse), s=xyz(c,specular);
    uint32_t scene=xyz(c,lighting_uniform(c,L_CONTEXT,NV_IGRAPH_XF_LTCTXA_FR_AMB));
    uint32_t multiplier=xyz(c,lighting_uniform(c,L_CONTEXT,NV_IGRAPH_XF_LTCTXA_CM_COL));
    uint32_t emission=f->emission_src==MATERIAL_COLOR_SRC_SPECULAR?s:d, color;
    if(f->ambient_src==MATERIAL_COLOR_SRC_MATERIAL){
        uint32_t term=f->emission_src==MATERIAL_COLOR_SRC_MATERIAL ? value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO]) : binary(c,SpvOpFMul,c->v3,emission,multiplier);
        color=binary(c,SpvOpFAdd,c->v3,scene,term);
    }else{
        uint32_t ambient=f->ambient_src==MATERIAL_COLOR_SRC_SPECULAR?s:d;
        color=binary(c,SpvOpFAdd,c->v3,binary(c,SpvOpFMul,c->v3,ambient,multiplier),f->emission_src==MATERIAL_COLOR_SRC_MATERIAL?scene:emission);
    }
    uint32_t alpha=f->diffuse_src==MATERIAL_COLOR_SRC_MATERIAL?lighting_uniform(c,L_ALPHA,0):extract(c,f->diffuse_src==MATERIAL_COLOR_SRC_SPECULAR?specular:diffuse,3);
    c->o[3]=make_v4(c,extract(c,color,0),extract(c,color,1),extract(c,color,2),alpha);
    c->o[4]=make_v4(c,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO],extract(c,specular,3));
    c->o[7]=back_diffuse;c->o[8]=back_specular;
    if(f->two_sided){
        uint32_t back=xyz(c,lighting_uniform(c,L_CONTEXT,NV_IGRAPH_XF_LTCTXA_BR_AMB));
        c->o[7]=make_v4(c,extract(c,back,0),extract(c,back,1),extract(c,back,2),lighting_uniform(c,L_BACK_ALPHA,0));
        c->o[8]=make_v4(c,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO],extract(c,back_specular,3));
    }
    bool active=!!(c->lighting_mask&(1u<<L_COLORS));
    uint32_t normal=0, eye=0, params=0, back_params=0, position_view=0;
    if(active){
        normal=transformed_normal(c);
        if(c->state->specular_enable){
            params=lighting_uniform(c,L_SPECULAR,0);
            if(f->two_sided)back_params=lighting_uniform(c,L_BACK_SPECULAR,0);
            if(f->local_eye){
                if(!*eye_position)*eye_position=transform(c,input(c,0),NV_IGRAPH_XF_XFCTX_MMAT0);
                uint32_t e=constant(c,NV_IGRAPH_XF_XFCTX_EYEP);
                uint32_t ew=extract(c,e,3), pw=extract(c,*eye_position,3);
                uint32_t ev=value(c->w,SpvOpCompositeConstruct,c->v3,3,ew,ew,ew);
                uint32_t pv=value(c->w,SpvOpCompositeConstruct,c->v3,3,pw,pw,pw);
                uint32_t ep=binary(c,SpvOpFDiv,c->v3,xyz(c,e),ev);
                uint32_t p=binary(c,SpvOpFDiv,c->v3,xyz(c,*eye_position),pv);
                position_view=p;
                eye=ext1(c,GLSLstd450Normalize,c->v3,binary(c,SpvOpFSub,c->v3,ep,p));
            }
        }
    }
    if(f->light_dynamic){dynamic_lights(c,normal,eye,params,back_params,d,s,eye_position);}else
    for(unsigned i=0;i<NV2A_MAX_LIGHTS;i++){
        if(f->light[i]==LIGHT_OFF)continue;
        bool local=f->light[i]==LIGHT_LOCAL || f->light[i]==LIGHT_SPOT;
        uint32_t before[4]={c->o[3],c->o[4],c->o[7],c->o[8]};
        uint32_t range_merge=0, outside=0, direction, attenuation=c->cf[F_ONE];
        if(local){
            /* Keep the homogeneous divide at the first local-light use,
             * after preceding infinite lights, as in the reference generator. */
            if(!position_view){
                if(!*eye_position)*eye_position=transform(c,input(c,0),NV_IGRAPH_XF_XFCTX_MMAT0);
                uint32_t w=extract(c,*eye_position,3);
                uint32_t divisor=value(c->w,SpvOpCompositeConstruct,c->v3,3,w,w,w);
                position_view=binary(c,SpvOpFDiv,c->v3,xyz(c,*eye_position),divisor);
            }
            uint32_t delta=binary(c,SpvOpFSub,c->v3,lighting_uniform(c,L_POSITION,i),position_view);
            uint32_t distance=ext1(c,GLSLstd450Length,c->f,delta);
            uint32_t range=extract(c,lighting_uniform(c,L_RANGE,NV_IGRAPH_XF_LTC1_r0+i),0);
            uint32_t within=binary(c,SpvOpFOrdLessThanEqual,c->b,distance,range);
            uint32_t inside=id(c->w);outside=id(c->w);range_merge=id(c->w);
            op(c->w,SpvOpSelectionMerge,2,range_merge,0u);op(c->w,SpvOpBranchConditional,3,within,inside,outside);
            op(c->w,SpvOpLabel,1,inside);
            direction=ext1(c,GLSLstd450Normalize,c->v3,delta);
            uint32_t a=lighting_uniform(c,L_ATTENUATION,i);
            uint32_t polynomial=add(c,add(c,extract(c,a,0),mul(c,extract(c,a,1),distance)),mul(c,mul(c,extract(c,a,2),distance),distance));
            attenuation=divf(c,c->cf[F_ONE],polynomial);
        }else{
            direction=ext1(c,GLSLstd450Normalize,c->v3,lighting_uniform(c,L_DIRECTION,i));
        }
        uint32_t raw=binary(c,SpvOpDot,c->f,normal,direction);
        uint32_t dot=ext2(c,GLSLstd450FMax,c->f,c->cf[F_ZERO],raw), half_raw=0, pf=0;
        if(c->state->specular_enable){
            /* Infinite half vectors are programmed values, not normalized here. */
            uint32_t half;
            if(local || f->local_eye){
                uint32_t other=eye;
                if(!f->local_eye)other=value(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO]);
                half=ext1(c,GLSLstd450Normalize,c->v3,binary(c,SpvOpFAdd,c->v3,direction,other));
            }else{half=lighting_uniform(c,L_HALF,i);}
            half_raw=binary(c,SpvOpDot,c->f,normal,half);
            pf=specular_factor(c,dot,ext2(c,GLSLstd450FMax,c->f,c->cf[F_ZERO],half_raw),params);
        }
        if(f->light[i]==LIGHT_SPOT)attenuation=spotlight_attenuation(c,i,direction,attenuation,false);
        accumulate_light(c,i,false,attenuation,raw,dot,half_raw,pf,back_params,d,s);
        if(local){
            uint32_t last=c->w->block;
            op(c->w,SpvOpBranch,1,range_merge);op(c->w,SpvOpLabel,1,outside);op(c->w,SpvOpBranch,1,range_merge);op(c->w,SpvOpLabel,1,range_merge);
            static const unsigned color_regs[]={3,4,7,8};
            for(unsigned j=0;j<4;j++)if(c->o[color_regs[j]]!=before[j])c->o[color_regs[j]]=value(c->w,SpvOpPhi,c->v4,4,c->o[color_regs[j]],last,before[j],outside);
        }
    }
    if(c->state->specular_enable && !c->state->separate_specular){
        c->o[3]=with_xyz(c,c->o[3],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[3]),xyz(c,c->o[4])));
        c->o[7]=with_xyz(c,c->o[7],binary(c,SpvOpFAdd,c->v3,xyz(c,c->o[7]),xyz(c,c->o[8])));
        c->o[4]=specular;c->o[8]=back_specular;
    }
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

static void fixed_function(Context *c)
{
    static const unsigned tex_matrix[] = {
        NV_IGRAPH_XF_XFCTX_T0MAT, NV_IGRAPH_XF_XFCTX_T1MAT,
        NV_IGRAPH_XF_XFCTX_T2MAT, NV_IGRAPH_XF_XFCTX_T3MAT,
    };
    static const unsigned tex_plane[] = {
        NV_IGRAPH_XF_XFCTX_TG0MAT, NV_IGRAPH_XF_XFCTX_TG1MAT,
        NV_IGRAPH_XF_XFCTX_TG2MAT, NV_IGRAPH_XF_XFCTX_TG3MAT,
    };
    if(csv_live(c))c->light_control=lighting_uniform(c,L_CONTROL,0);
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
    if(csv_live(c)){csv_lighting(c,&eye_position);}
    else if (c->state->fixed_function.lighting) { static_lighting(c, &eye_position); }
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
    if (csv_live(c) || c->state->specular_enable) {
        d1 = color(c, c->o[4]); b1 = color(c, c->o[8]);
        if (!csv_live(c) && c->state->ignore_specular_alpha) {
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
    if (c->lighting_mask) {
        c->lighting_arrays[0]=id(w);c->lighting_arrays[1]=id(w);c->lighting_arrays[2]=id(w);
        c->uniform_v3=id(w);c->specular_max=id(w);
        if(c->lighting_mask&(1u<<L_RANGE))c->lighting_arrays[3]=id(w);
        if(c->lighting_mask&(1u<<L_MODE)){c->light_shift=id(w);c->light_mask=id(w);c->back_bit=id(w);c->true_value=id(w);c->false_value=id(w);}
    }
}
static void emit(Writer *w, const VshState *state, SpvVshOptions opts)
{
    Context c = { .w = w, .state = state, .opts = opts, .lighting_mask = lighting_profile(state) };
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
    if (c.lighting_mask) {
        size_t offsets[L_COUNT]={0};
        lighting_offsets(c.lighting_mask,3120u+inline_ubo*256u+viewer*64u+points*128u+dynamic*16u,offsets);
        unsigned member=7u+inline_ubo+viewer+points+dynamic;
        for(unsigned i=0;i<L_COUNT;i++)if(c.lighting_mask&(1u<<i)){
            c.lighting_members[i]=member;
            op(w,SpvOpMemberDecorate,4,c.ubo_t,member++,(uint32_t)SpvDecorationOffset,(uint32_t)offsets[i]);
        }
        for(unsigned i=0;i<4;i++)if(c.lighting_arrays[i])op(w,SpvOpDecorate,3,c.lighting_arrays[i],(uint32_t)SpvDecorationArrayStride,16u);
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
    if((c.lighting_mask&(1u<<L_MODE)) && !state->fixed_function.csv0c_dynamic && state->fixed_function.local_eye)op(w,SpvOpDecorate,2,w->precise_distance,(uint32_t)SpvDecorationNoContraction);
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
        if(c.lighting_mask){
            used[NV2A_LTCTXA_COUNT]=true;used[NV2A_LTCTXB_COUNT]=true;
            for(unsigned i=0;i<NV2A_MAX_LIGHTS*6;i++)used[i]=true;
            if((c.lighting_mask&(1u<<L_COLORS)) && (state->fixed_function.csv0c_dynamic || (state->specular_enable && state->fixed_function.local_eye)))used[NV_IGRAPH_XF_XFCTX_EYEP]=true;
        }
        if (state->fog_dynamic) { used[NV_IGRAPH_XF_XFCTX_FOG] = true; }
        unsigned bones = skin_matrices(state->fixed_function.skinning);
        if(c.lighting_mask&(1u<<L_COLORS))for(unsigned b=0;b<(bones?bones:1);b++)for(unsigned j=0;j<4;j++)used[NV_IGRAPH_XF_XFCTX_IMMAT0+b*8+j]=true;
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
    if(c.lighting_mask&(1u<<L_MODE)){
        op(w,SpvOpConstant,3,c.u,c.light_shift,16u);op(w,SpvOpConstant,3,c.u,c.light_mask,15u);op(w,SpvOpConstant,3,c.u,c.back_bit,32768u);
        op(w,SpvOpConstantTrue,2,c.b,c.true_value);op(w,SpvOpConstantFalse,2,c.b,c.false_value);
    }
    if(c.lighting_mask){
        op(w,SpvOpTypeArray,3,c.lighting_arrays[0],c.v4,c.ci[NV2A_LTCTXA_COUNT]);
        op(w,SpvOpTypeArray,3,c.lighting_arrays[1],c.v4,c.ci[NV2A_LTCTXB_COUNT]);
        op(w,SpvOpTypeArray,3,c.lighting_arrays[2],c.v3,c.ci[NV2A_MAX_LIGHTS]);
        op(w,SpvOpConstant,3,c.f,c.specular_max,0x461c4000u);
        if(c.lighting_mask&(1u<<L_RANGE))op(w,SpvOpTypeArray,3,c.lighting_arrays[3],c.v4,c.ci[NV2A_LTC1_COUNT]);
    }
    word(w, ((9 + inline_ubo + viewer + points + dynamic + __builtin_popcount(c.lighting_mask)) << 16) | SpvOpTypeStruct);
    word(w, c.ubo_t); word(w, c.c_array); word(w, c.v4); word(w, c.u); word(w, c.v2);
    word(w, c.f); word(w, c.f); word(w, c.v2);
    if (inline_ubo) { word(w, c.inline_array); }
    if (viewer) { word(w, c.texgen_array); }
    if (points) { word(w, c.point_array); }
    if (dynamic) { word(w, c.u); }
    for(unsigned i=0;i<L_COUNT;i++)if(c.lighting_mask&(1u<<i)){
        uint32_t type=lighting_info[i].count>1?(i==L_CONTEXT?c.lighting_arrays[0]:i==L_COLORS?c.lighting_arrays[1]:i==L_RANGE?c.lighting_arrays[3]:c.lighting_arrays[2]):i>=L_MODE?c.u:lighting_info[i].width==3?c.v3:c.f;
        word(w,type);
    }
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
    if(c.lighting_mask)op(w,SpvOpTypePointer,3,c.uniform_v3,(uint32_t)SpvStorageClassUniform,c.v3);
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
    if (state->is_fixed_function) { fixed_function(&c); } else { program(&c); }
    finish(&c);
    op(w, SpvOpReturn, 0); op(w, SpvOpFunctionEnd, 0);
    if (w->words) { w->words[3] = w->next; }
}

static GlslcLayout layout(bool push, bool inline_ubo, unsigned count, bool viewer, bool points, bool dynamic, unsigned lighting)
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
    unsigned base_n=n;
    size_t lit_offsets[L_COUNT]={0};
    size_t total=push?count*16:lighting_offsets(lighting,3120u+inline_ubo*256u+viewer*64u+points*128u+dynamic*16u,lit_offsets);
    n+=__builtin_popcount(lighting);
    size_t bytes = n * sizeof(GlslcUniform);
    for (unsigned i = 0; i < base_n; i++) { bytes += strlen(names[indices[i]]) + 1; }
    for(unsigned i=0;i<L_COUNT;i++)if(lighting&(1u<<i))bytes+=strlen(lighting_info[i].name)+1;
    GlslcUniform *members = malloc(bytes);
    if (!members) { abort(); }
    char *text = (char *)(members + n);
    for (unsigned i = 0; i < base_n; i++) {
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
    unsigned member=base_n;
    for(unsigned i=0;i<L_COUNT;i++)if(lighting&(1u<<i)){
        const LightingInfo *info=&lighting_info[i];size_t len=strlen(info->name)+1;memcpy(text,info->name,len);
        members[member++]=(GlslcUniform){.name=text,.dim_v=info->width,.dim_a=info->count,.stride=info->count>1?16:info->width*4,.offset=lit_offsets[i]};text+=len;
    }
    return (GlslcLayout) { .members = members, .count = n, .size = total };
}

bool pgraph_spv_vsh(const VshState *state, SpvVshOptions opts, GlslcResult *out)
{
    memset(out, 0, sizeof(*out));
    if (!pgraph_spv_vsh_supported(state, opts)) { return false; }
    Writer count = { .next = 1 };
    emit(&count, state, opts);
    if (count.precise_count != (needs_precise_fog(state) ? 4u : 0u)) { abort(); }
    bool precise_local = state->is_fixed_function && !state->fixed_function.csv0c_dynamic && state->fixed_function.lighting && state->fixed_function.light_dynamic && state->fixed_function.local_eye;
    if (!!count.precise_distance != precise_local) { abort(); }
    uint32_t *words = malloc(count.count * sizeof(*words));
    if (!words) { abort(); }
    Writer output = { .words = words, .capacity = count.count, .next = 1,
                      .precise_mul = {count.precise_mul[0], count.precise_mul[1], count.precise_mul[2], count.precise_mul[3]},
                      .precise_distance = count.precise_distance };
    emit(&output, state, opts);
    if (output.count != count.count || output.next != count.next || output.precise_count != count.precise_count || output.precise_distance != count.precise_distance) { abort(); }
    out->words = words;
    out->num_words = out->allocation_words = output.count;
    out->uniforms = layout(false, !opts.push_constants && state->uniform_attrs, 0, needs_texgen_viewer(state), state->is_fixed_function && state->point_params_enable, state->is_fixed_function && state->fixed_function.texgen_dynamic, lighting_profile(state));
    if (opts.push_constants && state->uniform_attrs) {
        out->push_constants = layout(true, false, __builtin_popcount(state->uniform_attrs), false, false, false, 0);
    }
    return true;
}
