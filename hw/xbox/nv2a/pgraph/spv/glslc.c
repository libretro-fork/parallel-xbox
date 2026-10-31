/*
 * GLSL to SPIR-V: the compiler
 *
 * One pass over the tokens: declarations and statements are parsed and
 * their SPIR-V emitted as they are read. Every variable is an OpVariable
 * and every use of it a load, which is what glslang emits before its
 * optimizer too; the driver's compiler does the rest. Control flow is
 * structured as the specification wants it (selection and loop merges),
 * and the few constructs that need to know what comes later (a switch's
 * labels, a for loop's increment) are read ahead in the token list.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <spirv/unified1/spirv.h>
#include "glslc_int.h"

/* GLSL.std.450 extended instructions */
enum {
    GLSL_Round = 1, GLSL_RoundEven = 2, GLSL_Trunc = 3, GLSL_FAbs = 4,
    GLSL_SAbs = 5, GLSL_FSign = 6, GLSL_SSign = 7, GLSL_Floor = 8,
    GLSL_Ceil = 9, GLSL_Fract = 10, GLSL_Radians = 11, GLSL_Degrees = 12,
    GLSL_Sin = 13, GLSL_Cos = 14, GLSL_Tan = 15, GLSL_Asin = 16,
    GLSL_Acos = 17, GLSL_Atan = 18, GLSL_Atan2 = 25, GLSL_Pow = 26,
    GLSL_Exp = 27, GLSL_Log = 28, GLSL_Exp2 = 29, GLSL_Log2 = 30,
    GLSL_Sqrt = 31, GLSL_InverseSqrt = 32, GLSL_Determinant = 33,
    GLSL_MatrixInverse = 34, GLSL_Modf = 35, GLSL_FMin = 37, GLSL_UMin = 38,
    GLSL_SMin = 39, GLSL_FMax = 40, GLSL_UMax = 41, GLSL_SMax = 42,
    GLSL_FClamp = 43, GLSL_UClamp = 44, GLSL_SClamp = 45, GLSL_FMix = 46,
    GLSL_Step = 48, GLSL_SmoothStep = 49, GLSL_Fma = 50, GLSL_Ldexp = 53,
    GLSL_PackSnorm4x8 = 54, GLSL_PackUnorm4x8 = 55, GLSL_PackSnorm2x16 = 56,
    GLSL_PackUnorm2x16 = 57, GLSL_PackHalf2x16 = 58,
    GLSL_UnpackSnorm2x16 = 60, GLSL_UnpackUnorm2x16 = 61,
    GLSL_UnpackHalf2x16 = 62, GLSL_UnpackSnorm4x8 = 63,
    GLSL_UnpackUnorm4x8 = 64, GLSL_Length = 66, GLSL_Distance = 67,
    GLSL_Cross = 68, GLSL_Normalize = 69, GLSL_FaceForward = 70,
    GLSL_Reflect = 71, GLSL_Refract = 72, GLSL_FindILsb = 73,
    GLSL_FindSMsb = 74, GLSL_FindUMsb = 75,
};

/* ---- words ---- */

typedef struct Buf {
    uint32_t *w;
    size_t n, cap;
} Buf;

static void buf_reserve(Buf *b, size_t needed)
{
    const size_t limit = SIZE_MAX / sizeof(uint32_t);
    if (needed > limit) {
        abort();
    }
    if (needed <= b->cap) {
        return;
    }
    size_t cap = b->cap ? b->cap : 1024;
    while (cap < needed) {
        cap = cap > limit / 2 ? needed : cap * 2;
    }
    uint32_t *words = realloc(b->w, cap * sizeof(*words));
    if (!words) {
        abort();
    }
    b->w = words;
    b->cap = cap;
}

static void buf_put(Buf *b, uint32_t word)
{
    if (b->n == SIZE_MAX / sizeof(uint32_t)) {
        abort();
    }
    buf_reserve(b, b->n + 1);
    b->w[b->n++] = word;
}

static void buf_append(Buf *b, const Buf *source)
{
    if (!source->n) {
        return;
    }
    if (source->n > SIZE_MAX / sizeof(uint32_t) - b->n) {
        abort();
    }
    buf_reserve(b, b->n + source->n);
    memcpy(b->w + b->n, source->w, source->n * sizeof(uint32_t));
    b->n += source->n;
}

/* A consumed stream can donate its allocation when the destination is empty. */
static void buf_append_take(Buf *b, Buf *source)
{
    if (!b->n && source->n) {
        free(b->w);
        *b = *source;
    } else {
        buf_append(b, source);
        free(source->w);
    }
    memset(source, 0, sizeof(*source));
}

/* ---- types ---- */

enum TypeKind {
    TY_VOID,
    TY_BOOL,
    TY_INT,
    TY_UINT,
    TY_FLOAT,
    TY_VEC,    /* base: scalar kind, n: 2..4 */
    TY_MAT,    /* float, n columns of rows-vectors */
    TY_ARRAY,  /* elem, count (0: runtime-sized) */
    TY_STRUCT, /* members */
    TY_SAMPLED_IMAGE, /* image + sampler: sampler2D etc. */
    TY_IMAGE,
    TY_FUNCTION,
};

#define MAX_MEMBERS 64

typedef struct Type Type;
struct Type {
    enum TypeKind kind;
    enum TypeKind base; /* vec: scalar kind */
    int n;              /* vec: components; mat: columns; array: count */
    int rows;           /* mat */
    Type *elem;         /* array, pointer, image's sampled type */
    int nmembers;
    Type **members;
    const char **member_names;
    uint32_t *member_offsets; /* laid out blocks */
    bool *member_bool;  /* laid out blocks: declared bool, held as uint */
    int dim;            /* image: SpvDim */
    bool arrayed, depth, ms;
    int sampled;        /* 1 sampled, 2 storage */
    int format;         /* storage image format */
    bool precise_layout;
    uint32_t id;
    uint32_t ptr_id[16]; /* pointer type per storage class, 0 = none */
    uint32_t array_stride; /* laid out arrays */
    uint32_t size, align; /* laid out */
    bool is_block;
};

/* ---- constants ---- */

typedef struct Const {
    uint32_t type_id;
    uint32_t value;
    uint32_t id;
} Const;

/* ---- values ---- */

typedef struct Val {
    Type *t;
    uint32_t id;
} Val;

/* A place: pointer, or a swizzle of a vector place */
typedef struct LVal {
    Type *t;        /* the type of what is read or written through it */
    Type *vec_t;    /* the vector's type when swizzled, else NULL */
    uint32_t ptr;
    int sc;         /* storage class */
    uint8_t swz[4]; /* component indices when swizzled */
    int nswz;
    bool readonly;
    bool precise;   /* what is assigned to it is computed exactly */
    bool as_bool;   /* a block's bool, kept as a word: converted on access */
    Type *store_t;  /* as_bool: the word type held */
} LVal;

/* An expression's result: a value, or a place that can be loaded */
typedef struct Ex {
    bool is_lv;
    LVal lv;
    Val v;
    bool is_const; /* v is a scalar constant: cv holds it */
    uint32_t cv;
} Ex;

/* ---- symbols ---- */

typedef struct Sym {
    const char *name;
    LVal lv;
    int scope;
    int member;      /* >= 0: member of the block variable lv.ptr */
    bool has_const;  /* a scalar constant: cv folds */
    uint32_t cv;
} Sym;

typedef struct Func {
    const char *name;
    Type *ret;
    int nparams;
    Type **params;
    uint8_t *pmode;  /* per parameter: PM_IN, PM_OUT, PM_INOUT */
    uint32_t id;
    uint32_t type_id;
} Func;

#define PM_IN 0
#define PM_OUT 1
#define PM_INOUT 2

/* A declared qualifier set */
typedef struct Quals {
    int sc;          /* SpvStorageClass or -1 for a plain local/global */
    bool is_const, is_precise, is_flat, is_noperspective, is_centroid;
    bool is_in, is_out, is_uniform, is_buffer, is_shared, is_push;
    bool readonly, writeonly;
    bool has_location, has_binding, has_set;
    int location, binding, set;
    bool std140, std430;
    int format;     /* image format or -1 */
    int geom_mode;  /* a geometry primitive layout, or 0 */
    bool geom_points; /* layout(points): in or out decides the mode */
} Quals;

#define MAX_FRAMES 32

struct GlslcCtx {
    Arena arena;
    jmp_buf err;
    char *error;
    size_t error_cap;
    int *error_line;
    GlslcStage stage;
    GlslcOptions opts;

    TokList toks;
    size_t pos;

    /* module sections */
    Buf caps, exts, entry_iface, exec_modes, names, decor, types, funcs;
    uint32_t next_id;
    uint32_t glsl_ext_id;
    bool cap_geometry, cap_image_query, cap_demote, cap_image_read_without_format,
        cap_image_write_without_format, cap_storage_image_ext_formats;

    Type **type_list;
    size_t ntypes, types_cap;
    Const *consts;
    size_t nconsts, consts_cap;
    Type *t_void, *t_bool, *t_int, *t_uint, *t_float;

    Sym *syms;
    size_t nsyms, syms_cap;
    int scope;

    Func *fns;
    size_t nfns, fns_cap;

    /* the function being emitted */
    Buf fvars; /* its OpVariables, for its first block */
    Buf fbody;
    bool in_function;
    Func *cur_fn;
    uint32_t cur_block;
    bool terminated;
    struct {
        uint32_t brk, cont; /* 0 = none */
        bool is_switch;
    } cf[MAX_FRAMES];
    int ncf;
    bool precise_ctx; /* decorate float arithmetic NoContraction */

    /* the entry point */
    uint32_t main_id;
    Buf global_inits; /* OpStore of globals with dynamic initializers */
    Buf ginit_vars;   /* the temporaries those need, main's variables */
    bool have_main;
    uint32_t main_entry_label;

    /* built-in variables made on first use */
    uint32_t bi_position, bi_pointsize, bi_fragcoord, bi_frontfacing,
        bi_pointcoord, bi_fragdepth, bi_primitiveid, bi_globalinv,
        bi_localinv, bi_workgroupid, bi_localinvindex, bi_numworkgroups,
        bi_vertexindex, bi_instanceindex, bi_pervertex_out, bi_gl_in;
    Type *t_pervertex;
    int geom_in_vertices; /* geometry: input primitive's vertex count */
    bool depth_replacing;
    int local_size[3];
    bool have_local_size;
    bool geom_in_set, geom_out_set;
};

typedef GlslcCtx C;

static void fail(C *c, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 2, 3)));

static void fail(C *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->error, c->error_cap, fmt, ap);
    va_end(ap);
    *c->error_line = c->pos < c->toks.n ? (int)c->toks.t[c->pos].line : 0;
    longjmp(c->err, 1);
}

static uint32_t new_id(C *c)
{
    return c->next_id++;
}

/* ---- instructions ---- */

static void op(Buf *b, int opcode, int nwords_after)
{
    buf_put(b, ((uint32_t)(nwords_after + 1) << 16) | (uint32_t)opcode);
}

static void op_string(Buf *b, const char *s)
{
    size_t len = strlen(s) + 1;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t w = 0;
        for (size_t k = 0; k < 4 && i + k < len; k++) {
            w |= (uint32_t)(unsigned char)s[i + k] << (8 * k);
        }
        buf_put(b, w);
    }
}

static int string_words(const char *s)
{
    return (int)((strlen(s) + 1 + 3) / 4);
}

static void emit_name(C *c, uint32_t id, const char *name)
{
    op(&c->names, SpvOpName, 1 + string_words(name));
    buf_put(&c->names, id);
    op_string(&c->names, name);
}

static void emit_member_name(C *c, uint32_t id, int member, const char *name)
{
    op(&c->names, SpvOpMemberName, 2 + string_words(name));
    buf_put(&c->names, id);
    buf_put(&c->names, member);
    op_string(&c->names, name);
}

static void decorate(C *c, uint32_t id, int decoration, int nargs, ...)
{
    va_list ap;
    op(&c->decor, SpvOpDecorate, 2 + nargs);
    buf_put(&c->decor, id);
    buf_put(&c->decor, decoration);
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) {
        buf_put(&c->decor, va_arg(ap, uint32_t));
    }
    va_end(ap);
}

static void member_decorate(C *c, uint32_t id, int member, int decoration,
                            int nargs, ...)
{
    va_list ap;
    op(&c->decor, SpvOpMemberDecorate, 3 + nargs);
    buf_put(&c->decor, id);
    buf_put(&c->decor, member);
    buf_put(&c->decor, decoration);
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) {
        buf_put(&c->decor, va_arg(ap, uint32_t));
    }
    va_end(ap);
}

/* A function-body instruction with a result: returns the result id */
static uint32_t ins(C *c, int opcode, uint32_t type_id, int nargs, ...)
{
    va_list ap;
    uint32_t id = new_id(c);
    op(&c->fbody, opcode, 2 + nargs);
    buf_put(&c->fbody, type_id);
    buf_put(&c->fbody, id);
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) {
        buf_put(&c->fbody, va_arg(ap, uint32_t));
    }
    va_end(ap);
    return id;
}

/* A function-body instruction with no result */
static void ins0(C *c, int opcode, int nargs, ...)
{
    va_list ap;
    op(&c->fbody, opcode, nargs);
    va_start(ap, nargs);
    for (int i = 0; i < nargs; i++) {
        buf_put(&c->fbody, va_arg(ap, uint32_t));
    }
    va_end(ap);
}

/* ---- types ---- */

static Type *type_new(C *c, enum TypeKind kind)
{
    Type *t = arena_alloc(&c->arena, sizeof(*t));
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    if (c->ntypes == c->types_cap) {
        c->types_cap = c->types_cap ? c->types_cap * 2 : 128;
        c->type_list = realloc(c->type_list, c->types_cap * sizeof(Type *));
        if (!c->type_list) {
            abort();
        }
    }
    c->type_list[c->ntypes++] = t;
    return t;
}

static Type *scalar_type(C *c, enum TypeKind k)
{
    switch (k) {
    case TY_BOOL: return c->t_bool;
    case TY_INT: return c->t_int;
    case TY_UINT: return c->t_uint;
    case TY_FLOAT: return c->t_float;
    default: fail(c, "internal: not a scalar kind");
    }
}

static Type *vec_type(C *c, enum TypeKind base, int n)
{
    if (n == 1) {
        return scalar_type(c, base);
    }
    for (size_t i = 0; i < c->ntypes; i++) {
        Type *t = c->type_list[i];
        if (t->kind == TY_VEC && t->base == base && t->n == n) {
            return t;
        }
    }
    Type *t = type_new(c, TY_VEC);
    t->base = base;
    t->n = n;
    t->id = new_id(c);
    op(&c->types, SpvOpTypeVector, 3);
    buf_put(&c->types, t->id);
    buf_put(&c->types, scalar_type(c, base)->id);
    buf_put(&c->types, n);
    return t;
}

static Type *mat_type(C *c, int cols, int rows)
{
    for (size_t i = 0; i < c->ntypes; i++) {
        Type *t = c->type_list[i];
        if (t->kind == TY_MAT && t->n == cols && t->rows == rows) {
            return t;
        }
    }
    Type *col = vec_type(c, TY_FLOAT, rows);
    Type *t = type_new(c, TY_MAT);
    t->n = cols;
    t->rows = rows;
    t->base = TY_FLOAT;
    t->elem = col;
    t->id = new_id(c);
    op(&c->types, SpvOpTypeMatrix, 3);
    buf_put(&c->types, t->id);
    buf_put(&c->types, col->id);
    buf_put(&c->types, cols);
    return t;
}

static uint32_t const_uint(C *c, uint32_t v);

/* Arrays with a layout stride are distinct types (the decoration is on
 * the type), so a stride of 0 means "no layout". */
static Type *array_type(C *c, Type *elem, int count, uint32_t stride)
{
    for (size_t i = 0; i < c->ntypes; i++) {
        Type *t = c->type_list[i];
        if (t->kind == TY_ARRAY && t->elem == elem && t->n == count &&
            t->array_stride == stride) {
            return t;
        }
    }
    Type *t = type_new(c, TY_ARRAY);
    t->elem = elem;
    t->n = count;
    t->array_stride = stride;
    t->id = new_id(c);
    if (count) {
        uint32_t len = const_uint(c, count);
        op(&c->types, SpvOpTypeArray, 3);
        buf_put(&c->types, t->id);
        buf_put(&c->types, elem->id);
        buf_put(&c->types, len);
    } else {
        op(&c->types, SpvOpTypeRuntimeArray, 2);
        buf_put(&c->types, t->id);
        buf_put(&c->types, elem->id);
    }
    if (stride) {
        decorate(c, t->id, SpvDecorationArrayStride, 1, stride);
    }
    return t;
}

static uint32_t ptr_type(C *c, Type *t, int sc)
{
    if (sc < 0 || sc >= 16) {
        fail(c, "internal: storage class %d", sc);
    }
    if (!t->ptr_id[sc]) {
        t->ptr_id[sc] = new_id(c);
        op(&c->types, SpvOpTypePointer, 3);
        buf_put(&c->types, t->ptr_id[sc]);
        buf_put(&c->types, sc);
        buf_put(&c->types, t->id);
    }
    return t->ptr_id[sc];
}

static Type *image_type(C *c, Type *sampled, int dim, bool arrayed, int sampledness,
                        int format, bool depth)
{
    for (size_t i = 0; i < c->ntypes; i++) {
        Type *t = c->type_list[i];
        if (t->kind == TY_IMAGE && t->elem == sampled && t->dim == dim &&
            t->arrayed == arrayed && t->sampled == sampledness &&
            t->format == format && t->depth == depth) {
            return t;
        }
    }
    Type *t = type_new(c, TY_IMAGE);
    t->elem = sampled;
    t->dim = dim;
    t->arrayed = arrayed;
    t->sampled = sampledness;
    t->format = format;
    t->depth = depth;
    t->id = new_id(c);
    op(&c->types, SpvOpTypeImage, 8);
    buf_put(&c->types, t->id);
    buf_put(&c->types, sampled->id);
    buf_put(&c->types, dim);
    buf_put(&c->types, depth ? 1 : 0);
    buf_put(&c->types, arrayed ? 1 : 0);
    buf_put(&c->types, 0);
    buf_put(&c->types, sampledness);
    buf_put(&c->types, format);
    return t;
}

static Type *sampled_image_type(C *c, Type *img)
{
    for (size_t i = 0; i < c->ntypes; i++) {
        Type *t = c->type_list[i];
        if (t->kind == TY_SAMPLED_IMAGE && t->elem == img) {
            return t;
        }
    }
    Type *t = type_new(c, TY_SAMPLED_IMAGE);
    t->elem = img;
    t->id = new_id(c);
    op(&c->types, SpvOpTypeSampledImage, 2);
    buf_put(&c->types, t->id);
    buf_put(&c->types, img->id);
    return t;
}

static bool is_scalar(const Type *t)
{
    return t->kind == TY_BOOL || t->kind == TY_INT || t->kind == TY_UINT ||
           t->kind == TY_FLOAT;
}

static bool is_scalar_or_vec(const Type *t)
{
    return is_scalar(t) || t->kind == TY_VEC;
}

static enum TypeKind base_kind(const Type *t)
{
    if (t->kind == TY_VEC || t->kind == TY_MAT) {
        return t->base;
    }
    return t->kind;
}

static int comp_count(const Type *t)
{
    if (t->kind == TY_VEC) {
        return t->n;
    }
    if (t->kind == TY_MAT) {
        return t->n * t->rows;
    }
    return 1;
}

static const char *type_name(const Type *t)
{
    static char buf[64];
    switch (t->kind) {
    case TY_VOID: return "void";
    case TY_BOOL: return "bool";
    case TY_INT: return "int";
    case TY_UINT: return "uint";
    case TY_FLOAT: return "float";
    case TY_VEC:
        snprintf(buf, sizeof(buf), "%svec%d",
                 t->base == TY_BOOL ? "b" : t->base == TY_INT ? "i" :
                 t->base == TY_UINT ? "u" : "", t->n);
        return buf;
    case TY_MAT:
        snprintf(buf, sizeof(buf), "mat%dx%d", t->n, t->rows);
        return buf;
    case TY_ARRAY: return "array";
    case TY_STRUCT: return "struct";
    case TY_SAMPLED_IMAGE: return "sampler";
    case TY_IMAGE: return "image";
    default: return "?";
    }
}

/* ---- constants ---- */

static uint32_t const_scalar(C *c, Type *t, uint32_t bits)
{
    for (size_t i = 0; i < c->nconsts; i++) {
        if (c->consts[i].type_id == t->id && c->consts[i].value == bits) {
            return c->consts[i].id;
        }
    }
    if (c->nconsts == c->consts_cap) {
        c->consts_cap = c->consts_cap ? c->consts_cap * 2 : 256;
        c->consts = realloc(c->consts, c->consts_cap * sizeof(Const));
        if (!c->consts) {
            abort();
        }
    }
    uint32_t id = new_id(c);
    if (t->kind == TY_BOOL) {
        op(&c->types, bits ? SpvOpConstantTrue : SpvOpConstantFalse, 2);
        buf_put(&c->types, t->id);
        buf_put(&c->types, id);
    } else {
        op(&c->types, SpvOpConstant, 3);
        buf_put(&c->types, t->id);
        buf_put(&c->types, id);
        buf_put(&c->types, bits);
    }
    c->consts[c->nconsts].type_id = t->id;
    c->consts[c->nconsts].value = bits;
    c->consts[c->nconsts].id = id;
    c->nconsts++;
    return id;
}

static uint32_t const_uint(C *c, uint32_t v)
{
    return const_scalar(c, c->t_uint, v);
}

static uint32_t const_int(C *c, int32_t v)
{
    return const_scalar(c, c->t_int, (uint32_t)v);
}

static uint32_t const_float(C *c, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return const_scalar(c, c->t_float, bits);
}

static uint32_t const_bool(C *c, bool b)
{
    return const_scalar(c, c->t_bool, b);
}

/* A composite constant (a vector of constant scalars, say): deduped by
 * its words */
static uint32_t const_composite(C *c, Type *t, const uint32_t *ids, int n)
{
    /* search the types section for an identical OpConstantComposite */
    size_t i = 0;
    while (i < c->types.n) {
        uint32_t w = c->types.w[i];
        uint32_t len = w >> 16;
        if ((w & 0xffff) == SpvOpConstantComposite && len == (uint32_t)(3 + n) &&
            c->types.w[i + 1] == t->id &&
            memcmp(&c->types.w[i + 3], ids, n * 4) == 0) {
            return c->types.w[i + 2];
        }
        i += len;
    }
    uint32_t id = new_id(c);
    op(&c->types, SpvOpConstantComposite, 2 + n);
    buf_put(&c->types, t->id);
    buf_put(&c->types, id);
    for (int k = 0; k < n; k++) {
        buf_put(&c->types, ids[k]);
    }
    return id;
}

/* ---- layout (std140 / std430) ---- */

static void layout_type(C *c, Type *t, bool std140);

static uint32_t round_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) / a * a;
}

/* Size and base alignment of t in a block of the given layout. Arrays
 * and nested structs get their decorated types made here. */
static void layout_type(C *c, Type *t, bool std140)
{
    switch (t->kind) {
    case TY_BOOL: case TY_INT: case TY_UINT: case TY_FLOAT:
        t->size = 4;
        t->align = 4;
        break;
    case TY_VEC:
        t->size = 4 * t->n;
        t->align = t->n == 3 ? 16 : 4 * t->n;
        break;
    case TY_MAT: {
        /* column-major: a column vector's stride, rows 3 padded to 4 */
        uint32_t stride = t->rows == 3 ? 16 : 4 * t->rows;
        if (std140) {
            stride = round_up(stride, 16);
        }
        t->align = stride;
        t->size = stride * t->n;
        break;
    }
    case TY_ARRAY:
        /* the stride is on the type: the caller gets a stride-bearing
         * array type through layout_array() */
        fail(c, "internal: layout of an array type");
    case TY_STRUCT:
        fail(c, "internal: nested structs are not supported");
    default:
        fail(c, "a %s cannot be in a block", type_name(t));
    }
}

/* The array type for a block member: its element laid out, the stride
 * from it */
static Type *layout_array(C *c, Type *elem, int count, bool std140,
                          uint32_t *size, uint32_t *align)
{
    if (elem->kind == TY_ARRAY) {
        fail(c, "arrays of arrays are not supported");
    }
    layout_type(c, elem, std140);
    uint32_t stride = round_up(elem->size, elem->align);
    uint32_t a = elem->align;
    if (std140) {
        stride = round_up(stride, 16);
        a = round_up(a, 16);
    }
    *size = stride * (count ? count : 1);
    *align = a;
    return array_type(c, elem, count, stride);
}

/* ---- tokens ---- */

static const Tok *peek(C *c)
{
    return &c->toks.t[c->pos];
}

static const Tok *peek2(C *c)
{
    size_t i = c->pos + 1;
    if (i >= c->toks.n) {
        i = c->toks.n - 1;
    }
    return &c->toks.t[i];
}

static const Tok *next(C *c)
{
    const Tok *t = &c->toks.t[c->pos];
    if (t->kind != T_EOF) {
        c->pos++;
    }
    return t;
}

static bool is_punct(C *c, int p)
{
    const Tok *t = peek(c);
    return t->kind == T_PUNCT && t->punct == p;
}

static bool accept(C *c, int p)
{
    if (is_punct(c, p)) {
        c->pos++;
        return true;
    }
    return false;
}

static const char *punct_name(int p)
{
    static const char *const names[] = {
        "?", "(", ")", "[", "]", "{", "}", ".", ",", ";", "?", ":", "+", "-",
        "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "<<", ">>", "&", "|",
        "^", "~", "!", "&&", "||", "^^", "++", "--", "=", "+=", "-=", "*=",
        "/=", "%=", "<<=", ">>=", "&=", "|=", "^=",
    };
    return p >= 0 && p < (int)(sizeof(names) / sizeof(names[0])) ? names[p] : "?";
}

static void expect(C *c, int p)
{
    if (!accept(c, p)) {
        const Tok *t = peek(c);
        if (t->kind == T_IDENT) {
            fail(c, "expected '%s', got '%s'", punct_name(p), t->text);
        }
        fail(c, "expected '%s'", punct_name(p));
    }
}

static bool is_ident(C *c, const char *s)
{
    const Tok *t = peek(c);
    return t->kind == T_IDENT && strcmp(t->text, s) == 0;
}

static bool accept_ident(C *c, const char *s)
{
    if (is_ident(c, s)) {
        c->pos++;
        return true;
    }
    return false;
}

static const char *expect_ident(C *c)
{
    const Tok *t = next(c);
    if (t->kind != T_IDENT) {
        fail(c, "expected a name");
    }
    return t->text;
}

/* ---- symbols ---- */

static Sym *sym_find(C *c, const char *name)
{
    for (size_t i = c->nsyms; i > 0; i--) {
        if (c->syms[i - 1].name == name) {
            return &c->syms[i - 1];
        }
    }
    return NULL;
}

static Sym *sym_add(C *c, const char *name, const LVal *lv)
{
    for (size_t i = c->nsyms; i > 0; i--) {
        if (c->syms[i - 1].scope != c->scope) {
            break;
        }
        if (c->syms[i - 1].name == name) {
            fail(c, "'%s' is already declared", name);
        }
    }
    if (c->nsyms == c->syms_cap) {
        c->syms_cap = c->syms_cap ? c->syms_cap * 2 : 256;
        c->syms = realloc(c->syms, c->syms_cap * sizeof(Sym));
        if (!c->syms) {
            abort();
        }
    }
    Sym *s = &c->syms[c->nsyms++];
    memset(s, 0, sizeof(*s));
    s->name = name;
    s->lv = *lv;
    s->scope = c->scope;
    s->member = -1;
    return s;
}

static void scope_push(C *c)
{
    c->scope++;
}

static void scope_pop(C *c)
{
    while (c->nsyms && c->syms[c->nsyms - 1].scope == c->scope) {
        c->nsyms--;
    }
    c->scope--;
}

static bool implicit_ok(enum TypeKind from, enum TypeKind to);

static bool func_named(C *c, const char *name)
{
    for (size_t i = 0; i < c->nfns; i++) {
        if (c->fns[i].name == name) {
            return true;
        }
    }
    return false;
}

/* The overload of name taking exactly these parameter types */
static Func *func_find_exact(C *c, const char *name, Type **params, int n)
{
    for (size_t i = 0; i < c->nfns; i++) {
        Func *f = &c->fns[i];
        if (f->name != name || f->nparams != n) {
            continue;
        }
        bool same = true;
        for (int k = 0; k < n; k++) {
            if (f->params[k] != params[k]) {
                same = false;
            }
        }
        if (same) {
            return f;
        }
    }
    return NULL;
}

/* The overload for a call: an exact match, else the one match through
 * implicit conversions */
static Func *func_resolve(C *c, const char *name, Type **args, int n)
{
    Func *exact = func_find_exact(c, name, args, n);
    if (exact) {
        return exact;
    }
    Func *found = NULL;
    for (size_t i = 0; i < c->nfns; i++) {
        Func *f = &c->fns[i];
        if (f->name != name || f->nparams != n) {
            continue;
        }
        bool ok = true;
        for (int k = 0; k < n && ok; k++) {
            Type *a = args[k], *p = f->params[k];
            if (a == p) {
                continue;
            }
            if (is_scalar_or_vec(a) && is_scalar_or_vec(p) &&
                comp_count(a) == comp_count(p) &&
                implicit_ok(base_kind(a), base_kind(p))) {
                continue;
            }
            ok = false;
        }
        if (ok) {
            if (found) {
                fail(c, "the call of %s() is ambiguous", name);
            }
            found = f;
        }
    }
    return found;
}

/* ---- blocks ---- */

static void start_block(C *c, uint32_t label)
{
    ins0(c, SpvOpLabel, 1, label);
    c->cur_block = label;
    c->terminated = false;
}

/* After a terminator, code that follows in the source still needs a
 * block: an unreachable one, branched to nothing */
static void ensure_block(C *c)
{
    if (c->terminated) {
        start_block(c, new_id(c));
    }
}

static void branch(C *c, uint32_t target)
{
    if (c->terminated) {
        return;
    }
    ins0(c, SpvOpBranch, 1, target);
    c->terminated = true;
}

static void branch_cond(C *c, uint32_t cond, uint32_t t, uint32_t f)
{
    ins0(c, SpvOpBranchConditional, 3, cond, t, f);
    c->terminated = true;
}

/* ---- variables ---- */

/* Declares a variable in the function's first block (Function storage)
 * or the module (anything else) */
static uint32_t make_variable(C *c, Type *t, int sc, uint32_t init)
{
    uint32_t id = new_id(c);
    uint32_t pt = ptr_type(c, t, sc);
    Buf *b = sc == SpvStorageClassFunction ? &c->fvars : &c->types;
    op(b, SpvOpVariable, init ? 4 : 3);
    buf_put(b, pt);
    buf_put(b, id);
    buf_put(b, sc);
    if (init) {
        buf_put(b, init);
    }
    if (sc != SpvStorageClassFunction && sc != SpvStorageClassPrivate &&
        sc != SpvStorageClassWorkgroup) {
        buf_put(&c->entry_iface, id);
    } else if (sc != SpvStorageClassFunction) {
        /* SPIR-V 1.4+: every global the entry point may touch */
        buf_put(&c->entry_iface, id);
    }
    return id;
}

static Val convert(C *c, Val v, enum TypeKind to);

static Val load(C *c, const LVal *lv)
{
    Val v;
    if (lv->as_bool) {
        v.t = lv->store_t;
        v.id = ins(c, SpvOpLoad, lv->store_t->id, 1, lv->ptr);
        return convert(c, v, TY_BOOL);
    }
    if (lv->nswz) {
        Val whole;
        whole.t = lv->vec_t;
        whole.id = ins(c, SpvOpLoad, lv->vec_t->id, 1, lv->ptr);
        if (lv->nswz == 1) {
            v.t = lv->t;
            v.id = ins(c, SpvOpCompositeExtract, lv->t->id, 2, whole.id,
                       (uint32_t)lv->swz[0]);
            return v;
        }
        op(&c->fbody, SpvOpVectorShuffle, 4 + lv->nswz);
        buf_put(&c->fbody, lv->t->id);
        v.id = new_id(c);
        buf_put(&c->fbody, v.id);
        buf_put(&c->fbody, whole.id);
        buf_put(&c->fbody, whole.id);
        for (int i = 0; i < lv->nswz; i++) {
            buf_put(&c->fbody, lv->swz[i]);
        }
        v.t = lv->t;
        return v;
    }
    v.t = lv->t;
    v.id = ins(c, SpvOpLoad, lv->t->id, 1, lv->ptr);
    return v;
}

static void store(C *c, const LVal *lv, Val v)
{
    if (lv->readonly) {
        fail(c, "assignment to a read-only variable");
    }
    if (lv->as_bool) {
        v = convert(c, v, TY_UINT);
        ins0(c, SpvOpStore, 2, lv->ptr, v.id);
        return;
    }
    if (!lv->nswz) {
        ins0(c, SpvOpStore, 2, lv->ptr, v.id);
        return;
    }
    uint32_t whole = ins(c, SpvOpLoad, lv->vec_t->id, 1, lv->ptr);
    if (lv->nswz == 1) {
        uint32_t r = ins(c, SpvOpCompositeInsert, lv->vec_t->id, 3, v.id, whole,
                         (uint32_t)lv->swz[0]);
        ins0(c, SpvOpStore, 2, lv->ptr, r);
        return;
    }
    /* shuffle: components not written come from the old vector (0..n-1),
     * written ones from the new (n..) */
    int n = lv->vec_t->n;
    op(&c->fbody, SpvOpVectorShuffle, 4 + n);
    buf_put(&c->fbody, lv->vec_t->id);
    uint32_t r = new_id(c);
    buf_put(&c->fbody, r);
    buf_put(&c->fbody, whole);
    buf_put(&c->fbody, v.id);
    for (int i = 0; i < n; i++) {
        uint32_t src = i;
        for (int k = 0; k < lv->nswz; k++) {
            if (lv->swz[k] == i) {
                src = n + k;
            }
        }
        buf_put(&c->fbody, src);
    }
    ins0(c, SpvOpStore, 2, lv->ptr, r);
}

static Val rval(C *c, Ex *e)
{
    if (e->is_lv) {
        return load(c, &e->lv);
    }
    return e->v;
}

static Ex ex_val(Val v)
{
    Ex e;
    memset(&e, 0, sizeof(e));
    e.v = v;
    return e;
}

static Ex ex_const(C *c, Type *t, uint32_t bits)
{
    Ex e;
    memset(&e, 0, sizeof(e));
    e.v.t = t;
    e.v.id = const_scalar(c, t, bits);
    e.is_const = true;
    e.cv = bits;
    return e;
}

static Ex ex_lv(const LVal *lv)
{
    Ex e;
    memset(&e, 0, sizeof(e));
    e.is_lv = true;
    e.lv = *lv;
    return e;
}

/* ---- conversions ---- */

static uint32_t splat(C *c, Type *vt, uint32_t scalar)
{
    uint32_t ids[4] = { scalar, scalar, scalar, scalar };
    op(&c->fbody, SpvOpCompositeConstruct, 2 + vt->n);
    buf_put(&c->fbody, vt->id);
    uint32_t id = new_id(c);
    buf_put(&c->fbody, id);
    for (int i = 0; i < vt->n; i++) {
        buf_put(&c->fbody, ids[i]);
    }
    return id;
}

/* Converts the scalar/vector v to the base kind to, keeping its shape */
static Val convert(C *c, Val v, enum TypeKind to)
{
    enum TypeKind from = base_kind(v.t);
    if (from == to) {
        return v;
    }
    if (!is_scalar_or_vec(v.t)) {
        fail(c, "cannot convert a %s", type_name(v.t));
    }
    Type *tt = vec_type(c, to, comp_count(v.t));
    Val r;
    r.t = tt;
    if (to == TY_BOOL) {
        /* != 0 */
        uint32_t zero;
        Type *st = scalar_type(c, from);
        if (from == TY_FLOAT) {
            zero = const_float(c, 0.0f);
        } else {
            zero = const_scalar(c, st, 0);
        }
        if (v.t->kind == TY_VEC) {
            zero = splat(c, v.t, zero);
        }
        r.id = ins(c, from == TY_FLOAT ? SpvOpFUnordNotEqual : SpvOpINotEqual,
                   tt->id, 2, v.id, zero);
        return r;
    }
    if (from == TY_BOOL) {
        Type *st = scalar_type(c, to);
        uint32_t one = to == TY_FLOAT ? const_float(c, 1.0f) : const_scalar(c, st, 1);
        uint32_t zero = to == TY_FLOAT ? const_float(c, 0.0f) : const_scalar(c, st, 0);
        if (v.t->kind == TY_VEC) {
            one = splat(c, tt, one);
            zero = splat(c, tt, zero);
        }
        r.id = ins(c, SpvOpSelect, tt->id, 3, v.id, one, zero);
        return r;
    }
    int opcode;
    if (from == TY_INT && to == TY_FLOAT) {
        opcode = SpvOpConvertSToF;
    } else if (from == TY_UINT && to == TY_FLOAT) {
        opcode = SpvOpConvertUToF;
    } else if (from == TY_FLOAT && to == TY_INT) {
        opcode = SpvOpConvertFToS;
    } else if (from == TY_FLOAT && to == TY_UINT) {
        opcode = SpvOpConvertFToU;
    } else {
        opcode = SpvOpBitcast; /* int <-> uint */
    }
    r.id = ins(c, opcode, tt->id, 1, v.id);
    return r;
}

static int kind_rank(enum TypeKind k)
{
    switch (k) {
    case TY_BOOL: return 0;
    case TY_INT: return 1;
    case TY_UINT: return 2;
    case TY_FLOAT: return 3;
    default: return -1;
    }
}

/* GLSL's implicit conversions: int to uint, int and uint to float */
static bool implicit_ok(enum TypeKind from, enum TypeKind to)
{
    if (from == to) {
        return true;
    }
    if (from == TY_INT && (to == TY_UINT || to == TY_FLOAT)) {
        return true;
    }
    if (from == TY_UINT && to == TY_FLOAT) {
        return true;
    }
    return false;
}

/* v converted to type t if an implicit conversion allows it */
static Val coerce(C *c, Val v, Type *t)
{
    if (v.t == t) {
        return v;
    }
    if (is_scalar_or_vec(v.t) && is_scalar_or_vec(t) &&
        comp_count(v.t) == comp_count(t) &&
        implicit_ok(base_kind(v.t), base_kind(t))) {
        return convert(c, v, base_kind(t));
    }
    fail(c, "cannot convert %s to %s", type_name(v.t), type_name(t));
}

/* A scalar v broadcast to the vector type vt */
static Val broadcast(C *c, Val v, Type *vt)
{
    Val r;
    r.t = vt;
    r.id = splat(c, vt, v.id);
    return r;
}

/* ---- type names ---- */

static Type *parse_type_keyword(C *c, const char *s)
{
    if (!strcmp(s, "void")) return c->t_void;
    if (!strcmp(s, "bool")) return c->t_bool;
    if (!strcmp(s, "int")) return c->t_int;
    if (!strcmp(s, "uint")) return c->t_uint;
    if (!strcmp(s, "float")) return c->t_float;
    if (!strncmp(s, "vec", 3) && s[3] >= '2' && s[3] <= '4' && !s[4]) {
        return vec_type(c, TY_FLOAT, s[3] - '0');
    }
    if (!strncmp(s, "ivec", 4) && s[4] >= '2' && s[4] <= '4' && !s[5]) {
        return vec_type(c, TY_INT, s[4] - '0');
    }
    if (!strncmp(s, "uvec", 4) && s[4] >= '2' && s[4] <= '4' && !s[5]) {
        return vec_type(c, TY_UINT, s[4] - '0');
    }
    if (!strncmp(s, "bvec", 4) && s[4] >= '2' && s[4] <= '4' && !s[5]) {
        return vec_type(c, TY_BOOL, s[4] - '0');
    }
    if (!strncmp(s, "mat", 3) && s[3] >= '2' && s[3] <= '4') {
        if (!s[4]) {
            return mat_type(c, s[3] - '0', s[3] - '0');
        }
        if (s[4] == 'x' && s[5] >= '2' && s[5] <= '4' && !s[6]) {
            return mat_type(c, s[3] - '0', s[5] - '0');
        }
    }
    return NULL;
}

/* sampler2D, sampler3D, samplerCube, sampler2DShadow, image2D... */
static Type *parse_opaque_type(C *c, const char *s, int format)
{
    const char *p;
    Type *sampled = c->t_float;
    bool is_sampler;
    if (!strncmp(s, "sampler", 7)) {
        p = s + 7;
        is_sampler = true;
    } else if (!strncmp(s, "isampler", 8)) {
        p = s + 8;
        sampled = c->t_int;
        is_sampler = true;
    } else if (!strncmp(s, "usampler", 8)) {
        p = s + 8;
        sampled = c->t_uint;
        is_sampler = true;
    } else if (!strncmp(s, "image", 5)) {
        p = s + 5;
        is_sampler = false;
    } else if (!strncmp(s, "iimage", 6)) {
        p = s + 6;
        sampled = c->t_int;
        is_sampler = false;
    } else if (!strncmp(s, "uimage", 6)) {
        p = s + 6;
        sampled = c->t_uint;
        is_sampler = false;
    } else {
        return NULL;
    }
    int dim;
    bool arrayed = false, depth = false;
    if (!strncmp(p, "1D", 2)) {
        dim = SpvDim1D;
        p += 2;
    } else if (!strncmp(p, "2D", 2)) {
        dim = SpvDim2D;
        p += 2;
    } else if (!strncmp(p, "3D", 2)) {
        dim = SpvDim3D;
        p += 2;
    } else if (!strncmp(p, "Cube", 4)) {
        dim = SpvDimCube;
        p += 4;
    } else if (!strncmp(p, "Buffer", 6)) {
        dim = SpvDimBuffer;
        p += 6;
    } else {
        return NULL;
    }
    if (!strncmp(p, "Array", 5)) {
        arrayed = true;
        p += 5;
    }
    if (!strcmp(p, "Shadow")) {
        depth = true;
        p += 6;
    }
    if (*p) {
        return NULL;
    }
    if (dim == SpvDim1D) {
        fail(c, "1D images are not supported");
    }
    Type *img = image_type(c, sampled, dim, arrayed, is_sampler ? 1 : 2,
                           is_sampler ? SpvImageFormatUnknown : format, depth);
    return is_sampler ? sampled_image_type(c, img) : img;
}

static bool is_type_name(C *c, const char *s)
{
    return parse_type_keyword(c, s) != NULL ||
           !strncmp(s, "sampler", 7) || !strncmp(s, "isampler", 8) ||
           !strncmp(s, "usampler", 8) || !strncmp(s, "image", 5) ||
           !strncmp(s, "iimage", 6) || !strncmp(s, "uimage", 6);
}

/* ---- expressions ---- */

static Ex parse_expr(C *c);
static Ex parse_assignment(C *c);
static Ex parse_ternary(C *c);
static Val parse_rvalue(C *c);

/* Floating arithmetic in a precise context carries NoContraction */
static uint32_t farith(C *c, int opcode, Type *t, uint32_t a, uint32_t b)
{
    uint32_t id = ins(c, opcode, t->id, 2, a, b);
    if (c->precise_ctx) {
        decorate(c, id, SpvDecorationNoContraction, 0);
    }
    return id;
}

static uint32_t const_bits_convert(enum TypeKind from, enum TypeKind to,
                                   uint32_t bits);

static Ex fold_binop(C *c, int p, const Ex *a, const Ex *b, enum TypeKind k)
{
    uint32_t r = 0;
    Type *rt = scalar_type(c, k);
    if (k == TY_FLOAT) {
        float x, y, z;
        memcpy(&x, &a->cv, 4);
        memcpy(&y, &b->cv, 4);
        switch (p) {
        case P_PLUS: z = x + y; break;
        case P_MINUS: z = x - y; break;
        case P_STAR: z = x * y; break;
        case P_SLASH: z = x / y; break;
        case P_LT: return ex_const(c, c->t_bool, x < y);
        case P_GT: return ex_const(c, c->t_bool, x > y);
        case P_LE: return ex_const(c, c->t_bool, x <= y);
        case P_GE: return ex_const(c, c->t_bool, x >= y);
        case P_EQ: return ex_const(c, c->t_bool, x == y);
        case P_NE: return ex_const(c, c->t_bool, x != y);
        default: fail(c, "bad constant float expression");
        }
        memcpy(&r, &z, 4);
        return ex_const(c, rt, r);
    }
    if (k == TY_INT) {
        int32_t x = (int32_t)a->cv, y = (int32_t)b->cv, z;
        switch (p) {
        case P_PLUS: z = (int32_t)((uint32_t)x + (uint32_t)y); break;
        case P_MINUS: z = (int32_t)((uint32_t)x - (uint32_t)y); break;
        case P_STAR: z = (int32_t)((uint32_t)x * (uint32_t)y); break;
        case P_SLASH:
            if (!y) fail(c, "constant division by zero");
            z = x / y;
            break;
        case P_PERCENT:
            if (!y) fail(c, "constant division by zero");
            z = x % y;
            break;
        case P_SHL: z = (int32_t)((uint32_t)x << (y & 31)); break;
        case P_SHR: z = x >> (y & 31); break;
        case P_AND: z = x & y; break;
        case P_OR: z = x | y; break;
        case P_XOR: z = x ^ y; break;
        case P_LT: return ex_const(c, c->t_bool, x < y);
        case P_GT: return ex_const(c, c->t_bool, x > y);
        case P_LE: return ex_const(c, c->t_bool, x <= y);
        case P_GE: return ex_const(c, c->t_bool, x >= y);
        case P_EQ: return ex_const(c, c->t_bool, x == y);
        case P_NE: return ex_const(c, c->t_bool, x != y);
        default: fail(c, "bad constant expression");
        }
        return ex_const(c, rt, (uint32_t)z);
    }
    if (k == TY_UINT) {
        uint32_t x = a->cv, y = b->cv, z;
        switch (p) {
        case P_PLUS: z = x + y; break;
        case P_MINUS: z = x - y; break;
        case P_STAR: z = x * y; break;
        case P_SLASH:
            if (!y) fail(c, "constant division by zero");
            z = x / y;
            break;
        case P_PERCENT:
            if (!y) fail(c, "constant division by zero");
            z = x % y;
            break;
        case P_SHL: z = x << (y & 31); break;
        case P_SHR: z = x >> (y & 31); break;
        case P_AND: z = x & y; break;
        case P_OR: z = x | y; break;
        case P_XOR: z = x ^ y; break;
        case P_LT: return ex_const(c, c->t_bool, x < y);
        case P_GT: return ex_const(c, c->t_bool, x > y);
        case P_LE: return ex_const(c, c->t_bool, x <= y);
        case P_GE: return ex_const(c, c->t_bool, x >= y);
        case P_EQ: return ex_const(c, c->t_bool, x == y);
        case P_NE: return ex_const(c, c->t_bool, x != y);
        default: fail(c, "bad constant expression");
        }
        return ex_const(c, rt, z);
    }
    if (k == TY_BOOL) {
        bool x = a->cv != 0, y = b->cv != 0;
        switch (p) {
        case P_EQ: return ex_const(c, c->t_bool, x == y);
        case P_NE: return ex_const(c, c->t_bool, x != y);
        case P_ANDAND: return ex_const(c, c->t_bool, x && y);
        case P_OROR: return ex_const(c, c->t_bool, x || y);
        case P_XORXOR: return ex_const(c, c->t_bool, x != y);
        default: fail(c, "bad constant bool expression");
        }
    }
    fail(c, "bad constant expression");
}

/* Matrix products, the one place where shapes differ */
static Val mat_mul(C *c, Val a, Val b)
{
    Val r;
    if (a.t->kind == TY_MAT && b.t->kind == TY_MAT) {
        if (a.t->n != b.t->rows) {
            fail(c, "matrix sizes do not match for '*'");
        }
        r.t = mat_type(c, b.t->n, a.t->rows);
        r.id = ins(c, SpvOpMatrixTimesMatrix, r.t->id, 2, a.id, b.id);
        return r;
    }
    if (a.t->kind == TY_MAT) {
        if (is_scalar(b.t)) {
            b = convert(c, b, TY_FLOAT);
            r.t = a.t;
            r.id = ins(c, SpvOpMatrixTimesScalar, r.t->id, 2, a.id, b.id);
            return r;
        }
        if (b.t->kind != TY_VEC || b.t->n != a.t->n) {
            fail(c, "matrix times a vector of the wrong size");
        }
        b = convert(c, b, TY_FLOAT);
        r.t = vec_type(c, TY_FLOAT, a.t->rows);
        r.id = ins(c, SpvOpMatrixTimesVector, r.t->id, 2, a.id, b.id);
        return r;
    }
    /* b is the matrix */
    if (is_scalar(a.t)) {
        a = convert(c, a, TY_FLOAT);
        r.t = b.t;
        r.id = ins(c, SpvOpMatrixTimesScalar, r.t->id, 2, b.id, a.id);
        return r;
    }
    if (a.t->kind != TY_VEC || a.t->n != b.t->rows) {
        fail(c, "vector times a matrix of the wrong size");
    }
    a = convert(c, a, TY_FLOAT);
    r.t = vec_type(c, TY_FLOAT, b.t->n);
    r.id = ins(c, SpvOpVectorTimesMatrix, r.t->id, 2, a.id, b.id);
    return r;
}

/* Brings two scalar/vector operands to a common type: the higher base
 * kind, a scalar broadcast to the other's vector size */
static void harmonize(C *c, Val *a, Val *b)
{
    if (!is_scalar_or_vec(a->t) || !is_scalar_or_vec(b->t)) {
        fail(c, "operands %s and %s do not fit the operator", type_name(a->t),
             type_name(b->t));
    }
    enum TypeKind ka = base_kind(a->t), kb = base_kind(b->t);
    enum TypeKind k = kind_rank(ka) >= kind_rank(kb) ? ka : kb;
    if (!implicit_ok(ka, k) || !implicit_ok(kb, k)) {
        fail(c, "operands %s and %s do not match", type_name(a->t),
             type_name(b->t));
    }
    *a = convert(c, *a, k);
    *b = convert(c, *b, k);
    if (a->t->kind == TY_VEC && b->t->kind != TY_VEC) {
        *b = broadcast(c, *b, a->t);
    } else if (b->t->kind == TY_VEC && a->t->kind != TY_VEC) {
        *a = broadcast(c, *a, b->t);
    } else if (a->t != b->t) {
        fail(c, "vector sizes differ");
    }
}

static Ex binop(C *c, int p, Ex ea, Ex eb)
{
    if (ea.is_const && eb.is_const && base_kind(ea.v.t) == base_kind(eb.v.t) &&
        p != P_ANDAND && p != P_OROR) {
        return fold_binop(c, p, &ea, &eb, base_kind(ea.v.t));
    }
    if (ea.is_const && eb.is_const && is_scalar(ea.v.t) && is_scalar(eb.v.t) &&
        p != P_SHL && p != P_SHR && p != P_ANDAND && p != P_OROR) {
        /* mixed int/uint/float constants: promote and fold */
        enum TypeKind ka = ea.v.t->kind, kb = eb.v.t->kind;
        enum TypeKind k = kind_rank(ka) >= kind_rank(kb) ? ka : kb;
        if (implicit_ok(ka, k) && implicit_ok(kb, k)) {
            Ex a2 = ea, b2 = eb;
            if (ka != k) {
                a2 = ex_const(c, scalar_type(c, k), const_bits_convert(ka, k, ea.cv));
            }
            if (kb != k) {
                b2 = ex_const(c, scalar_type(c, k), const_bits_convert(kb, k, eb.cv));
            }
            return fold_binop(c, p, &a2, &b2, k);
        }
    }
    Val a = rval(c, &ea), b = rval(c, &eb);
    Val r;
    if (p == P_STAR && (a.t->kind == TY_MAT || b.t->kind == TY_MAT)) {
        return ex_val(mat_mul(c, a, b));
    }
    if (a.t->kind == TY_MAT || b.t->kind == TY_MAT) {
        fail(c, "only '*' is supported on matrices");
    }
    switch (p) {
    case P_PLUS: case P_MINUS: case P_STAR: case P_SLASH: case P_PERCENT: {
        harmonize(c, &a, &b);
        enum TypeKind k = base_kind(a.t);
        int opcode;
        if (k == TY_FLOAT) {
            switch (p) {
            case P_PLUS: opcode = SpvOpFAdd; break;
            case P_MINUS: opcode = SpvOpFSub; break;
            case P_STAR: opcode = SpvOpFMul; break;
            case P_SLASH: opcode = SpvOpFDiv; break;
            default: fail(c, "'%%' on floats; use mod()");
            }
            r.t = a.t;
            r.id = farith(c, opcode, a.t, a.id, b.id);
            return ex_val(r);
        }
        if (k == TY_BOOL) {
            fail(c, "arithmetic on booleans");
        }
        switch (p) {
        case P_PLUS: opcode = SpvOpIAdd; break;
        case P_MINUS: opcode = SpvOpISub; break;
        case P_STAR: opcode = SpvOpIMul; break;
        case P_SLASH: opcode = k == TY_INT ? SpvOpSDiv : SpvOpUDiv; break;
        default: opcode = k == TY_INT ? SpvOpSRem : SpvOpUMod; break;
        }
        r.t = a.t;
        r.id = ins(c, opcode, a.t->id, 2, a.id, b.id);
        return ex_val(r);
    }
    case P_SHL: case P_SHR: {
        enum TypeKind ka = base_kind(a.t);
        if (ka != TY_INT && ka != TY_UINT) {
            fail(c, "shift of a non-integer");
        }
        if (base_kind(b.t) != TY_INT && base_kind(b.t) != TY_UINT) {
            fail(c, "shift by a non-integer");
        }
        if (a.t->kind == TY_VEC && b.t->kind != TY_VEC) {
            b = broadcast(c, b, vec_type(c, base_kind(b.t), a.t->n));
        }
        if (a.t->kind != TY_VEC && b.t->kind == TY_VEC) {
            fail(c, "scalar shifted by a vector");
        }
        int opcode = p == P_SHL ? SpvOpShiftLeftLogical :
                     ka == TY_INT ? SpvOpShiftRightArithmetic :
                                    SpvOpShiftRightLogical;
        r.t = a.t;
        r.id = ins(c, opcode, a.t->id, 2, a.id, b.id);
        return ex_val(r);
    }
    case P_AND: case P_OR: case P_XOR: {
        harmonize(c, &a, &b);
        enum TypeKind k = base_kind(a.t);
        if (k != TY_INT && k != TY_UINT) {
            fail(c, "bitwise operator on a non-integer");
        }
        int opcode = p == P_AND ? SpvOpBitwiseAnd : p == P_OR ? SpvOpBitwiseOr :
                                                                SpvOpBitwiseXor;
        r.t = a.t;
        r.id = ins(c, opcode, a.t->id, 2, a.id, b.id);
        return ex_val(r);
    }
    case P_LT: case P_GT: case P_LE: case P_GE: {
        harmonize(c, &a, &b);
        if (a.t->kind == TY_VEC) {
            fail(c, "relational operator on vectors; use lessThan() and co");
        }
        enum TypeKind k = base_kind(a.t);
        int opcode;
        if (k == TY_FLOAT) {
            opcode = p == P_LT ? SpvOpFOrdLessThan : p == P_GT ? SpvOpFOrdGreaterThan :
                     p == P_LE ? SpvOpFOrdLessThanEqual : SpvOpFOrdGreaterThanEqual;
        } else if (k == TY_INT) {
            opcode = p == P_LT ? SpvOpSLessThan : p == P_GT ? SpvOpSGreaterThan :
                     p == P_LE ? SpvOpSLessThanEqual : SpvOpSGreaterThanEqual;
        } else if (k == TY_UINT) {
            opcode = p == P_LT ? SpvOpULessThan : p == P_GT ? SpvOpUGreaterThan :
                     p == P_LE ? SpvOpULessThanEqual : SpvOpUGreaterThanEqual;
        } else {
            fail(c, "relational operator on booleans");
        }
        r.t = c->t_bool;
        r.id = ins(c, opcode, c->t_bool->id, 2, a.id, b.id);
        return ex_val(r);
    }
    case P_EQ: case P_NE: {
        harmonize(c, &a, &b);
        enum TypeKind k = base_kind(a.t);
        int opcode;
        if (k == TY_FLOAT) {
            opcode = p == P_EQ ? SpvOpFOrdEqual : SpvOpFUnordNotEqual;
        } else if (k == TY_BOOL) {
            opcode = p == P_EQ ? SpvOpLogicalEqual : SpvOpLogicalNotEqual;
        } else {
            opcode = p == P_EQ ? SpvOpIEqual : SpvOpINotEqual;
        }
        if (a.t->kind == TY_VEC) {
            Type *bt = vec_type(c, TY_BOOL, a.t->n);
            uint32_t v = ins(c, opcode, bt->id, 2, a.id, b.id);
            r.t = c->t_bool;
            r.id = ins(c, p == P_EQ ? SpvOpAll : SpvOpAny, c->t_bool->id, 1, v);
            return ex_val(r);
        }
        r.t = c->t_bool;
        r.id = ins(c, opcode, c->t_bool->id, 2, a.id, b.id);
        return ex_val(r);
    }
    case P_XORXOR:
        if (a.t != c->t_bool || b.t != c->t_bool) {
            fail(c, "'^^' needs booleans");
        }
        r.t = c->t_bool;
        r.id = ins(c, SpvOpLogicalNotEqual, c->t_bool->id, 2, a.id, b.id);
        return ex_val(r);
    default:
        fail(c, "internal: operator %d", p);
    }
}

static int binop_prec(int p)
{
    switch (p) {
    case P_OROR: return 1;
    case P_XORXOR: return 2;
    case P_ANDAND: return 3;
    case P_OR: return 4;
    case P_XOR: return 5;
    case P_AND: return 6;
    case P_EQ: case P_NE: return 7;
    case P_LT: case P_GT: case P_LE: case P_GE: return 8;
    case P_SHL: case P_SHR: return 9;
    case P_PLUS: case P_MINUS: return 10;
    case P_STAR: case P_SLASH: case P_PERCENT: return 11;
    default: return 0;
    }
}

static Ex parse_unary(C *c);

/* Emits instructions at an earlier point of the function body: the
 * words made between splice_begin and splice_end go in at pos rather
 * than the end (ids are not positional, so this is safe) */
static void splice_begin(C *c, Buf *saved, size_t pos, size_t *at)
{
    *saved = c->fbody;
    memset(&c->fbody, 0, sizeof(c->fbody));
    *at = pos;
}

static void splice_end(C *c, Buf *saved, size_t at)
{
    Buf made = c->fbody;
    c->fbody = *saved;
    if (at > c->fbody.n || made.n > SIZE_MAX / sizeof(uint32_t) - c->fbody.n) {
        abort();
    }
    if (!c->fbody.n) {
        buf_append_take(&c->fbody, &made);
    } else if (made.n) {
        /* Insert into the owned body, not a second prefix/body/suffix buffer. */
        buf_reserve(&c->fbody, c->fbody.n + made.n);
        memmove(c->fbody.w + at + made.n, c->fbody.w + at,
                (c->fbody.n - at) * sizeof(uint32_t));
        memcpy(c->fbody.w + at, made.w, made.n * sizeof(uint32_t));
        c->fbody.n += made.n;
    }
    free(made.w);
}

static Ex parse_binary(C *c, int min_prec);

/* a && b, a || b: the right side runs only when it matters */
static Ex short_circuit(C *c, int p, Ex ea)
{
    Val a = rval(c, &ea);
    if (a.t != c->t_bool) {
        fail(c, "'%s' needs booleans", punct_name(p));
    }
    uint32_t from = c->cur_block;
    uint32_t rhs = new_id(c), merge = new_id(c);
    ins0(c, SpvOpSelectionMerge, 2, merge, SpvSelectionControlMaskNone);
    if (p == P_ANDAND) {
        branch_cond(c, a.id, rhs, merge);
    } else {
        branch_cond(c, a.id, merge, rhs);
    }
    start_block(c, rhs);
    Ex eb = parse_binary(c, binop_prec(p) + 1);
    Val b = rval(c, &eb);
    if (b.t != c->t_bool) {
        fail(c, "'%s' needs booleans", punct_name(p));
    }
    uint32_t rhs_end = c->cur_block;
    branch(c, merge);
    start_block(c, merge);
    Val r;
    r.t = c->t_bool;
    r.id = ins(c, SpvOpPhi, c->t_bool->id, 4, const_bool(c, p == P_OROR), from,
               b.id, rhs_end);
    return ex_val(r);
}

/* Precedence climbing over the binary operators */
static Ex parse_binary(C *c, int min_prec)
{
    Ex lhs = parse_unary(c);
    for (;;) {
        const Tok *t = peek(c);
        if (t->kind != T_PUNCT) {
            return lhs;
        }
        int p = t->punct;
        int prec = binop_prec(p);
        if (!prec || prec < min_prec) {
            return lhs;
        }
        next(c);
        if (p == P_ANDAND || p == P_OROR) {
            if (lhs.is_const) {
                /* fold where the left is constant and the right has no
                 * effects to skip: parse the right at this precedence */
                Ex rhs = parse_binary(c, prec + 1);
                if (rhs.is_const) {
                    lhs = fold_binop(c, p, &lhs, &rhs, TY_BOOL);
                    continue;
                }
                Val a = rval(c, &lhs), b = rval(c, &rhs);
                Val r;
                r.t = c->t_bool;
                r.id = ins(c, p == P_ANDAND ? SpvOpLogicalAnd : SpvOpLogicalOr,
                           c->t_bool->id, 2, a.id, b.id);
                lhs = ex_val(r);
                continue;
            }
            lhs = short_circuit(c, p, lhs);
            continue;
        }
        Ex rhs = parse_binary(c, prec + 1);
        lhs = binop(c, p, lhs, rhs);
    }
}

/* cond ? a : b, one side evaluated */
static Ex parse_ternary(C *c)
{
    Ex cond = parse_binary(c, 1);
    if (!accept(c, P_QUESTION)) {
        return cond;
    }
    Val cv = rval(c, &cond);
    if (cv.t != c->t_bool) {
        fail(c, "'?:' needs a boolean condition");
    }
    uint32_t bt = new_id(c), bf = new_id(c), merge = new_id(c);
    ins0(c, SpvOpSelectionMerge, 2, merge, SpvSelectionControlMaskNone);
    branch_cond(c, cv.id, bt, bf);
    start_block(c, bt);
    Ex ea = parse_assignment(c);
    Val a = rval(c, &ea);
    uint32_t a_end = c->cur_block;
    size_t a_tail = c->fbody.n; /* where a's block ends, before its branch */
    branch(c, merge);
    expect(c, P_COLON);
    start_block(c, bf);
    Ex eb = parse_assignment(c);
    Val b = rval(c, &eb);
    if (a.t != b.t) {
        /* the sides' common type, as GLSL converts them */
        if (!is_scalar_or_vec(a.t) || !is_scalar_or_vec(b.t) ||
            comp_count(a.t) != comp_count(b.t)) {
            fail(c, "'?:' sides are %s and %s", type_name(a.t), type_name(b.t));
        }
        enum TypeKind ka = base_kind(a.t), kb = base_kind(b.t);
        enum TypeKind k = kind_rank(ka) >= kind_rank(kb) ? ka : kb;
        if (!implicit_ok(ka, k) || !implicit_ok(kb, k)) {
            fail(c, "'?:' sides do not match");
        }
        b = convert(c, b, k);
        if (ka != k) {
            /* a's conversion belongs in a's block, already emitted */
            Buf saved;
            size_t at;
            splice_begin(c, &saved, a_tail, &at);
            a = convert(c, a, k);
            splice_end(c, &saved, at);
        }
    }
    uint32_t b_end = c->cur_block;
    branch(c, merge);
    start_block(c, merge);
    Val r;
    r.t = a.t;
    r.id = ins(c, SpvOpPhi, a.t->id, 4, a.id, a_end, b.id, b_end);
    return ex_val(r);
}

static int assign_op(int p)
{
    switch (p) {
    case P_ADD_ASSIGN: return P_PLUS;
    case P_SUB_ASSIGN: return P_MINUS;
    case P_MUL_ASSIGN: return P_STAR;
    case P_DIV_ASSIGN: return P_SLASH;
    case P_MOD_ASSIGN: return P_PERCENT;
    case P_SHL_ASSIGN: return P_SHL;
    case P_SHR_ASSIGN: return P_SHR;
    case P_AND_ASSIGN: return P_AND;
    case P_OR_ASSIGN: return P_OR;
    case P_XOR_ASSIGN: return P_XOR;
    default: return 0;
    }
}

/* Stores v into the place, converting it to the place's type */
static void assign(C *c, const LVal *lv, Val v)
{
    if (v.t != lv->t) {
        if (lv->t->kind == TY_MAT || v.t->kind == TY_MAT ||
            lv->t->kind == TY_ARRAY || lv->t->kind == TY_STRUCT) {
            fail(c, "cannot assign %s to %s", type_name(v.t), type_name(lv->t));
        }
        v = coerce(c, v, lv->t);
    }
    store(c, lv, v);
}

static Ex parse_assignment(C *c)
{
    Ex lhs = parse_ternary(c);
    const Tok *t = peek(c);
    if (t->kind != T_PUNCT) {
        return lhs;
    }
    if (t->punct == P_ASSIGN) {
        next(c);
        if (!lhs.is_lv) {
            fail(c, "assignment to something that is not a variable");
        }
        bool saved_precise = c->precise_ctx;
        c->precise_ctx = c->precise_ctx || lhs.lv.precise;
        Ex rhs = parse_assignment(c);
        c->precise_ctx = saved_precise;
        Val v = rval(c, &rhs);
        LVal lv = lhs.lv;
        assign(c, &lv, v);
        /* the value of the expression: what was stored */
        v = coerce(c, v, lv.t);
        return ex_val(v);
    }
    int bp = assign_op(t->punct);
    if (bp) {
        next(c);
        if (!lhs.is_lv) {
            fail(c, "assignment to something that is not a variable");
        }
        LVal lv = lhs.lv;
        bool saved_precise = c->precise_ctx;
        c->precise_ctx = c->precise_ctx || lv.precise;
        Ex rhs = parse_assignment(c);
        Ex cur = ex_lv(&lv);
        Ex r = binop(c, bp, cur, rhs);
        c->precise_ctx = saved_precise;
        Val v = rval(c, &r);
        if (v.t->kind == TY_MAT && lv.t->kind == TY_MAT) {
            store(c, &lv, v);
        } else {
            assign(c, &lv, v);
        }
        return ex_val(v);
    }
    return lhs;
}

static Ex parse_expr(C *c)
{
    return parse_assignment(c);
}

static Val parse_rvalue(C *c)
{
    Ex e = parse_assignment(c);
    return rval(c, &e);
}

/* The next constant integer expression */
static int32_t parse_const_int(C *c)
{
    Ex e = parse_ternary(c);
    if (!e.is_const || (e.v.t != c->t_int && e.v.t != c->t_uint)) {
        fail(c, "a constant integer is needed here");
    }
    return (int32_t)e.cv;
}

/* ---- places: indexing and swizzles ---- */

/* An rvalue put in a temporary so it can be indexed as a place */
static LVal spill(C *c, Val v)
{
    LVal lv;
    memset(&lv, 0, sizeof(lv));
    lv.t = v.t;
    lv.sc = SpvStorageClassFunction;
    lv.ptr = make_variable(c, v.t, SpvStorageClassFunction, 0);
    ins0(c, SpvOpStore, 2, lv.ptr, v.id);
    return lv;
}

static LVal access_index(C *c, const LVal *base, Val index)
{
    LVal lv;
    Type *et;
    if (base->nswz) {
        fail(c, "indexing a swizzle");
    }
    switch (base->t->kind) {
    case TY_ARRAY: et = base->t->elem; break;
    case TY_MAT: et = base->t->elem; break;
    case TY_VEC: et = scalar_type(c, base->t->base); break;
    default: fail(c, "indexing a %s", type_name(base->t));
    }
    if (index.t != c->t_int && index.t != c->t_uint) {
        fail(c, "an index must be an integer");
    }
    memset(&lv, 0, sizeof(lv));
    lv.t = et;
    lv.sc = base->sc;
    lv.readonly = base->readonly;
    lv.ptr = ins(c, SpvOpAccessChain, ptr_type(c, et, base->sc), 2, base->ptr,
                 index.id);
    return lv;
}

static int swizzle_index(C *c, char ch)
{
    switch (ch) {
    case 'x': case 'r': case 's': return 0;
    case 'y': case 'g': case 't': return 1;
    case 'z': case 'b': case 'p': return 2;
    case 'w': case 'a': case 'q': return 3;
    default: fail(c, "bad swizzle component '%c'", ch);
    }
}

/* e.field on a vector: a swizzle place or value */
static Ex swizzle(C *c, Ex e, const char *field)
{
    int n = strlen(field);
    uint8_t idx[4];
    Type *vt;
    if (n < 1 || n > 4) {
        fail(c, "bad swizzle '%s'", field);
    }
    if (e.is_lv) {
        vt = e.lv.t;
    } else {
        vt = e.v.t;
    }
    if (vt->kind != TY_VEC && !is_scalar(vt)) {
        fail(c, "swizzle of a %s", type_name(vt));
    }
    int vn = vt->kind == TY_VEC ? vt->n : 1;
    for (int i = 0; i < n; i++) {
        idx[i] = swizzle_index(c, field[i]);
        if (idx[i] >= vn) {
            fail(c, "swizzle '%s' past the end of a %s", field, type_name(vt));
        }
    }
    enum TypeKind bk = base_kind(vt);
    Type *rt = vec_type(c, bk, n);
    if (e.is_lv && !e.lv.nswz) {
        /* a place, if no component repeats */
        bool repeats = false;
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < i; k++) {
                if (idx[i] == idx[k]) {
                    repeats = true;
                }
            }
        }
        if (!repeats && vt->kind == TY_VEC) {
            LVal lv = e.lv;
            lv.vec_t = vt;
            lv.t = rt;
            lv.nswz = n;
            memcpy(lv.swz, idx, 4);
            return ex_lv(&lv);
        }
    }
    Val v = rval(c, &e);
    Val r;
    r.t = rt;
    if (vt->kind != TY_VEC) {
        /* scalar.xxx */
        if (n == 1) {
            return ex_val(v);
        }
        r.id = splat(c, rt, v.id);
        return ex_val(r);
    }
    if (n == 1) {
        r.id = ins(c, SpvOpCompositeExtract, rt->id, 2, v.id, (uint32_t)idx[0]);
        return ex_val(r);
    }
    op(&c->fbody, SpvOpVectorShuffle, 4 + n);
    buf_put(&c->fbody, rt->id);
    r.id = new_id(c);
    buf_put(&c->fbody, r.id);
    buf_put(&c->fbody, v.id);
    buf_put(&c->fbody, v.id);
    for (int i = 0; i < n; i++) {
        buf_put(&c->fbody, idx[i]);
    }
    return ex_val(r);
}

/* ---- constructors ---- */

/* The scalar components of v, in order, each converted to kind k */
static int flatten(C *c, Val v, enum TypeKind k, uint32_t *out, int max)
{
    if (is_scalar(v.t)) {
        if (max < 1) {
            fail(c, "too many constructor arguments");
        }
        out[0] = convert(c, v, k).id;
        return 1;
    }
    if (v.t->kind == TY_VEC) {
        if (max < v.t->n) {
            /* take only what fits */
            if (max <= 0) {
                fail(c, "too many constructor arguments");
            }
        }
        int n = v.t->n < max ? v.t->n : max;
        Val cv = convert(c, v, k);
        for (int i = 0; i < n; i++) {
            out[i] = ins(c, SpvOpCompositeExtract, scalar_type(c, k)->id, 2,
                         cv.id, (uint32_t)i);
        }
        return n;
    }
    if (v.t->kind == TY_MAT) {
        int n = 0;
        for (int col = 0; col < v.t->n && n < max; col++) {
            uint32_t colv = ins(c, SpvOpCompositeExtract, v.t->elem->id, 2, v.id,
                                (uint32_t)col);
            for (int row = 0; row < v.t->rows && n < max; row++) {
                uint32_t s = ins(c, SpvOpCompositeExtract, c->t_float->id, 2,
                                 colv, (uint32_t)row);
                Val sv = { c->t_float, s };
                out[n++] = convert(c, sv, k).id;
            }
        }
        return n;
    }
    fail(c, "a %s cannot be a constructor argument", type_name(v.t));
}

static Val construct(C *c, Type *t, Val *args, int nargs)
{
    Val r;
    r.t = t;
    if (is_scalar(t)) {
        if (nargs != 1) {
            fail(c, "%s() takes one argument", type_name(t));
        }
        Val a = args[0];
        if (a.t->kind == TY_VEC) {
            Val s;
            s.t = scalar_type(c, a.t->base);
            s.id = ins(c, SpvOpCompositeExtract, s.t->id, 2, a.id, 0u);
            a = s;
        } else if (!is_scalar(a.t)) {
            fail(c, "%s() of a %s", type_name(t), type_name(a.t));
        }
        return convert(c, a, t->kind);
    }
    if (t->kind == TY_VEC) {
        if (nargs == 1 && is_scalar(args[0].t)) {
            Val s = convert(c, args[0], t->base);
            r.id = splat(c, t, s.id);
            return r;
        }
        if (nargs == 1 && args[0].t->kind == TY_VEC && args[0].t->n >= t->n) {
            Val v = convert(c, args[0], t->base);
            if (args[0].t->n == t->n) {
                return v;
            }
            op(&c->fbody, SpvOpVectorShuffle, 4 + t->n);
            buf_put(&c->fbody, t->id);
            r.id = new_id(c);
            buf_put(&c->fbody, r.id);
            buf_put(&c->fbody, v.id);
            buf_put(&c->fbody, v.id);
            for (int i = 0; i < t->n; i++) {
                buf_put(&c->fbody, i);
            }
            return r;
        }
        uint32_t comps[16];
        int n = 0;
        for (int i = 0; i < nargs; i++) {
            if (n >= t->n) {
                fail(c, "too many arguments to %s()", type_name(t));
            }
            n += flatten(c, args[i], t->base, comps + n, t->n - n);
        }
        if (n != t->n) {
            fail(c, "too few arguments to %s()", type_name(t));
        }
        op(&c->fbody, SpvOpCompositeConstruct, 2 + n);
        buf_put(&c->fbody, t->id);
        r.id = new_id(c);
        buf_put(&c->fbody, r.id);
        for (int i = 0; i < n; i++) {
            buf_put(&c->fbody, comps[i]);
        }
        return r;
    }
    if (t->kind == TY_MAT) {
        uint32_t cols[4];
        if (nargs == 1 && is_scalar(args[0].t)) {
            /* diagonal */
            Val s = convert(c, args[0], TY_FLOAT);
            uint32_t zero = const_float(c, 0.0f);
            for (int col = 0; col < t->n; col++) {
                uint32_t comps[4];
                for (int row = 0; row < t->rows; row++) {
                    comps[row] = row == col ? s.id : zero;
                }
                op(&c->fbody, SpvOpCompositeConstruct, 2 + t->rows);
                buf_put(&c->fbody, t->elem->id);
                cols[col] = new_id(c);
                buf_put(&c->fbody, cols[col]);
                for (int row = 0; row < t->rows; row++) {
                    buf_put(&c->fbody, comps[row]);
                }
            }
        } else if (nargs == 1 && args[0].t->kind == TY_MAT) {
            /* resize: columns and rows taken where they exist, the
             * identity elsewhere */
            Val m = args[0];
            uint32_t zero = const_float(c, 0.0f), one = const_float(c, 1.0f);
            for (int col = 0; col < t->n; col++) {
                uint32_t comps[4];
                uint32_t src_col = 0;
                if (col < m.t->n) {
                    src_col = ins(c, SpvOpCompositeExtract, m.t->elem->id, 2, m.id,
                                  (uint32_t)col);
                }
                for (int row = 0; row < t->rows; row++) {
                    if (col < m.t->n && row < m.t->rows) {
                        comps[row] = ins(c, SpvOpCompositeExtract, c->t_float->id,
                                         2, src_col, (uint32_t)row);
                    } else {
                        comps[row] = row == col ? one : zero;
                    }
                }
                op(&c->fbody, SpvOpCompositeConstruct, 2 + t->rows);
                buf_put(&c->fbody, t->elem->id);
                cols[col] = new_id(c);
                buf_put(&c->fbody, cols[col]);
                for (int row = 0; row < t->rows; row++) {
                    buf_put(&c->fbody, comps[row]);
                }
            }
        } else {
            bool all_cols = nargs == t->n;
            for (int i = 0; i < nargs && all_cols; i++) {
                if (args[i].t != t->elem) {
                    all_cols = false;
                }
            }
            if (all_cols) {
                for (int i = 0; i < nargs; i++) {
                    cols[i] = args[i].id;
                }
            } else {
                uint32_t comps[16];
                int n = 0, want = t->n * t->rows;
                for (int i = 0; i < nargs; i++) {
                    if (n >= want) {
                        fail(c, "too many arguments to %s()", type_name(t));
                    }
                    n += flatten(c, args[i], TY_FLOAT, comps + n, want - n);
                }
                if (n != want) {
                    fail(c, "too few arguments to %s()", type_name(t));
                }
                for (int col = 0; col < t->n; col++) {
                    op(&c->fbody, SpvOpCompositeConstruct, 2 + t->rows);
                    buf_put(&c->fbody, t->elem->id);
                    cols[col] = new_id(c);
                    buf_put(&c->fbody, cols[col]);
                    for (int row = 0; row < t->rows; row++) {
                        buf_put(&c->fbody, comps[col * t->rows + row]);
                    }
                }
            }
        }
        op(&c->fbody, SpvOpCompositeConstruct, 2 + t->n);
        buf_put(&c->fbody, t->id);
        r.id = new_id(c);
        buf_put(&c->fbody, r.id);
        for (int i = 0; i < t->n; i++) {
            buf_put(&c->fbody, cols[i]);
        }
        return r;
    }
    if (t->kind == TY_ARRAY) {
        if (nargs != t->n) {
            fail(c, "an array constructor needs one argument per element");
        }
        op(&c->fbody, SpvOpCompositeConstruct, 2 + nargs);
        buf_put(&c->fbody, t->id);
        r.id = new_id(c);
        buf_put(&c->fbody, r.id);
        for (int i = 0; i < nargs; i++) {
            Val a = coerce(c, args[i], t->elem);
            buf_put(&c->fbody, a.id);
        }
        return r;
    }
    fail(c, "cannot construct a %s", type_name(t));
}

/* Constructor arguments that are all constants give a constant
 * composite, which a const global needs */
static bool all_const(const Ex *args, int n)
{
    for (int i = 0; i < n; i++) {
        if (!args[i].is_const) {
            return false;
        }
    }
    return true;
}

#define MAX_ARGS 32

static int parse_args(C *c, Ex *args)
{
    int n = 0;
    expect(c, P_LPAREN);
    if (accept(c, P_RPAREN)) {
        return 0;
    }
    for (;;) {
        if (n == MAX_ARGS) {
            fail(c, "too many arguments");
        }
        args[n++] = parse_assignment(c);
        if (accept(c, P_COMMA)) {
            continue;
        }
        expect(c, P_RPAREN);
        return n;
    }
}

/* A type possibly followed by [N]: array constructor types */
static Type *parse_array_suffix(C *c, Type *t)
{
    while (accept(c, P_LBRACKET)) {
        int n = 0;
        if (!accept(c, P_RBRACKET)) {
            n = parse_const_int(c);
            if (n <= 0) {
                fail(c, "array size must be positive");
            }
            expect(c, P_RBRACKET);
        }
        t = array_type(c, t, n, 0);
    }
    return t;
}

static Ex parse_constructor(C *c, Type *t)
{
    Ex args[MAX_ARGS];
    int n = parse_args(c, args);
    if (t->kind == TY_ARRAY && t->n == 0) {
        /* T[](...): as many as given */
        if (!n) {
            fail(c, "an empty array constructor");
        }
        t = array_type(c, t->elem, n, 0);
    }
    /* constant scalars and vectors fold */
    if (is_scalar(t) && n == 1 && args[0].is_const) {
        return ex_const(c, t, const_bits_convert(args[0].v.t->kind, t->kind,
                                                 args[0].cv));
    }
    if (t->kind == TY_VEC && n > 0 && all_const(args, n)) {
        uint32_t ids[MAX_ARGS];
        Type *et = scalar_type(c, t->base);
        if (n == 1) {
            uint32_t one = const_scalar(c, et, const_bits_convert(args[0].v.t->kind,
                                                                  et->kind, args[0].cv));
            for (int i = 0; i < t->n; i++) {
                ids[i] = one;
            }
            Val r = { t, const_composite(c, t, ids, t->n) };
            return ex_val(r);
        }
        if (n == t->n) {
            bool ok = true;
            for (int i = 0; i < n; i++) {
                if (!implicit_ok(args[i].v.t->kind, et->kind)) {
                    ok = false;
                    break;
                }
                ids[i] = const_scalar(c, et, const_bits_convert(args[i].v.t->kind,
                                                                et->kind, args[i].cv));
            }
            if (ok) {
                Val r = { t, const_composite(c, t, ids, n) };
                return ex_val(r);
            }
        }
    }
    Val vals[MAX_ARGS];
    for (int i = 0; i < n; i++) {
        vals[i] = rval(c, &args[i]);
    }
    /* an array of constant composites (vec2[9](vec2(..), ...)) is also
     * a constant: its elements came from const_composite */
    if (t->kind == TY_ARRAY && n > 0) {
        bool consts = true;
        uint32_t ids[MAX_ARGS];
        for (int i = 0; i < n; i++) {
            ids[i] = vals[i].id;
            /* an id made in the types section is a constant */
            bool found = false;
            size_t k = 0;
            while (k < c->types.n) {
                uint32_t w = c->types.w[k];
                uint32_t len = w >> 16;
                int opc = w & 0xffff;
                if ((opc == SpvOpConstantComposite || opc == SpvOpConstant ||
                     opc == SpvOpConstantTrue || opc == SpvOpConstantFalse) &&
                    c->types.w[k + 2] == ids[i]) {
                    found = true;
                    break;
                }
                k += len;
            }
            if (!found || vals[i].t != t->elem) {
                consts = false;
                break;
            }
        }
        if (consts) {
            Val r;
            r.t = t;
            r.id = const_composite(c, t, ids, n);
            return ex_val(r);
        }
    }
    return ex_val(construct(c, t, vals, n));
}

/* ---- built-in functions ---- */

static uint32_t ext(C *c, int inst, Type *rt, int nargs, const uint32_t *ids)
{
    op(&c->fbody, SpvOpExtInst, 4 + nargs);
    buf_put(&c->fbody, rt->id);
    uint32_t id = new_id(c);
    buf_put(&c->fbody, id);
    buf_put(&c->fbody, c->glsl_ext_id);
    buf_put(&c->fbody, inst);
    for (int i = 0; i < nargs; i++) {
        buf_put(&c->fbody, ids[i]);
    }
    if (c->precise_ctx && rt->kind != TY_BOOL) {
        decorate(c, id, SpvDecorationNoContraction, 0);
    }
    return id;
}

/* Scalars broadcast to the first argument's vector size, all converted
 * to k (GLSL's genType overloads with scalar second/third arguments) */
static void unify_args(C *c, Val *a, int n, enum TypeKind k)
{
    Type *vt = NULL;
    for (int i = 0; i < n; i++) {
        if (a[i].t->kind == TY_VEC) {
            vt = a[i].t;
            break;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!is_scalar_or_vec(a[i].t)) {
            fail(c, "a %s cannot be an argument here", type_name(a[i].t));
        }
        if (k != TY_VOID) {
            if (!implicit_ok(base_kind(a[i].t), k)) {
                fail(c, "argument of type %s where %s is needed",
                     type_name(a[i].t), type_name(scalar_type(c, k)));
            }
            a[i] = convert(c, a[i], k);
        }
        if (vt && a[i].t->kind != TY_VEC) {
            a[i] = broadcast(c, a[i], vec_type(c, base_kind(a[i].t), vt->n));
        } else if (vt && a[i].t->n != vt->n) {
            fail(c, "vector sizes differ in a call");
        }
    }
}

/* Converts all to one common kind: the highest among them */
static enum TypeKind common_kind(C *c, const Val *a, int n)
{
    enum TypeKind k = TY_BOOL;
    for (int i = 0; i < n; i++) {
        if (!is_scalar_or_vec(a[i].t)) {
            fail(c, "a %s cannot be an argument here", type_name(a[i].t));
        }
        enum TypeKind ak = base_kind(a[i].t);
        if (kind_rank(ak) > kind_rank(k)) {
            k = ak;
        }
    }
    return k;
}

static Val call_builtin(C *c, const char *name, Val *a, int n, bool *handled);

/* The table: name, argument count, how to emit */
enum BiKind {
    BI_EXT_F,     /* GLSL.std.450 on floats, result the same type */
    BI_EXT_FSU,   /* ext inst per kind: float, signed, unsigned */
    BI_EXT_F_SCALAR, /* floats in, float out */
    BI_CORE,      /* a core op, result the same type */
    BI_SPECIAL,
};

typedef struct Builtin {
    const char *name;
    int nargs;        /* -1: 1 or 2 */
    enum BiKind kind;
    int inst_f, inst_s, inst_u;
} Builtin;

static const Builtin builtins[] = {
    { "radians", 1, BI_EXT_F, GLSL_Radians },
    { "degrees", 1, BI_EXT_F, GLSL_Degrees },
    { "sin", 1, BI_EXT_F, GLSL_Sin },
    { "cos", 1, BI_EXT_F, GLSL_Cos },
    { "tan", 1, BI_EXT_F, GLSL_Tan },
    { "asin", 1, BI_EXT_F, GLSL_Asin },
    { "acos", 1, BI_EXT_F, GLSL_Acos },
    { "pow", 2, BI_EXT_F, GLSL_Pow },
    { "exp", 1, BI_EXT_F, GLSL_Exp },
    { "log", 1, BI_EXT_F, GLSL_Log },
    { "exp2", 1, BI_EXT_F, GLSL_Exp2 },
    { "log2", 1, BI_EXT_F, GLSL_Log2 },
    { "sqrt", 1, BI_EXT_F, GLSL_Sqrt },
    { "inversesqrt", 1, BI_EXT_F, GLSL_InverseSqrt },
    { "floor", 1, BI_EXT_F, GLSL_Floor },
    { "trunc", 1, BI_EXT_F, GLSL_Trunc },
    { "round", 1, BI_EXT_F, GLSL_Round },
    { "roundEven", 1, BI_EXT_F, GLSL_RoundEven },
    { "ceil", 1, BI_EXT_F, GLSL_Ceil },
    { "fract", 1, BI_EXT_F, GLSL_Fract },
    { "step", 2, BI_EXT_F, GLSL_Step },
    { "smoothstep", 3, BI_EXT_F, GLSL_SmoothStep },
    { "fma", 3, BI_EXT_F, GLSL_Fma },
    { "normalize", 1, BI_EXT_F, GLSL_Normalize },
    { "reflect", 2, BI_EXT_F, GLSL_Reflect },
    { "refract", 3, BI_EXT_F, GLSL_Refract },
    { "faceforward", 3, BI_EXT_F, GLSL_FaceForward },
    { "abs", 1, BI_EXT_FSU, GLSL_FAbs, GLSL_SAbs, GLSL_SAbs },
    { "sign", 1, BI_EXT_FSU, GLSL_FSign, GLSL_SSign, GLSL_SSign },
    { "min", 2, BI_EXT_FSU, GLSL_FMin, GLSL_SMin, GLSL_UMin },
    { "max", 2, BI_EXT_FSU, GLSL_FMax, GLSL_SMax, GLSL_UMax },
    { "clamp", 3, BI_EXT_FSU, GLSL_FClamp, GLSL_SClamp, GLSL_UClamp },
    { "length", 1, BI_EXT_F_SCALAR, GLSL_Length },
    { "distance", 2, BI_EXT_F_SCALAR, GLSL_Distance },
    { "cross", 2, BI_EXT_F, GLSL_Cross },
};

static Val call_builtin(C *c, const char *name, Val *a, int n, bool *handled)
{
    Val r;
    uint32_t ids[MAX_ARGS];
    *handled = true;
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        const Builtin *b = &builtins[i];
        if (strcmp(b->name, name) != 0) {
            continue;
        }
        if (n != b->nargs) {
            fail(c, "%s() takes %d arguments", name, b->nargs);
        }
        switch (b->kind) {
        case BI_EXT_F:
        case BI_EXT_F_SCALAR:
            unify_args(c, a, n, TY_FLOAT);
            for (int k = 0; k < n; k++) {
                ids[k] = a[k].id;
            }
            r.t = b->kind == BI_EXT_F ? a[0].t : c->t_float;
            r.id = ext(c, b->inst_f, r.t, n, ids);
            return r;
        case BI_EXT_FSU: {
            enum TypeKind k = common_kind(c, a, n);
            if (k == TY_BOOL) {
                fail(c, "%s() of booleans", name);
            }
            unify_args(c, a, n, k);
            for (int i2 = 0; i2 < n; i2++) {
                ids[i2] = a[i2].id;
            }
            r.t = a[0].t;
            r.id = ext(c, k == TY_FLOAT ? b->inst_f : k == TY_INT ? b->inst_s :
                                                                   b->inst_u,
                       r.t, n, ids);
            return r;
        }
        default:
            break;
        }
    }
    if (!strcmp(name, "atan")) {
        if (n < 1 || n > 2) {
            fail(c, "atan() takes one or two arguments");
        }
        unify_args(c, a, n, TY_FLOAT);
        ids[0] = a[0].id;
        ids[1] = n > 1 ? a[1].id : 0;
        r.t = a[0].t;
        r.id = ext(c, n == 2 ? GLSL_Atan2 : GLSL_Atan, r.t, n, ids);
        return r;
    }
    if (!strcmp(name, "mod")) {
        if (n != 2) {
            fail(c, "mod() takes two arguments");
        }
        unify_args(c, a, n, TY_FLOAT);
        r.t = a[0].t;
        r.id = farith(c, SpvOpFMod, r.t, a[0].id, a[1].id);
        return r;
    }
    if (!strcmp(name, "mix")) {
        if (n != 3) {
            fail(c, "mix() takes three arguments");
        }
        if (base_kind(a[2].t) == TY_BOOL) {
            /* mix(x, y, bvec): select */
            Val xy[2] = { a[0], a[1] };
            enum TypeKind k = common_kind(c, xy, 2);
            unify_args(c, xy, 2, k);
            Val sel = a[2];
            if (xy[0].t->kind == TY_VEC && sel.t->kind != TY_VEC) {
                sel = broadcast(c, sel, vec_type(c, TY_BOOL, xy[0].t->n));
            }
            if (xy[0].t->kind == TY_VEC && sel.t->n != xy[0].t->n) {
                fail(c, "mix() selector size differs");
            }
            r.t = xy[0].t;
            r.id = ins(c, SpvOpSelect, r.t->id, 3, sel.id, xy[1].id, xy[0].id);
            return r;
        }
        unify_args(c, a, n, TY_FLOAT);
        ids[0] = a[0].id;
        ids[1] = a[1].id;
        ids[2] = a[2].id;
        r.t = a[0].t;
        r.id = ext(c, GLSL_FMix, r.t, 3, ids);
        return r;
    }
    if (!strcmp(name, "dot")) {
        if (n != 2) {
            fail(c, "dot() takes two arguments");
        }
        unify_args(c, a, n, TY_FLOAT);
        r.t = c->t_float;
        if (a[0].t->kind != TY_VEC) {
            r.id = farith(c, SpvOpFMul, r.t, a[0].id, a[1].id);
        } else {
            r.id = ins(c, SpvOpDot, r.t->id, 2, a[0].id, a[1].id);
            if (c->precise_ctx) {
                decorate(c, r.id, SpvDecorationNoContraction, 0);
            }
        }
        return r;
    }
    if (!strcmp(name, "isnan") || !strcmp(name, "isinf")) {
        if (n != 1) {
            fail(c, "%s() takes one argument", name);
        }
        a[0] = convert(c, a[0], TY_FLOAT);
        r.t = vec_type(c, TY_BOOL, comp_count(a[0].t));
        r.id = ins(c, name[2] == 'n' ? SpvOpIsNan : SpvOpIsInf, r.t->id, 1, a[0].id);
        return r;
    }
    if (!strcmp(name, "floatBitsToUint") || !strcmp(name, "floatBitsToInt") ||
        !strcmp(name, "uintBitsToFloat") || !strcmp(name, "intBitsToFloat")) {
        if (n != 1) {
            fail(c, "%s() takes one argument", name);
        }
        enum TypeKind from = name[0] == 'f' ? TY_FLOAT : name[0] == 'u' ? TY_UINT : TY_INT;
        enum TypeKind to = name[0] == 'f' ? (name[11] == 'U' ? TY_UINT : TY_INT) : TY_FLOAT;
        if (base_kind(a[0].t) != from) {
            if (from == TY_UINT && base_kind(a[0].t) == TY_INT) {
                a[0] = convert(c, a[0], TY_UINT);
            } else {
                fail(c, "%s() of a %s", name, type_name(a[0].t));
            }
        }
        r.t = vec_type(c, to, comp_count(a[0].t));
        r.id = ins(c, SpvOpBitcast, r.t->id, 1, a[0].id);
        return r;
    }
    if (!strcmp(name, "bitfieldExtract")) {
        if (n != 3) {
            fail(c, "bitfieldExtract() takes three arguments");
        }
        enum TypeKind k = base_kind(a[0].t);
        if (k != TY_INT && k != TY_UINT) {
            fail(c, "bitfieldExtract() of a %s", type_name(a[0].t));
        }
        a[1] = convert(c, a[1], TY_INT);
        a[2] = convert(c, a[2], TY_INT);
        r.t = a[0].t;
        r.id = ins(c, k == TY_INT ? SpvOpBitFieldSExtract : SpvOpBitFieldUExtract,
                   r.t->id, 3, a[0].id, a[1].id, a[2].id);
        return r;
    }
    if (!strcmp(name, "bitfieldInsert")) {
        if (n != 4) {
            fail(c, "bitfieldInsert() takes four arguments");
        }
        enum TypeKind k = base_kind(a[0].t);
        if (k != TY_INT && k != TY_UINT) {
            fail(c, "bitfieldInsert() of a %s", type_name(a[0].t));
        }
        a[1] = convert(c, a[1], k);
        a[2] = convert(c, a[2], TY_INT);
        a[3] = convert(c, a[3], TY_INT);
        r.t = a[0].t;
        r.id = ins(c, SpvOpBitFieldInsert, r.t->id, 4, a[0].id, a[1].id, a[2].id,
                   a[3].id);
        return r;
    }
    if (!strcmp(name, "bitCount") || !strcmp(name, "findLSB") ||
        !strcmp(name, "findMSB")) {
        if (n != 1) {
            fail(c, "%s() takes one argument", name);
        }
        enum TypeKind k = base_kind(a[0].t);
        if (k != TY_INT && k != TY_UINT) {
            fail(c, "%s() of a %s", name, type_name(a[0].t));
        }
        r.t = vec_type(c, TY_INT, comp_count(a[0].t));
        if (name[0] == 'b') {
            r.id = ins(c, SpvOpBitCount, r.t->id, 1, a[0].id);
        } else {
            ids[0] = a[0].id;
            r.id = ext(c, name[4] == 'L' ? GLSL_FindILsb :
                          k == TY_INT ? GLSL_FindSMsb : GLSL_FindUMsb, r.t, 1, ids);
        }
        return r;
    }
    if (!strcmp(name, "lessThan") || !strcmp(name, "lessThanEqual") ||
        !strcmp(name, "greaterThan") || !strcmp(name, "greaterThanEqual") ||
        !strcmp(name, "equal") || !strcmp(name, "notEqual")) {
        if (n != 2) {
            fail(c, "%s() takes two arguments", name);
        }
        enum TypeKind k = common_kind(c, a, n);
        unify_args(c, a, n, k);
        if (a[0].t->kind != TY_VEC) {
            fail(c, "%s() needs vectors", name);
        }
        int opcode;
        bool lt = name[0] == 'l', gt = name[0] == 'g';
        bool eq_too = strstr(name, "Equal") != NULL;
        if (k == TY_FLOAT) {
            opcode = lt ? (eq_too ? SpvOpFOrdLessThanEqual : SpvOpFOrdLessThan) :
                     gt ? (eq_too ? SpvOpFOrdGreaterThanEqual : SpvOpFOrdGreaterThan) :
                     name[0] == 'e' ? SpvOpFOrdEqual : SpvOpFUnordNotEqual;
        } else if (k == TY_INT) {
            opcode = lt ? (eq_too ? SpvOpSLessThanEqual : SpvOpSLessThan) :
                     gt ? (eq_too ? SpvOpSGreaterThanEqual : SpvOpSGreaterThan) :
                     name[0] == 'e' ? SpvOpIEqual : SpvOpINotEqual;
        } else if (k == TY_UINT) {
            opcode = lt ? (eq_too ? SpvOpULessThanEqual : SpvOpULessThan) :
                     gt ? (eq_too ? SpvOpUGreaterThanEqual : SpvOpUGreaterThan) :
                     name[0] == 'e' ? SpvOpIEqual : SpvOpINotEqual;
        } else {
            if (lt || gt) {
                fail(c, "%s() of booleans", name);
            }
            opcode = name[0] == 'e' ? SpvOpLogicalEqual : SpvOpLogicalNotEqual;
        }
        r.t = vec_type(c, TY_BOOL, a[0].t->n);
        r.id = ins(c, opcode, r.t->id, 2, a[0].id, a[1].id);
        return r;
    }
    if (!strcmp(name, "any") || !strcmp(name, "all") || !strcmp(name, "not")) {
        if (n != 1 || a[0].t->kind != TY_VEC || a[0].t->base != TY_BOOL) {
            fail(c, "%s() needs a boolean vector", name);
        }
        if (name[0] == 'n') {
            r.t = a[0].t;
            r.id = ins(c, SpvOpLogicalNot, r.t->id, 1, a[0].id);
        } else {
            r.t = c->t_bool;
            r.id = ins(c, name[1] == 'n' ? SpvOpAny : SpvOpAll, r.t->id, 1, a[0].id);
        }
        return r;
    }
    if (!strcmp(name, "transpose")) {
        if (n != 1 || a[0].t->kind != TY_MAT) {
            fail(c, "transpose() needs a matrix");
        }
        r.t = mat_type(c, a[0].t->rows, a[0].t->n);
        r.id = ins(c, SpvOpTranspose, r.t->id, 1, a[0].id);
        return r;
    }
    if (!strcmp(name, "determinant") || !strcmp(name, "inverse")) {
        if (n != 1 || a[0].t->kind != TY_MAT || a[0].t->n != a[0].t->rows) {
            fail(c, "%s() needs a square matrix", name);
        }
        ids[0] = a[0].id;
        r.t = name[0] == 'd' ? c->t_float : a[0].t;
        r.id = ext(c, name[0] == 'd' ? GLSL_Determinant : GLSL_MatrixInverse, r.t,
                   1, ids);
        return r;
    }
    if (!strcmp(name, "matrixCompMult")) {
        if (n != 2 || a[0].t->kind != TY_MAT || a[1].t != a[0].t) {
            fail(c, "matrixCompMult() needs two matrices of a kind");
        }
        uint32_t cols[4];
        for (int col = 0; col < a[0].t->n; col++) {
            uint32_t x = ins(c, SpvOpCompositeExtract, a[0].t->elem->id, 2, a[0].id,
                             (uint32_t)col);
            uint32_t y = ins(c, SpvOpCompositeExtract, a[0].t->elem->id, 2, a[1].id,
                             (uint32_t)col);
            cols[col] = farith(c, SpvOpFMul, a[0].t->elem, x, y);
        }
        op(&c->fbody, SpvOpCompositeConstruct, 2 + a[0].t->n);
        buf_put(&c->fbody, a[0].t->id);
        r.t = a[0].t;
        r.id = new_id(c);
        buf_put(&c->fbody, r.id);
        for (int col = 0; col < a[0].t->n; col++) {
            buf_put(&c->fbody, cols[col]);
        }
        return r;
    }
    if (!strcmp(name, "packUnorm4x8") || !strcmp(name, "packSnorm4x8") ||
        !strcmp(name, "packUnorm2x16") || !strcmp(name, "packSnorm2x16") ||
        !strcmp(name, "packHalf2x16")) {
        int want = strstr(name, "4x8") ? 4 : 2;
        if (n != 1 || a[0].t->kind != TY_VEC || a[0].t->n != want) {
            fail(c, "%s() needs a vec%d", name, want);
        }
        a[0] = convert(c, a[0], TY_FLOAT);
        ids[0] = a[0].id;
        r.t = c->t_uint;
        r.id = ext(c, !strcmp(name, "packUnorm4x8") ? GLSL_PackUnorm4x8 :
                     !strcmp(name, "packSnorm4x8") ? GLSL_PackSnorm4x8 :
                     !strcmp(name, "packUnorm2x16") ? GLSL_PackUnorm2x16 :
                     !strcmp(name, "packSnorm2x16") ? GLSL_PackSnorm2x16 :
                                                      GLSL_PackHalf2x16,
                   r.t, 1, ids);
        return r;
    }
    if (!strcmp(name, "unpackUnorm4x8") || !strcmp(name, "unpackSnorm4x8") ||
        !strcmp(name, "unpackUnorm2x16") || !strcmp(name, "unpackSnorm2x16") ||
        !strcmp(name, "unpackHalf2x16")) {
        int want = strstr(name, "4x8") ? 4 : 2;
        if (n != 1 || a[0].t != c->t_uint) {
            fail(c, "%s() needs a uint", name);
        }
        ids[0] = a[0].id;
        r.t = vec_type(c, TY_FLOAT, want);
        r.id = ext(c, !strcmp(name, "unpackUnorm4x8") ? GLSL_UnpackUnorm4x8 :
                     !strcmp(name, "unpackSnorm4x8") ? GLSL_UnpackSnorm4x8 :
                     !strcmp(name, "unpackUnorm2x16") ? GLSL_UnpackUnorm2x16 :
                     !strcmp(name, "unpackSnorm2x16") ? GLSL_UnpackSnorm2x16 :
                                                        GLSL_UnpackHalf2x16,
                   r.t, 1, ids);
        return r;
    }
    if (!strcmp(name, "texture") || !strcmp(name, "textureProj") ||
        !strcmp(name, "textureLod")) {
        bool proj = name[7] == 'P', lod = name[7] == 'L';
        if (n < 2 || n > 3 || a[0].t->kind != TY_SAMPLED_IMAGE) {
            fail(c, "%s() needs a sampler and a coordinate", name);
        }
        Type *img = a[0].t->elem;
        int coords = img->dim == SpvDim1D ? 1 : img->dim == SpvDim2D ? 2 : 3;
        if (img->arrayed) {
            coords++;
        }
        if (proj) {
            if (a[1].t->kind != TY_VEC || (a[1].t->n != coords + 1 && a[1].t->n != 4)) {
                fail(c, "%s() coordinate is the wrong size", name);
            }
        } else if (comp_count(a[1].t) != coords) {
            fail(c, "%s() coordinate is the wrong size", name);
        }
        a[1] = convert(c, a[1], TY_FLOAT);
        if (c->stage != GLSLC_STAGE_FRAGMENT && !lod) {
            fail(c, "%s() outside a fragment shader; use textureLod()", name);
        }
        r.t = vec_type(c, img->elem->kind, 4);
        if (lod) {
            if (n != 3) {
                fail(c, "textureLod() needs a level");
            }
            a[2] = convert(c, a[2], TY_FLOAT);
            r.id = ins(c, SpvOpImageSampleExplicitLod, r.t->id, 4, a[0].id, a[1].id,
                       (uint32_t)SpvImageOperandsLodMask, a[2].id);
        } else if (n == 3) {
            a[2] = convert(c, a[2], TY_FLOAT);
            r.id = ins(c, proj ? SpvOpImageSampleProjImplicitLod :
                                 SpvOpImageSampleImplicitLod,
                       r.t->id, 4, a[0].id, a[1].id, (uint32_t)SpvImageOperandsBiasMask,
                       a[2].id);
        } else {
            r.id = ins(c, proj ? SpvOpImageSampleProjImplicitLod :
                                 SpvOpImageSampleImplicitLod,
                       r.t->id, 2, a[0].id, a[1].id);
        }
        return r;
    }
    if (!strcmp(name, "textureQueryLod")) {
        if (n != 2 || a[0].t->kind != TY_SAMPLED_IMAGE ||
            a[0].t->elem->dim != SpvDim2D || a[0].t->elem->arrayed ||
            comp_count(a[1].t) != 2 || c->stage != GLSLC_STAGE_FRAGMENT) {
            fail(c, "textureQueryLod() needs a 2D sampler and vec2 in a fragment shader");
        }
        a[1] = convert(c, a[1], TY_FLOAT);
        r.t = vec_type(c, TY_FLOAT, 2);
        r.id = ins(c, SpvOpImageQueryLod, r.t->id, 2, a[0].id, a[1].id);
        c->cap_image_query = true;
        return r;
    }
    if (!strcmp(name, "textureGather")) {
        if (n != 3 || a[0].t->kind != TY_SAMPLED_IMAGE ||
            a[0].t->elem->dim != SpvDim2D || a[0].t->elem->arrayed ||
            comp_count(a[1].t) != 2 || comp_count(a[2].t) != 1) {
            fail(c, "textureGather() needs a 2D sampler, vec2 coordinate and component");
        }
        Type *img = a[0].t->elem;
        a[1] = convert(c, a[1], TY_FLOAT);
        a[2] = convert(c, a[2], TY_INT);
        r.t = vec_type(c, img->elem->kind, 4);
        r.id = ins(c, SpvOpImageGather, r.t->id, 3,
                   a[0].id, a[1].id, a[2].id);
        return r;
    }
    if (!strcmp(name, "texelFetch")) {
        if (n != 3 || a[0].t->kind != TY_SAMPLED_IMAGE) {
            fail(c, "texelFetch() needs a sampler, a coordinate and a level");
        }
        Type *img = a[0].t->elem;
        uint32_t im = ins(c, SpvOpImage, img->id, 1, a[0].id);
        a[1] = convert(c, a[1], TY_INT);
        a[2] = convert(c, a[2], TY_INT);
        r.t = vec_type(c, img->elem->kind, 4);
        r.id = ins(c, SpvOpImageFetch, r.t->id, 4, im, a[1].id,
                   (uint32_t)SpvImageOperandsLodMask, a[2].id);
        return r;
    }
    if (!strcmp(name, "textureSize")) {
        if (n != 2 || a[0].t->kind != TY_SAMPLED_IMAGE) {
            fail(c, "textureSize() needs a sampler and a level");
        }
        Type *img = a[0].t->elem;
        int coords = img->dim == SpvDim1D ? 1 : img->dim == SpvDim2D ? 2 : 3;
        if (img->dim == SpvDimCube) {
            coords = 2;
        }
        if (img->arrayed) {
            coords++;
        }
        uint32_t im = ins(c, SpvOpImage, img->id, 1, a[0].id);
        a[1] = convert(c, a[1], TY_INT);
        r.t = vec_type(c, TY_INT, coords);
        r.id = ins(c, SpvOpImageQuerySizeLod, r.t->id, 2, im, a[1].id);
        c->cap_image_query = true;
        return r;
    }
    if (!strcmp(name, "imageLoad") || !strcmp(name, "imageStore") ||
        !strcmp(name, "imageSize")) {
        if (n < 1 || a[0].t->kind != TY_IMAGE) {
            fail(c, "%s() needs an image", name);
        }
        Type *img = a[0].t;
        int coords = img->dim == SpvDim1D ? 1 : img->dim == SpvDim2D ? 2 : 3;
        if (img->arrayed) {
            coords++;
        }
        if (name[5] == 'S' && name[6] == 'i') {
            if (n != 1) {
                fail(c, "imageSize() takes an image");
            }
            r.t = vec_type(c, TY_INT, coords);
            r.id = ins(c, SpvOpImageQuerySize, r.t->id, 1, a[0].id);
            c->cap_image_query = true;
            return r;
        }
        if (n < 2 || comp_count(a[1].t) != coords) {
            fail(c, "%s() coordinate is the wrong size", name);
        }
        a[1] = convert(c, a[1], TY_INT);
        if (name[5] == 'L') {
            if (n != 2) {
                fail(c, "imageLoad() takes an image and a coordinate");
            }
            r.t = vec_type(c, img->elem->kind, 4);
            r.id = ins(c, SpvOpImageRead, r.t->id, 2, a[0].id, a[1].id);
            if (img->format == SpvImageFormatUnknown) {
                c->cap_image_read_without_format = true;
            }
            return r;
        }
        if (n != 3) {
            fail(c, "imageStore() takes an image, a coordinate and a value");
        }
        a[2] = coerce(c, a[2], vec_type(c, img->elem->kind, 4));
        ins0(c, SpvOpImageWrite, 3, a[0].id, a[1].id, a[2].id);
        if (img->format == SpvImageFormatUnknown) {
            c->cap_image_write_without_format = true;
        }
        r.t = c->t_void;
        r.id = 0;
        return r;
    }
    if (!strcmp(name, "EmitVertex") || !strcmp(name, "EndPrimitive")) {
        if (n != 0) {
            fail(c, "%s() takes no arguments", name);
        }
        if (c->stage != GLSLC_STAGE_GEOMETRY) {
            fail(c, "%s() outside a geometry shader", name);
        }
        ins0(c, name[1] == 'm' ? SpvOpEmitVertex : SpvOpEndPrimitive, 0);
        r.t = c->t_void;
        r.id = 0;
        return r;
    }
    if (!strcmp(name, "barrier")) {
        if (n != 0) {
            fail(c, "barrier() takes no arguments");
        }
        ins0(c, SpvOpControlBarrier, 3, const_uint(c, SpvScopeWorkgroup),
             const_uint(c, SpvScopeWorkgroup),
             const_uint(c, SpvMemorySemanticsAcquireReleaseMask |
                               SpvMemorySemanticsWorkgroupMemoryMask));
        r.t = c->t_void;
        r.id = 0;
        return r;
    }
    if (!strcmp(name, "memoryBarrier") || !strcmp(name, "memoryBarrierShared") ||
        !strcmp(name, "memoryBarrierBuffer") || !strcmp(name, "groupMemoryBarrier")) {
        if (n != 0) {
            fail(c, "%s() takes no arguments", name);
        }
        uint32_t sem = SpvMemorySemanticsAcquireReleaseMask;
        if (!strcmp(name, "memoryBarrierShared")) {
            sem |= SpvMemorySemanticsWorkgroupMemoryMask;
        } else if (!strcmp(name, "memoryBarrierBuffer")) {
            sem |= SpvMemorySemanticsUniformMemoryMask;
        } else {
            sem |= SpvMemorySemanticsWorkgroupMemoryMask |
                   SpvMemorySemanticsUniformMemoryMask |
                   SpvMemorySemanticsImageMemoryMask;
        }
        ins0(c, SpvOpMemoryBarrier, 2,
             const_uint(c, name[0] == 'g' ? SpvScopeWorkgroup : SpvScopeDevice),
             const_uint(c, sem));
        r.t = c->t_void;
        r.id = 0;
        return r;
    }
    *handled = false;
    memset(&r, 0, sizeof(r));
    return r;
}

/* ---- built-in variables ---- */

static uint32_t builtin_var(C *c, uint32_t *slot, Type *t, int sc, int builtin,
                            const char *name)
{
    if (!*slot) {
        *slot = make_variable(c, t, sc, 0);
        decorate(c, *slot, SpvDecorationBuiltIn, 1, (uint32_t)builtin);
        emit_name(c, *slot, name);
    }
    return *slot;
}

/* gl_PerVertex: Position and PointSize, as a block */
static Type *pervertex_type(C *c)
{
    if (c->t_pervertex) {
        return c->t_pervertex;
    }
    Type *t = type_new(c, TY_STRUCT);
    t->nmembers = 2;
    t->members = arena_alloc(&c->arena, 2 * sizeof(Type *));
    t->member_names = arena_alloc(&c->arena, 2 * sizeof(char *));
    t->members[0] = vec_type(c, TY_FLOAT, 4);
    t->members[1] = c->t_float;
    t->member_names[0] = "gl_Position";
    t->member_names[1] = "gl_PointSize";
    t->id = new_id(c);
    op(&c->types, SpvOpTypeStruct, 3);
    buf_put(&c->types, t->id);
    buf_put(&c->types, t->members[0]->id);
    buf_put(&c->types, t->members[1]->id);
    decorate(c, t->id, SpvDecorationBlock, 0);
    member_decorate(c, t->id, 0, SpvDecorationBuiltIn, 1, (uint32_t)SpvBuiltInPosition);
    member_decorate(c, t->id, 1, SpvDecorationBuiltIn, 1, (uint32_t)SpvBuiltInPointSize);
    emit_name(c, t->id, "gl_PerVertex");
    emit_member_name(c, t->id, 0, "gl_Position");
    emit_member_name(c, t->id, 1, "gl_PointSize");
    c->t_pervertex = t;
    return t;
}

static LVal member_lv(C *c, const LVal *base, int member)
{
    LVal lv;
    memset(&lv, 0, sizeof(lv));
    lv.t = base->t->members[member];
    lv.sc = base->sc;
    lv.readonly = base->readonly;
    lv.ptr = ins(c, SpvOpAccessChain, ptr_type(c, lv.t, base->sc), 2, base->ptr,
                 const_int(c, member));
    if (base->t->member_bool && base->t->member_bool[member]) {
        lv.as_bool = true;
        lv.store_t = lv.t;
        lv.t = vec_type(c, TY_BOOL, comp_count(lv.t));
    }
    return lv;
}

/* A built-in variable by name, as a place, or false */
static bool builtin_lv(C *c, const char *name, LVal *lv)
{
    memset(lv, 0, sizeof(*lv));
    if (!strcmp(name, "gl_Position") || !strcmp(name, "gl_PointSize")) {
        if (c->stage != GLSLC_STAGE_VERTEX && c->stage != GLSLC_STAGE_GEOMETRY) {
            fail(c, "%s outside a vertex or geometry shader", name);
        }
        Type *pv = pervertex_type(c);
        if (!c->bi_pervertex_out) {
            c->bi_pervertex_out = make_variable(c, pv, SpvStorageClassOutput, 0);
            emit_name(c, c->bi_pervertex_out, "");
        }
        LVal base;
        memset(&base, 0, sizeof(base));
        base.t = pv;
        base.sc = SpvStorageClassOutput;
        base.ptr = c->bi_pervertex_out;
        *lv = member_lv(c, &base, name[3] == 'P' && name[4] == 'o' && name[5] == 's' ? 0 : 1);
        return true;
    }
    if (!strcmp(name, "gl_in")) {
        if (c->stage != GLSLC_STAGE_GEOMETRY) {
            fail(c, "gl_in outside a geometry shader");
        }
        if (!c->geom_in_vertices) {
            fail(c, "gl_in before the input primitive layout");
        }
        Type *pv = pervertex_type(c);
        Type *arr = array_type(c, pv, c->geom_in_vertices, 0);
        if (!c->bi_gl_in) {
            c->bi_gl_in = make_variable(c, arr, SpvStorageClassInput, 0);
            emit_name(c, c->bi_gl_in, "gl_in");
        }
        lv->t = arr;
        lv->sc = SpvStorageClassInput;
        lv->ptr = c->bi_gl_in;
        lv->readonly = true;
        return true;
    }
    struct {
        const char *name;
        uint32_t *slot;
        Type *t;
        int sc, builtin;
        GlslcStage stage;
    } table[] = {
        { "gl_FragCoord", &c->bi_fragcoord, vec_type(c, TY_FLOAT, 4),
          SpvStorageClassInput, SpvBuiltInFragCoord, GLSLC_STAGE_FRAGMENT },
        { "gl_FrontFacing", &c->bi_frontfacing, c->t_bool, SpvStorageClassInput,
          SpvBuiltInFrontFacing, GLSLC_STAGE_FRAGMENT },
        { "gl_PointCoord", &c->bi_pointcoord, vec_type(c, TY_FLOAT, 2),
          SpvStorageClassInput, SpvBuiltInPointCoord, GLSLC_STAGE_FRAGMENT },
        { "gl_FragDepth", &c->bi_fragdepth, c->t_float, SpvStorageClassOutput,
          SpvBuiltInFragDepth, GLSLC_STAGE_FRAGMENT },
        { "gl_PrimitiveIDIn", &c->bi_primitiveid, c->t_int, SpvStorageClassInput,
          SpvBuiltInPrimitiveId, GLSLC_STAGE_GEOMETRY },
        { "gl_GlobalInvocationID", &c->bi_globalinv, vec_type(c, TY_UINT, 3),
          SpvStorageClassInput, SpvBuiltInGlobalInvocationId, GLSLC_STAGE_COMPUTE },
        { "gl_LocalInvocationID", &c->bi_localinv, vec_type(c, TY_UINT, 3),
          SpvStorageClassInput, SpvBuiltInLocalInvocationId, GLSLC_STAGE_COMPUTE },
        { "gl_WorkGroupID", &c->bi_workgroupid, vec_type(c, TY_UINT, 3),
          SpvStorageClassInput, SpvBuiltInWorkgroupId, GLSLC_STAGE_COMPUTE },
        { "gl_LocalInvocationIndex", &c->bi_localinvindex, c->t_uint,
          SpvStorageClassInput, SpvBuiltInLocalInvocationIndex, GLSLC_STAGE_COMPUTE },
        { "gl_NumWorkGroups", &c->bi_numworkgroups, vec_type(c, TY_UINT, 3),
          SpvStorageClassInput, SpvBuiltInNumWorkgroups, GLSLC_STAGE_COMPUTE },
        { "gl_VertexIndex", &c->bi_vertexindex, c->t_int, SpvStorageClassInput,
          SpvBuiltInVertexIndex, GLSLC_STAGE_VERTEX },
        { "gl_InstanceIndex", &c->bi_instanceindex, c->t_int, SpvStorageClassInput,
          SpvBuiltInInstanceIndex, GLSLC_STAGE_VERTEX },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(table[i].name, name) != 0) {
            continue;
        }
        if (c->stage != table[i].stage) {
            fail(c, "%s is not available in this shader stage", name);
        }
        lv->t = table[i].t;
        lv->sc = table[i].sc;
        lv->ptr = builtin_var(c, table[i].slot, table[i].t, table[i].sc,
                              table[i].builtin, name);
        lv->readonly = table[i].sc == SpvStorageClassInput;
        if (table[i].slot == &c->bi_fragdepth) {
            c->depth_replacing = true;
        }
        return true;
    }
    return false;
}

/* ---- primary and postfix expressions ---- */

static Ex parse_postfix(C *c, Ex e);

static Ex parse_call(C *c, const char *name)
{
    Ex args[MAX_ARGS];
    int n = parse_args(c, args);
    if (func_named(c, name)) {
        Type *types[MAX_ARGS];
        for (int i = 0; i < n; i++) {
            types[i] = args[i].is_lv ? args[i].lv.t : args[i].v.t;
        }
        Func *f = func_resolve(c, name, types, n);
        if (!f) {
            fail(c, "no %s() takes these arguments", name);
        }
        /* An out or inout argument is passed as a pointer: the variable's
         * own when it is a whole local, else a copy's, copied in before
         * (inout) and out after, as the GLSL semantics say. */
        uint32_t ids[MAX_ARGS];
        uint32_t copy_out[MAX_ARGS];
        for (int i = 0; i < n; i++) {
            copy_out[i] = 0;
            if (f->pmode[i] == PM_IN) {
                Val v = coerce(c, rval(c, &args[i]), f->params[i]);
                ids[i] = v.id;
                continue;
            }
            if (!args[i].is_lv || args[i].lv.readonly) {
                fail(c, "argument %d of %s() must be assignable", i + 1,
                     name);
            }
            if (args[i].lv.t != f->params[i]) {
                fail(c, "argument %d of %s() must be a %s", i + 1, name,
                     type_name(f->params[i]));
            }
            const LVal *lv = &args[i].lv;
            if (lv->sc == SpvStorageClassFunction && !lv->nswz &&
                !lv->as_bool) {
                ids[i] = lv->ptr;
                continue;
            }
            ids[i] = make_variable(c, f->params[i], SpvStorageClassFunction,
                                   0);
            if (f->pmode[i] == PM_INOUT) {
                Val v = load(c, lv);
                ins0(c, SpvOpStore, 2, ids[i], v.id);
            }
            copy_out[i] = ids[i];
        }
        op(&c->fbody, SpvOpFunctionCall, 3 + n);
        buf_put(&c->fbody, f->ret->id);
        uint32_t id = new_id(c);
        buf_put(&c->fbody, id);
        buf_put(&c->fbody, f->id);
        for (int i = 0; i < n; i++) {
            buf_put(&c->fbody, ids[i]);
        }
        for (int i = 0; i < n; i++) {
            if (copy_out[i]) {
                LVal tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.t = f->params[i];
                tmp.sc = SpvStorageClassFunction;
                tmp.ptr = copy_out[i];
                store(c, &args[i].lv, load(c, &tmp));
            }
        }
        Val r = { f->ret, id };
        return ex_val(r);
    }
    Val vals[MAX_ARGS];
    for (int i = 0; i < n; i++) {
        vals[i] = rval(c, &args[i]);
    }
    bool handled;
    Val r = call_builtin(c, name, vals, n, &handled);
    if (!handled) {
        fail(c, "unknown function %s()", name);
    }
    return ex_val(r);
}

static Ex parse_primary(C *c)
{
    const Tok *t = next(c);
    switch (t->kind) {
    case T_INT:
        return ex_const(c, c->t_int, t->val.u);
    case T_UINT:
        return ex_const(c, c->t_uint, t->val.u);
    case T_FLOAT: {
        uint32_t bits;
        memcpy(&bits, &t->val.f, 4);
        return ex_const(c, c->t_float, bits);
    }
    case T_PUNCT:
        if (t->punct == P_LPAREN) {
            Ex e = parse_expr(c);
            expect(c, P_RPAREN);
            return e;
        }
        fail(c, "unexpected '%s'", punct_name(t->punct));
    case T_IDENT: {
        const char *name = t->text;
        if (!strcmp(name, "true")) {
            return ex_const(c, c->t_bool, 1);
        }
        if (!strcmp(name, "false")) {
            return ex_const(c, c->t_bool, 0);
        }
        Type *ty = parse_type_keyword(c, name);
        if (ty) {
            ty = parse_array_suffix(c, ty);
            return parse_constructor(c, ty);
        }
        if (is_punct(c, P_LPAREN)) {
            return parse_call(c, name);
        }
        Sym *s = sym_find(c, name);
        if (s) {
            if (s->has_const) {
                return ex_const(c, s->lv.t, s->cv);
            }
            if (s->member >= 0) {
                LVal lv = member_lv(c, &s->lv, s->member);
                return ex_lv(&lv);
            }
            return ex_lv(&s->lv);
        }
        LVal lv;
        if (builtin_lv(c, name, &lv)) {
            return ex_lv(&lv);
        }
        fail(c, "'%s' is not declared", name);
    }
    default:
        fail(c, "unexpected end of the shader");
    }
}

static Ex parse_postfix(C *c, Ex e)
{
    for (;;) {
        if (accept(c, P_LBRACKET)) {
            Ex ie = parse_expr(c);
            expect(c, P_RBRACKET);
            Val idx = rval(c, &ie);
            if (!e.is_lv) {
                Val v = rval(c, &e);
                if (ie.is_const && v.t->kind == TY_VEC) {
                    Val r;
                    r.t = scalar_type(c, v.t->base);
                    if (ie.cv >= (uint32_t)v.t->n) {
                        fail(c, "index past the end");
                    }
                    r.id = ins(c, SpvOpCompositeExtract, r.t->id, 2, v.id, ie.cv);
                    e = ex_val(r);
                    continue;
                }
                if (ie.is_const && v.t->kind == TY_MAT) {
                    Val r;
                    r.t = v.t->elem;
                    if (ie.cv >= (uint32_t)v.t->n) {
                        fail(c, "index past the end");
                    }
                    r.id = ins(c, SpvOpCompositeExtract, r.t->id, 2, v.id, ie.cv);
                    e = ex_val(r);
                    continue;
                }
                LVal tmp = spill(c, v);
                e = ex_lv(&tmp);
            }
            if (e.lv.nswz) {
                fail(c, "indexing a swizzle");
            }
            LVal lv = access_index(c, &e.lv, idx);
            e = ex_lv(&lv);
            continue;
        }
        if (accept(c, P_DOT)) {
            const char *field = expect_ident(c);
            Type *t = e.is_lv ? e.lv.t : e.v.t;
            if (t->kind == TY_STRUCT) {
                if (!e.is_lv) {
                    Val v = rval(c, &e);
                    LVal tmp = spill(c, v);
                    e = ex_lv(&tmp);
                }
                int m;
                for (m = 0; m < t->nmembers; m++) {
                    if (!strcmp(t->member_names[m], field)) {
                        break;
                    }
                }
                if (m == t->nmembers) {
                    fail(c, "no member '%s'", field);
                }
                LVal lv = member_lv(c, &e.lv, m);
                e = ex_lv(&lv);
                continue;
            }
            if (!strcmp(field, "length") && t->kind == TY_ARRAY) {
                expect(c, P_LPAREN);
                expect(c, P_RPAREN);
                if (!t->n) {
                    fail(c, ".length() of a runtime array is not supported");
                }
                e = ex_const(c, c->t_int, t->n);
                continue;
            }
            e = swizzle(c, e, field);
            continue;
        }
        if (is_punct(c, P_INC) || is_punct(c, P_DEC)) {
            bool inc = next(c)->punct == P_INC;
            if (!e.is_lv) {
                fail(c, "'%s' needs a variable", inc ? "++" : "--");
            }
            LVal lv = e.lv;
            Val old = load(c, &lv);
            Ex one = base_kind(old.t) == TY_FLOAT ? ex_const(c, c->t_float, 0x3f800000u) :
                     base_kind(old.t) == TY_UINT ? ex_const(c, c->t_uint, 1) :
                                                   ex_const(c, c->t_int, 1);
            Ex nv = binop(c, inc ? P_PLUS : P_MINUS, ex_val(old), one);
            Val nvv = rval(c, &nv);
            store(c, &lv, nvv);
            e = ex_val(old);
            continue;
        }
        return e;
    }
}

static Ex parse_unary(C *c)
{
    const Tok *t = peek(c);
    if (t->kind == T_PUNCT) {
        switch (t->punct) {
        case P_PLUS:
            next(c);
            return parse_unary(c);
        case P_MINUS: {
            next(c);
            Ex e = parse_unary(c);
            if (e.is_const) {
                if (e.v.t == c->t_float) {
                    return ex_const(c, c->t_float, e.cv ^ 0x80000000u);
                }
                return ex_const(c, e.v.t, (uint32_t)(-(int32_t)e.cv));
            }
            Val v = rval(c, &e);
            Val r;
            r.t = v.t;
            if (v.t->kind == TY_MAT) {
                uint32_t cols[4];
                for (int col = 0; col < v.t->n; col++) {
                    uint32_t x = ins(c, SpvOpCompositeExtract, v.t->elem->id, 2, v.id,
                                     (uint32_t)col);
                    cols[col] = ins(c, SpvOpFNegate, v.t->elem->id, 1, x);
                }
                op(&c->fbody, SpvOpCompositeConstruct, 2 + v.t->n);
                buf_put(&c->fbody, v.t->id);
                r.id = new_id(c);
                buf_put(&c->fbody, r.id);
                for (int col = 0; col < v.t->n; col++) {
                    buf_put(&c->fbody, cols[col]);
                }
                return ex_val(r);
            }
            enum TypeKind k = base_kind(v.t);
            if (k == TY_BOOL) {
                fail(c, "'-' on a boolean");
            }
            r.id = ins(c, k == TY_FLOAT ? SpvOpFNegate : SpvOpSNegate, v.t->id, 1,
                       v.id);
            return ex_val(r);
        }
        case P_NOT: {
            next(c);
            Ex e = parse_unary(c);
            if (e.is_const && e.v.t == c->t_bool) {
                return ex_const(c, c->t_bool, !e.cv);
            }
            Val v = rval(c, &e);
            if (v.t != c->t_bool) {
                fail(c, "'!' needs a boolean");
            }
            Val r;
            r.t = c->t_bool;
            r.id = ins(c, SpvOpLogicalNot, c->t_bool->id, 1, v.id);
            return ex_val(r);
        }
        case P_TILDE: {
            next(c);
            Ex e = parse_unary(c);
            if (e.is_const && (e.v.t == c->t_int || e.v.t == c->t_uint)) {
                return ex_const(c, e.v.t, ~e.cv);
            }
            Val v = rval(c, &e);
            enum TypeKind k = base_kind(v.t);
            if (k != TY_INT && k != TY_UINT) {
                fail(c, "'~' needs an integer");
            }
            Val r;
            r.t = v.t;
            r.id = ins(c, SpvOpNot, v.t->id, 1, v.id);
            return ex_val(r);
        }
        case P_INC: case P_DEC: {
            bool inc = next(c)->punct == P_INC;
            Ex e = parse_unary(c);
            if (!e.is_lv) {
                fail(c, "'%s' needs a variable", inc ? "++" : "--");
            }
            LVal lv = e.lv;
            Val old = load(c, &lv);
            Ex one = base_kind(old.t) == TY_FLOAT ? ex_const(c, c->t_float, 0x3f800000u) :
                     base_kind(old.t) == TY_UINT ? ex_const(c, c->t_uint, 1) :
                                                   ex_const(c, c->t_int, 1);
            Ex nv = binop(c, inc ? P_PLUS : P_MINUS, ex_val(old), one);
            Val nvv = rval(c, &nv);
            store(c, &lv, nvv);
            return ex_val(nvv);
        }
        default:
            break;
        }
    }
    Ex e = parse_primary(c);
    return parse_postfix(c, e);
}

/* ---- qualifiers and declarations ---- */

static int image_format_from_name(const char *s)
{
    static const struct {
        const char *name;
        int format;
    } f[] = {
        { "rgba32f", SpvImageFormatRgba32f }, { "rgba16f", SpvImageFormatRgba16f },
        { "r32f", SpvImageFormatR32f }, { "rgba8", SpvImageFormatRgba8 },
        { "rgba8_snorm", SpvImageFormatRgba8Snorm }, { "rg32f", SpvImageFormatRg32f },
        { "rg16f", SpvImageFormatRg16f }, { "r16f", SpvImageFormatR16f },
        { "rgba16", SpvImageFormatRgba16 }, { "rgb10_a2", SpvImageFormatRgb10A2 },
        { "rg16", SpvImageFormatRg16 }, { "rg8", SpvImageFormatRg8 },
        { "r16", SpvImageFormatR16 }, { "r8", SpvImageFormatR8 },
        { "rgba32i", SpvImageFormatRgba32i }, { "rgba16i", SpvImageFormatRgba16i },
        { "rgba8i", SpvImageFormatRgba8i }, { "r32i", SpvImageFormatR32i },
        { "rg32i", SpvImageFormatRg32i }, { "rg16i", SpvImageFormatRg16i },
        { "rg8i", SpvImageFormatRg8i }, { "r16i", SpvImageFormatR16i },
        { "r8i", SpvImageFormatR8i }, { "rgba32ui", SpvImageFormatRgba32ui },
        { "rgba16ui", SpvImageFormatRgba16ui }, { "rgba8ui", SpvImageFormatRgba8ui },
        { "r32ui", SpvImageFormatR32ui }, { "rg32ui", SpvImageFormatRg32ui },
        { "rg16ui", SpvImageFormatRg16ui }, { "rg8ui", SpvImageFormatRg8ui },
        { "r16ui", SpvImageFormatR16ui }, { "r8ui", SpvImageFormatR8ui },
    };
    for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
        if (!strcmp(f[i].name, s)) {
            return f[i].format;
        }
    }
    return -1;
}

/* layout(...) and the storage/interpolation words before a type */
static void parse_qualifiers(C *c, Quals *q)
{
    memset(q, 0, sizeof(*q));
    q->sc = -1;
    q->format = -1;
    for (;;) {
        const Tok *t = peek(c);
        if (t->kind != T_IDENT) {
            return;
        }
        if (!strcmp(t->text, "layout")) {
            next(c);
            expect(c, P_LPAREN);
            for (;;) {
                const char *k = expect_ident(c);
                if (!strcmp(k, "location") || !strcmp(k, "binding") ||
                    !strcmp(k, "set") || !strcmp(k, "local_size_x") ||
                    !strcmp(k, "local_size_y") || !strcmp(k, "local_size_z") ||
                    !strcmp(k, "max_vertices") || !strcmp(k, "invocations") ||
                    !strcmp(k, "offset") || !strcmp(k, "index")) {
                    expect(c, P_ASSIGN);
                    int v = parse_const_int(c);
                    if (!strcmp(k, "location")) {
                        q->has_location = true;
                        q->location = v;
                    } else if (!strcmp(k, "binding")) {
                        q->has_binding = true;
                        q->binding = v;
                    } else if (!strcmp(k, "set")) {
                        q->has_set = true;
                        q->set = v;
                    } else if (!strcmp(k, "local_size_x")) {
                        c->local_size[0] = v;
                        c->have_local_size = true;
                    } else if (!strcmp(k, "local_size_y")) {
                        c->local_size[1] = v;
                        c->have_local_size = true;
                    } else if (!strcmp(k, "local_size_z")) {
                        c->local_size[2] = v;
                        c->have_local_size = true;
                    } else if (!strcmp(k, "max_vertices")) {
                        op(&c->exec_modes, SpvOpExecutionMode, 3);
                        buf_put(&c->exec_modes, 0); /* patched: main's id */
                        buf_put(&c->exec_modes, SpvExecutionModeOutputVertices);
                        buf_put(&c->exec_modes, v);
                    } else if (!strcmp(k, "invocations")) {
                        op(&c->exec_modes, SpvOpExecutionMode, 3);
                        buf_put(&c->exec_modes, 0);
                        buf_put(&c->exec_modes, SpvExecutionModeInvocations);
                        buf_put(&c->exec_modes, v);
                    } else {
                        fail(c, "layout(%s) is not supported", k);
                    }
                } else if (!strcmp(k, "std140")) {
                    q->std140 = true;
                } else if (!strcmp(k, "std430")) {
                    q->std430 = true;
                } else if (!strcmp(k, "push_constant")) {
                    q->is_push = true;
                } else if (!strcmp(k, "points") || !strcmp(k, "lines") ||
                           !strcmp(k, "triangles") ||
                           !strcmp(k, "lines_adjacency") ||
                           !strcmp(k, "triangles_adjacency") ||
                           !strcmp(k, "line_strip") ||
                           !strcmp(k, "triangle_strip")) {
                    /* geometry primitive layouts, applied by the in/out
                     * that follows */
                    int mode, verts = 0;
                    bool out = false;
                    if (!strcmp(k, "points")) {
                        mode = SpvExecutionModeInputPoints;
                        verts = 1;
                    } else if (!strcmp(k, "lines")) {
                        mode = SpvExecutionModeInputLines;
                        verts = 2;
                    } else if (!strcmp(k, "triangles")) {
                        mode = SpvExecutionModeTriangles;
                        verts = 3;
                    } else if (!strcmp(k, "lines_adjacency")) {
                        mode = SpvExecutionModeInputLinesAdjacency;
                        verts = 4;
                    } else if (!strcmp(k, "triangles_adjacency")) {
                        mode = SpvExecutionModeInputTrianglesAdjacency;
                        verts = 6;
                    } else if (!strcmp(k, "line_strip")) {
                        mode = SpvExecutionModeOutputLineStrip;
                        out = true;
                    } else {
                        mode = SpvExecutionModeOutputTriangleStrip;
                        out = true;
                    }
                    if (c->stage != GLSLC_STAGE_GEOMETRY) {
                        fail(c, "layout(%s) outside a geometry shader", k);
                    }
                    /* applied by the in/out that follows; "points" is
                     * both an input and an output mode */
                    q->geom_mode = mode;
                    q->geom_points = !strcmp(k, "points");
                    if (!out) {
                        c->geom_in_vertices = verts;
                    }
                } else if (!strcmp(k, "origin_upper_left") ||
                           !strcmp(k, "pixel_center_integer") ||
                           !strcmp(k, "early_fragment_tests") ||
                           !strcmp(k, "depth_any") || !strcmp(k, "column_major") ||
                           !strcmp(k, "row_major")) {
                    if (!strcmp(k, "row_major")) {
                        fail(c, "row_major matrices are not supported");
                    }
                    if (!strcmp(k, "early_fragment_tests")) {
                        op(&c->exec_modes, SpvOpExecutionMode, 2);
                        buf_put(&c->exec_modes, 0);
                        buf_put(&c->exec_modes, SpvExecutionModeEarlyFragmentTests);
                    }
                } else {
                    int fmt = image_format_from_name(k);
                    if (fmt < 0) {
                        fail(c, "layout(%s) is not supported", k);
                    }
                    q->format = fmt;
                }
                if (accept(c, P_COMMA)) {
                    continue;
                }
                expect(c, P_RPAREN);
                break;
            }
            continue;
        }
        if (!strcmp(t->text, "const")) {
            q->is_const = true;
        } else if (!strcmp(t->text, "precise")) {
            q->is_precise = true;
        } else if (!strcmp(t->text, "flat")) {
            q->is_flat = true;
        } else if (!strcmp(t->text, "noperspective")) {
            q->is_noperspective = true;
        } else if (!strcmp(t->text, "centroid")) {
            q->is_centroid = true;
        } else if (!strcmp(t->text, "smooth") || !strcmp(t->text, "invariant") ||
                   !strcmp(t->text, "highp") || !strcmp(t->text, "mediump") ||
                   !strcmp(t->text, "lowp") || !strcmp(t->text, "coherent") ||
                   !strcmp(t->text, "volatile") || !strcmp(t->text, "restrict")) {
            /* accepted, nothing to do */
        } else if (!strcmp(t->text, "in")) {
            q->is_in = true;
        } else if (!strcmp(t->text, "out")) {
            q->is_out = true;
        } else if (!strcmp(t->text, "inout")) {
            q->is_in = q->is_out = true;
        } else if (!strcmp(t->text, "uniform")) {
            q->is_uniform = true;
        } else if (!strcmp(t->text, "buffer")) {
            q->is_buffer = true;
        } else if (!strcmp(t->text, "shared")) {
            q->is_shared = true;
        } else if (!strcmp(t->text, "readonly")) {
            q->readonly = true;
        } else if (!strcmp(t->text, "writeonly")) {
            q->writeonly = true;
        } else {
            return;
        }
        next(c);
    }
}

/* The type after the qualifiers */
static Type *parse_type(C *c, const Quals *q)
{
    const char *name = expect_ident(c);
    Type *t = parse_type_keyword(c, name);
    if (!t) {
        t = parse_opaque_type(c, name, q && q->format >= 0 ? q->format :
                                                            SpvImageFormatUnknown);
    }
    if (!t) {
        fail(c, "'%s' is not a type", name);
    }
    return parse_array_suffix(c, t);
}

/* A block's member list into a struct type, laid out */
static Type *parse_block_members(C *c, bool std140, const char *block_name,
                                 bool for_push)
{
    Type *t = type_new(c, TY_STRUCT);
    t->members = arena_alloc(&c->arena, MAX_MEMBERS * sizeof(Type *));
    t->member_names = arena_alloc(&c->arena, MAX_MEMBERS * sizeof(char *));
    t->member_offsets = arena_alloc(&c->arena, MAX_MEMBERS * sizeof(uint32_t));
    t->member_bool = arena_alloc(&c->arena, MAX_MEMBERS * sizeof(bool));
    memset(t->member_bool, 0, MAX_MEMBERS * sizeof(bool));
    uint32_t offset = 0;
    expect(c, P_LBRACE);
    while (!accept(c, P_RBRACE)) {
        Quals mq;
        parse_qualifiers(c, &mq);
        const char *tn = expect_ident(c);
        Type *mt = parse_type_keyword(c, tn);
        if (!mt) {
            fail(c, "'%s' is not a type a block can hold", tn);
        }
        bool was_bool = base_kind(mt) == TY_BOOL;
        if (was_bool) {
            /* a bool in a block is a 32-bit word */
            mt = vec_type(c, TY_UINT, comp_count(mt));
        }
        for (;;) {
            if (t->nmembers == MAX_MEMBERS) {
                fail(c, "too many block members");
            }
            const char *mname = expect_ident(c);
            Type *ft = mt;
            uint32_t size, align;
            t->member_bool[t->nmembers] = was_bool;
            if (accept(c, P_LBRACKET)) {
                if (was_bool) {
                    fail(c, "bool arrays in blocks are not supported");
                }
                int n = 0;
                if (!accept(c, P_RBRACKET)) {
                    n = parse_const_int(c);
                    if (n <= 0) {
                        fail(c, "array size must be positive");
                    }
                    expect(c, P_RBRACKET);
                }
                ft = layout_array(c, mt, n, std140, &size, &align);
            } else {
                layout_type(c, ft, std140);
                size = ft->size;
                align = ft->align;
            }
            offset = round_up(offset, align);
            t->members[t->nmembers] = ft;
            t->member_names[t->nmembers] = mname;
            t->member_offsets[t->nmembers] = offset;
            t->nmembers++;
            offset += size;
            if (accept(c, P_COMMA)) {
                continue;
            }
            expect(c, P_SEMI);
            break;
        }
    }
    if (!t->nmembers) {
        fail(c, "empty block %s", block_name);
    }
    t->id = new_id(c);
    op(&c->types, SpvOpTypeStruct, 1 + t->nmembers);
    buf_put(&c->types, t->id);
    for (int i = 0; i < t->nmembers; i++) {
        buf_put(&c->types, t->members[i]->id);
    }
    t->is_block = true;
    decorate(c, t->id, SpvDecorationBlock, 0);
    emit_name(c, t->id, block_name);
    for (int i = 0; i < t->nmembers; i++) {
        Type *mt = t->members[i];
        emit_member_name(c, t->id, i, t->member_names[i]);
        member_decorate(c, t->id, i, SpvDecorationOffset, 1, t->member_offsets[i]);
        Type *inner = mt->kind == TY_ARRAY ? mt->elem : mt;
        if (inner->kind == TY_MAT) {
            member_decorate(c, t->id, i, SpvDecorationColMajor, 0);
            member_decorate(c, t->id, i, SpvDecorationMatrixStride, 1, inner->align);
        }
    }
    (void)for_push;
    return t;
}

/* An interface variable's decorations */
static void decorate_interface(C *c, uint32_t id, const Quals *q)
{
    if (q->has_location) {
        decorate(c, id, SpvDecorationLocation, 1, (uint32_t)q->location);
    }
    if (q->has_binding) {
        decorate(c, id, SpvDecorationBinding, 1, (uint32_t)q->binding);
    }
    if (q->is_flat) {
        decorate(c, id, SpvDecorationFlat, 0);
    }
    if (q->is_noperspective) {
        decorate(c, id, SpvDecorationNoPerspective, 0);
    }
    if (q->is_centroid) {
        decorate(c, id, SpvDecorationCentroid, 0);
    }
}

/* Converts the bits of a scalar constant between kinds */
static uint32_t const_bits_convert(enum TypeKind from, enum TypeKind to,
                                   uint32_t bits)
{
    if (from == to) {
        return bits;
    }
    if (to == TY_FLOAT) {
        float f = from == TY_INT ? (float)(int32_t)bits :
                  from == TY_UINT ? (float)bits : (float)(bits != 0);
        memcpy(&bits, &f, 4);
        return bits;
    }
    if (from == TY_FLOAT) {
        float f;
        memcpy(&f, &bits, 4);
        return to == TY_INT ? (uint32_t)(int32_t)f :
               to == TY_UINT ? (uint32_t)f : (f != 0.0f);
    }
    if (to == TY_BOOL) {
        return bits != 0;
    }
    return bits; /* int <-> uint */
}

/* Whether an id is a constant made in the types section */
static bool id_is_constant(C *c, uint32_t id)
{
    size_t k = 0;
    while (k < c->types.n) {
        uint32_t w = c->types.w[k];
        uint32_t len = w >> 16;
        int opc = w & 0xffff;
        if ((opc == SpvOpConstantComposite || opc == SpvOpConstant ||
             opc == SpvOpConstantTrue || opc == SpvOpConstantFalse) &&
            c->types.w[k + 2] == id) {
            return true;
        }
        k += len;
    }
    return false;
}

static void parse_statement(C *c);
static void parse_block_statement(C *c);

/* One or more variables of a type: `name [= init] {, name [= init]} ;`.
 * Globals outside a function go in Private (or the interface class);
 * locals in Function. */
static void parse_variables(C *c, const Quals *q, Type *base, bool global)
{
    for (;;) {
        const char *name = expect_ident(c);
        Type *t = parse_array_suffix(c, base);
        if (global && q->is_in && c->stage == GLSLC_STAGE_GEOMETRY &&
            t->kind == TY_ARRAY && t->n == 0) {
            /* `in vec4 x[]`: one per input vertex */
            if (!c->geom_in_vertices) {
                fail(c, "input arrays before the input primitive layout");
            }
            t = array_type(c, t->elem, c->geom_in_vertices, 0);
        }
        int sc;
        if (!global) {
            sc = SpvStorageClassFunction;
        } else if (q->is_in) {
            sc = SpvStorageClassInput;
        } else if (q->is_out) {
            sc = SpvStorageClassOutput;
        } else if (q->is_uniform) {
            if (t->kind != TY_SAMPLED_IMAGE && t->kind != TY_IMAGE) {
                fail(c, "a uniform outside a block must be a sampler or image");
            }
            sc = SpvStorageClassUniformConstant;
        } else if (q->is_shared) {
            sc = SpvStorageClassWorkgroup;
        } else {
            sc = SpvStorageClassPrivate;
        }
        LVal lv;
        memset(&lv, 0, sizeof(lv));
        lv.t = t;
        lv.sc = sc;
        lv.readonly = sc == SpvStorageClassInput || sc == SpvStorageClassUniformConstant ||
                      q->is_const;
        lv.precise = q->is_precise;
        if (accept(c, P_ASSIGN)) {
            if (sc != SpvStorageClassFunction && sc != SpvStorageClassPrivate) {
                fail(c, "an interface variable cannot have an initializer");
            }
            bool saved_precise = c->precise_ctx;
            c->precise_ctx = c->precise_ctx || q->is_precise;
            Buf saved_body, saved_vars;
            bool to_ginit = global;
            if (to_ginit) {
                /* a global's initializer runs at the start of main */
                saved_body = c->fbody;
                saved_vars = c->fvars;
                c->fbody = c->global_inits;
                c->fvars = c->ginit_vars;
            }
            Ex init = parse_assignment(c);
            c->precise_ctx = saved_precise;
            if (init.is_const && is_scalar(t) && t->kind != TY_VEC) {
                /* a scalar constant: a constant initializer, and the
                 * symbol folds when const */
                uint32_t bits = const_bits_convert(init.v.t->kind, t->kind, init.cv);
                if (!implicit_ok(init.v.t->kind, t->kind)) {
                    fail(c, "cannot initialize %s with %s", type_name(t),
                         type_name(init.v.t));
                }
                uint32_t cid = const_scalar(c, t, bits);
                lv.ptr = make_variable(c, t, sc, cid);
                Sym *s = sym_add(c, name, &lv);
                if (q->is_const) {
                    s->has_const = true;
                    s->cv = bits;
                }
            } else {
                Val v = rval(c, &init);
                if (v.t != t) {
                    v = coerce(c, v, t);
                }
                if (!init.is_lv && id_is_constant(c, v.id)) {
                    lv.ptr = make_variable(c, t, sc, v.id);
                } else {
                    lv.ptr = make_variable(c, t, sc, 0);
                    ins0(c, SpvOpStore, 2, lv.ptr, v.id);
                }
                sym_add(c, name, &lv);
            }
            if (to_ginit) {
                c->global_inits = c->fbody;
                c->ginit_vars = c->fvars;
                c->fbody = saved_body;
                c->fvars = saved_vars;
            }
        } else {
            if (q->is_const) {
                fail(c, "const %s needs an initializer", name);
            }
            lv.ptr = make_variable(c, t, sc, 0);
            if (global) {
                decorate_interface(c, lv.ptr, q);
                if (sc == SpvStorageClassUniformConstant) {
                    decorate(c, lv.ptr, SpvDecorationDescriptorSet, 1,
                             (uint32_t)(q->has_set ? q->set : 0));
                    if (!q->has_binding) {
                        fail(c, "uniform %s needs a binding", name);
                    }
                    if (t->kind == TY_IMAGE) {
                        if (q->readonly) {
                            decorate(c, lv.ptr, SpvDecorationNonWritable, 0);
                        }
                        if (q->writeonly) {
                            decorate(c, lv.ptr, SpvDecorationNonReadable, 0);
                        }
                    }
                }
            }
            sym_add(c, name, &lv);
        }
        if (global) {
            emit_name(c, lv.ptr, name);
        }
        if (accept(c, P_COMMA)) {
            continue;
        }
        expect(c, P_SEMI);
        return;
    }
}

/* A uniform/buffer block: `Name { members } [instance[N]];` */
static void parse_block(C *c, const Quals *q)
{
    const char *bname = expect_ident(c);
    bool std140 = !(q->std430 || q->is_buffer || q->is_push);
    int sc;
    if (q->is_push) {
        sc = SpvStorageClassPushConstant;
    } else if (q->is_buffer) {
        sc = SpvStorageClassStorageBuffer;
    } else if (q->is_uniform) {
        sc = SpvStorageClassUniform;
    } else if (q->is_in) {
        sc = SpvStorageClassInput;
    } else if (q->is_out) {
        sc = SpvStorageClassOutput;
    } else {
        fail(c, "block %s has no storage qualifier", bname);
    }
    if (sc == SpvStorageClassInput || sc == SpvStorageClassOutput) {
        fail(c, "in/out blocks are not supported");
    }
    Type *st = parse_block_members(c, std140, bname, q->is_push);
    const char *instance = NULL;
    Type *vt = st;
    if (peek(c)->kind == T_IDENT) {
        instance = expect_ident(c);
        vt = parse_array_suffix(c, st);
    }
    expect(c, P_SEMI);
    uint32_t var = make_variable(c, vt, sc, 0);
    if (sc != SpvStorageClassPushConstant) {
        decorate(c, var, SpvDecorationDescriptorSet, 1,
                 (uint32_t)(q->has_set ? q->set : 0));
        if (!q->has_binding) {
            fail(c, "block %s needs a binding", bname);
        }
        decorate(c, var, SpvDecorationBinding, 1, (uint32_t)q->binding);
    }
    if (q->readonly) {
        decorate(c, var, SpvDecorationNonWritable, 0);
    }
    if (q->writeonly) {
        decorate(c, var, SpvDecorationNonReadable, 0);
    }
    emit_name(c, var, instance ? instance : "");
    LVal lv;
    memset(&lv, 0, sizeof(lv));
    lv.t = vt;
    lv.sc = sc;
    lv.ptr = var;
    lv.readonly = sc != SpvStorageClassStorageBuffer || q->readonly;
    if (instance) {
        sym_add(c, instance, &lv);
        return;
    }
    for (int i = 0; i < st->nmembers; i++) {
        Sym *s = sym_add(c, st->member_names[i], &lv);
        s->member = i;
    }
}

/* ---- statements ---- */

static bool starts_declaration(C *c)
{
    const Tok *t = peek(c);
    if (t->kind != T_IDENT) {
        return false;
    }
    if (!strcmp(t->text, "const") || !strcmp(t->text, "precise") ||
        !strcmp(t->text, "highp") || !strcmp(t->text, "mediump") ||
        !strcmp(t->text, "lowp")) {
        return true;
    }
    if (!is_type_name(c, t->text)) {
        return false;
    }
    /* a type name followed by a name, or by [ (array type), is a
     * declaration; followed by ( it is a constructor */
    const Tok *n = peek2(c);
    if (n->kind == T_IDENT) {
        return true;
    }
    return n->kind == T_PUNCT && n->punct == P_LBRACKET;
}

static void push_cf(C *c, uint32_t brk, uint32_t cont, bool is_switch)
{
    if (c->ncf == MAX_FRAMES) {
        fail(c, "control flow nested too deep");
    }
    c->cf[c->ncf].brk = brk;
    c->cf[c->ncf].cont = cont;
    c->cf[c->ncf].is_switch = is_switch;
    c->ncf++;
}

static void pop_cf(C *c)
{
    c->ncf--;
}

static Val parse_condition(C *c)
{
    expect(c, P_LPAREN);
    Val v = parse_rvalue(c);
    expect(c, P_RPAREN);
    if (v.t != c->t_bool) {
        fail(c, "a condition must be a boolean");
    }
    return v;
}

static void parse_if(C *c)
{
    Val cond = parse_condition(c);
    uint32_t bt = new_id(c), merge = new_id(c);
    /* read ahead for else: the false block is only made if there is one */
    uint32_t bf = merge;
    size_t save = c->pos;
    /* skip the then-statement to see whether else follows: too costly to
     * re-parse, so make a false block always and let it be empty */
    (void)save;
    bf = new_id(c);
    ins0(c, SpvOpSelectionMerge, 2, merge, SpvSelectionControlMaskNone);
    branch_cond(c, cond.id, bt, bf);
    start_block(c, bt);
    scope_push(c);
    parse_statement(c);
    scope_pop(c);
    branch(c, merge);
    start_block(c, bf);
    if (accept_ident(c, "else")) {
        scope_push(c);
        parse_statement(c);
        scope_pop(c);
    }
    branch(c, merge);
    start_block(c, merge);
}

static void parse_while(C *c)
{
    uint32_t header = new_id(c), cond_b = new_id(c), body = new_id(c),
             cont = new_id(c), merge = new_id(c);
    branch(c, header);
    start_block(c, header);
    ins0(c, SpvOpLoopMerge, 3, merge, cont, SpvLoopControlMaskNone);
    branch(c, cond_b);
    start_block(c, cond_b);
    Val cond = parse_condition(c);
    branch_cond(c, cond.id, body, merge);
    start_block(c, body);
    push_cf(c, merge, cont, false);
    scope_push(c);
    parse_statement(c);
    scope_pop(c);
    pop_cf(c);
    branch(c, cont);
    start_block(c, cont);
    branch(c, header);
    start_block(c, merge);
}

static void parse_do_while(C *c)
{
    uint32_t header = new_id(c), body = new_id(c), cont = new_id(c),
             merge = new_id(c);
    branch(c, header);
    start_block(c, header);
    ins0(c, SpvOpLoopMerge, 3, merge, cont, SpvLoopControlMaskNone);
    branch(c, body);
    start_block(c, body);
    push_cf(c, merge, cont, false);
    scope_push(c);
    parse_statement(c);
    scope_pop(c);
    pop_cf(c);
    branch(c, cont);
    start_block(c, cont);
    if (!accept_ident(c, "while")) {
        fail(c, "expected 'while' after the do body");
    }
    Val cond = parse_condition(c);
    expect(c, P_SEMI);
    branch_cond(c, cond.id, header, merge);
    start_block(c, merge);
}

static void parse_for(C *c)
{
    expect(c, P_LPAREN);
    scope_push(c);
    /* init */
    if (!accept(c, P_SEMI)) {
        if (starts_declaration(c)) {
            Quals q;
            parse_qualifiers(c, &q);
            Type *t = parse_type(c, &q);
            parse_variables(c, &q, t, false);
        } else {
            parse_expr(c);
            expect(c, P_SEMI);
        }
    }
    uint32_t header = new_id(c), cond_b = new_id(c), body = new_id(c),
             cont = new_id(c), merge = new_id(c);
    branch(c, header);
    start_block(c, header);
    ins0(c, SpvOpLoopMerge, 3, merge, cont, SpvLoopControlMaskNone);
    branch(c, cond_b);
    start_block(c, cond_b);
    if (accept(c, P_SEMI)) {
        branch(c, body);
    } else {
        Val cond = parse_rvalue(c);
        expect(c, P_SEMI);
        if (cond.t != c->t_bool) {
            fail(c, "a for condition must be a boolean");
        }
        branch_cond(c, cond.id, body, merge);
    }
    /* the increment is emitted after the body: remember where it is */
    size_t incr_pos = c->pos;
    if (!accept(c, P_RPAREN)) {
        /* skip to the closing paren */
        int depth = 0;
        for (;;) {
            const Tok *t = next(c);
            if (t->kind == T_EOF) {
                fail(c, "unexpected end of the shader");
            }
            if (t->kind == T_PUNCT) {
                if (t->punct == P_LPAREN) {
                    depth++;
                } else if (t->punct == P_RPAREN) {
                    if (!depth) {
                        break;
                    }
                    depth--;
                }
            }
        }
    }
    size_t body_pos = c->pos;
    start_block(c, body);
    push_cf(c, merge, cont, false);
    scope_push(c);
    parse_statement(c);
    scope_pop(c);
    pop_cf(c);
    branch(c, cont);
    start_block(c, cont);
    size_t after_body = c->pos;
    if (incr_pos != body_pos - 1 || !(c->toks.t[incr_pos].kind == T_PUNCT &&
                                      c->toks.t[incr_pos].punct == P_RPAREN)) {
        c->pos = incr_pos;
        parse_expr(c);
        expect(c, P_RPAREN);
        c->pos = after_body;
    }
    branch(c, header);
    start_block(c, merge);
    scope_pop(c);
}

static void parse_switch(C *c)
{
    expect(c, P_LPAREN);
    Val sel = parse_rvalue(c);
    expect(c, P_RPAREN);
    if (sel.t != c->t_int && sel.t != c->t_uint) {
        fail(c, "switch needs an integer");
    }
    expect(c, P_LBRACE);
    /* the labels, read ahead */
    uint32_t labels[64], values[64], default_label = 0;
    int nlabels = 0;
    size_t body_start = c->pos;
    {
        int depth = 0;
        for (;;) {
            const Tok *t = peek(c);
            if (t->kind == T_EOF) {
                fail(c, "unexpected end of the shader");
            }
            if (t->kind == T_PUNCT && t->punct == P_LBRACE) {
                depth++;
                next(c);
                continue;
            }
            if (t->kind == T_PUNCT && t->punct == P_RBRACE) {
                if (!depth) {
                    break;
                }
                depth--;
                next(c);
                continue;
            }
            if (depth == 0 && t->kind == T_IDENT && !strcmp(t->text, "case")) {
                next(c);
                if (nlabels == 64) {
                    fail(c, "too many case labels");
                }
                values[nlabels] = (uint32_t)parse_const_int(c);
                labels[nlabels] = new_id(c);
                nlabels++;
                expect(c, P_COLON);
                continue;
            }
            if (depth == 0 && t->kind == T_IDENT && !strcmp(t->text, "default")) {
                next(c);
                expect(c, P_COLON);
                if (default_label) {
                    fail(c, "two default labels");
                }
                default_label = new_id(c);
                continue;
            }
            next(c);
        }
    }
    c->pos = body_start;
    uint32_t merge = new_id(c);
    uint32_t def = default_label ? default_label : merge;
    ins0(c, SpvOpSelectionMerge, 2, merge, SpvSelectionControlMaskNone);
    op(&c->fbody, SpvOpSwitch, 2 + 2 * nlabels);
    buf_put(&c->fbody, sel.id);
    buf_put(&c->fbody, def);
    for (int i = 0; i < nlabels; i++) {
        buf_put(&c->fbody, values[i]);
        buf_put(&c->fbody, labels[i]);
    }
    c->terminated = true;
    push_cf(c, merge, c->ncf ? c->cf[c->ncf - 1].cont : 0, true);
    scope_push(c);
    int li = 0;
    bool any_label = false;
    while (!accept(c, P_RBRACE)) {
        if (accept_ident(c, "case")) {
            parse_const_int(c);
            expect(c, P_COLON);
            uint32_t l = labels[li++];
            branch(c, l); /* fall through from the case above */
            start_block(c, l);
            any_label = true;
            continue;
        }
        if (accept_ident(c, "default")) {
            expect(c, P_COLON);
            branch(c, default_label);
            start_block(c, default_label);
            any_label = true;
            continue;
        }
        if (!any_label) {
            fail(c, "statements before the first case label");
        }
        parse_statement(c);
    }
    scope_pop(c);
    pop_cf(c);
    branch(c, merge);
    start_block(c, merge);
}

static void parse_statement(C *c)
{
    const Tok *t = peek(c);
    ensure_block(c);
    if (t->kind == T_PUNCT && t->punct == P_LBRACE) {
        parse_block_statement(c);
        return;
    }
    if (t->kind == T_PUNCT && t->punct == P_SEMI) {
        next(c);
        return;
    }
    if (t->kind == T_IDENT) {
        if (!strcmp(t->text, "if")) {
            next(c);
            parse_if(c);
            return;
        }
        if (!strcmp(t->text, "for")) {
            next(c);
            parse_for(c);
            return;
        }
        if (!strcmp(t->text, "while")) {
            next(c);
            parse_while(c);
            return;
        }
        if (!strcmp(t->text, "do")) {
            next(c);
            parse_do_while(c);
            return;
        }
        if (!strcmp(t->text, "switch")) {
            next(c);
            parse_switch(c);
            return;
        }
        if (!strcmp(t->text, "break")) {
            next(c);
            expect(c, P_SEMI);
            if (!c->ncf || !c->cf[c->ncf - 1].brk) {
                fail(c, "break outside a loop or switch");
            }
            branch(c, c->cf[c->ncf - 1].brk);
            return;
        }
        if (!strcmp(t->text, "continue")) {
            next(c);
            expect(c, P_SEMI);
            uint32_t target = 0;
            for (int i = c->ncf; i > 0; i--) {
                if (!c->cf[i - 1].is_switch) {
                    target = c->cf[i - 1].cont;
                    break;
                }
            }
            if (!target) {
                fail(c, "continue outside a loop");
            }
            branch(c, target);
            return;
        }
        if (!strcmp(t->text, "return")) {
            next(c);
            if (accept(c, P_SEMI)) {
                if (c->cur_fn->ret != c->t_void) {
                    fail(c, "return without a value");
                }
                ins0(c, SpvOpReturn, 0);
                c->terminated = true;
                return;
            }
            Val v = parse_rvalue(c);
            expect(c, P_SEMI);
            if (c->cur_fn->ret == c->t_void) {
                fail(c, "return with a value in a void function");
            }
            v = coerce(c, v, c->cur_fn->ret);
            ins0(c, SpvOpReturnValue, 1, v.id);
            c->terminated = true;
            return;
        }
        if (!strcmp(t->text, "discard")) {
            next(c);
            expect(c, P_SEMI);
            if (c->stage != GLSLC_STAGE_FRAGMENT) {
                fail(c, "discard outside a fragment shader");
            }
            if (c->opts.spirv_1_6) {
                ins0(c, SpvOpDemoteToHelperInvocation, 0);
                c->cap_demote = true;
            } else {
                ins0(c, SpvOpKill, 0);
                c->terminated = true;
            }
            return;
        }
        if (starts_declaration(c)) {
            Quals q;
            parse_qualifiers(c, &q);
            Type *ty = parse_type(c, &q);
            parse_variables(c, &q, ty, false);
            return;
        }
    }
    parse_expr(c);
    expect(c, P_SEMI);
}

static void parse_block_statement(C *c)
{
    expect(c, P_LBRACE);
    scope_push(c);
    while (!accept(c, P_RBRACE)) {
        if (peek(c)->kind == T_EOF) {
            fail(c, "unexpected end of the shader");
        }
        parse_statement(c);
    }
    scope_pop(c);
}

/* ---- functions ---- */

/* An out or inout parameter is a pointer into the caller's storage: the
 * function writes through it. */
static uint32_t param_type_id(C *c, Type *t, uint8_t mode)
{
    return mode == PM_IN ? t->id : ptr_type(c, t, SpvStorageClassFunction);
}

static uint32_t function_type(C *c, Type *ret, Type **params,
                              const uint8_t *pmode, int n)
{
    uint32_t pids[MAX_ARGS];
    for (int k = 0; k < n; k++) {
        pids[k] = param_type_id(c, params[k], pmode[k]);
    }
    /* dedupe by words */
    size_t i = 0;
    while (i < c->types.n) {
        uint32_t w = c->types.w[i];
        uint32_t len = w >> 16;
        if ((w & 0xffff) == SpvOpTypeFunction && len == (uint32_t)(3 + n) &&
            c->types.w[i + 2] == ret->id) {
            bool same = true;
            for (int k = 0; k < n; k++) {
                if (c->types.w[i + 3 + k] != pids[k]) {
                    same = false;
                }
            }
            if (same) {
                return c->types.w[i + 1];
            }
        }
        i += len;
    }
    uint32_t id = new_id(c);
    op(&c->types, SpvOpTypeFunction, 2 + n);
    buf_put(&c->types, id);
    buf_put(&c->types, ret->id);
    for (int k = 0; k < n; k++) {
        buf_put(&c->types, pids[k]);
    }
    return id;
}

static void parse_function(C *c, Type *ret, const char *name)
{
    Type *params[MAX_ARGS];
    const char *pnames[MAX_ARGS];
    bool pprecise[MAX_ARGS];
    uint8_t pmode[MAX_ARGS];
    int n = 0;
    expect(c, P_LPAREN);
    if (!accept(c, P_RPAREN)) {
        for (;;) {
            Quals q;
            parse_qualifiers(c, &q);
            if (n == MAX_ARGS) {
                fail(c, "too many parameters");
            }
            pmode[n] = !q.is_out ? PM_IN : q.is_in ? PM_INOUT : PM_OUT;
            params[n] = parse_type(c, &q);
            pnames[n] = peek(c)->kind == T_IDENT ? expect_ident(c) : NULL;
            params[n] = parse_array_suffix(c, params[n]);
            pprecise[n] = q.is_precise;
            n++;
            if (accept(c, P_COMMA)) {
                continue;
            }
            expect(c, P_RPAREN);
            break;
        }
    }
    Func *f = func_find_exact(c, name, params, n);
    if (f) {
        if (f->ret != ret) {
            fail(c, "%s() declared with another return type before", name);
        }
        if (memcmp(f->pmode, pmode, n) != 0) {
            fail(c, "%s() declared with other parameter qualifiers before",
                 name);
        }
    } else {
        if (c->nfns == c->fns_cap) {
            c->fns_cap = c->fns_cap ? c->fns_cap * 2 : 64;
            c->fns = realloc(c->fns, c->fns_cap * sizeof(Func));
            if (!c->fns) {
                abort();
            }
        }
        f = &c->fns[c->nfns++];
        f->name = name;
        f->ret = ret;
        f->nparams = n;
        f->params = arena_alloc(&c->arena, (n ? n : 1) * sizeof(Type *));
        memcpy(f->params, params, n * sizeof(Type *));
        f->pmode = arena_alloc(&c->arena, n ? n : 1);
        memcpy(f->pmode, pmode, n);
        f->id = new_id(c);
        f->type_id = function_type(c, ret, params, pmode, n);
        emit_name(c, f->id, name);
    }
    if (accept(c, P_SEMI)) {
        return; /* a prototype */
    }
    if (c->in_function) {
        fail(c, "a function inside a function");
    }
    bool is_main = !strcmp(name, "main");
    if (is_main) {
        if (ret != c->t_void || n != 0) {
            fail(c, "main() must be void with no parameters");
        }
        if (c->have_main) {
            fail(c, "main() defined twice");
        }
        c->have_main = true;
        c->main_id = f->id;
    }
    c->in_function = true;
    c->cur_fn = f;
    memset(&c->fvars, 0, sizeof(c->fvars));
    memset(&c->fbody, 0, sizeof(c->fbody));
    c->ncf = 0;
    /* OpFunction, parameters */
    Buf head;
    memset(&head, 0, sizeof(head));
    op(&head, SpvOpFunction, 4);
    buf_put(&head, ret->id);
    buf_put(&head, f->id);
    buf_put(&head, SpvFunctionControlMaskNone);
    buf_put(&head, f->type_id);
    uint32_t param_ids[MAX_ARGS];
    for (int i = 0; i < n; i++) {
        param_ids[i] = new_id(c);
        op(&head, SpvOpFunctionParameter, 2);
        buf_put(&head, param_type_id(c, params[i], pmode[i]));
        buf_put(&head, param_ids[i]);
    }
    uint32_t entry = new_id(c);
    c->cur_block = entry;
    c->terminated = false;
    scope_push(c);
    /* in parameters live in local variables, so the body can assign them;
     * out and inout ones are the caller's storage, written through */
    for (int i = 0; i < n; i++) {
        LVal lv;
        memset(&lv, 0, sizeof(lv));
        lv.t = params[i];
        lv.sc = SpvStorageClassFunction;
        lv.precise = pprecise[i];
        if (pmode[i] == PM_IN) {
            lv.ptr = make_variable(c, params[i], SpvStorageClassFunction, 0);
            ins0(c, SpvOpStore, 2, lv.ptr, param_ids[i]);
        } else {
            lv.ptr = param_ids[i];
        }
        if (pnames[i]) {
            sym_add(c, pnames[i], &lv);
        }
    }
    if (is_main) {
        /* the globals' initializers first */
        buf_append_take(&c->fvars, &c->ginit_vars);
        buf_append_take(&c->fbody, &c->global_inits);
    }
    parse_block_statement(c);
    scope_pop(c);
    if (!c->terminated) {
        if (ret == c->t_void) {
            ins0(c, SpvOpReturn, 0);
        } else {
            ins0(c, SpvOpUnreachable, 0);
        }
        c->terminated = true;
    }
    buf_append_take(&c->funcs, &head);
    op(&c->funcs, SpvOpLabel, 1);
    buf_put(&c->funcs, entry);
    buf_append_take(&c->funcs, &c->fvars);
    buf_append_take(&c->funcs, &c->fbody);
    op(&c->funcs, SpvOpFunctionEnd, 0);
    c->in_function = false;
    c->cur_fn = NULL;
}

/* ---- the top level ---- */

static void parse_toplevel(C *c)
{
    while (peek(c)->kind != T_EOF) {
        if (accept(c, P_SEMI)) {
            continue;
        }
        Quals q;
        parse_qualifiers(c, &q);
        /* `layout(...) in;` / `out;`: a stage layout */
        if ((q.is_in || q.is_out) && is_punct(c, P_SEMI)) {
            next(c);
            if (q.geom_mode) {
                int mode = q.geom_mode;
                if (q.geom_points) {
                    mode = q.is_in ? SpvExecutionModeInputPoints :
                                     SpvExecutionModeOutputPoints;
                }
                op(&c->exec_modes, SpvOpExecutionMode, 2);
                buf_put(&c->exec_modes, 0);
                buf_put(&c->exec_modes, mode);
                if (q.is_in) {
                    c->geom_in_set = true;
                } else {
                    c->geom_out_set = true;
                }
            }
            continue;
        }
        const Tok *t = peek(c);
        if (t->kind != T_IDENT) {
            fail(c, "expected a declaration");
        }
        /* a block: `uniform Name {` */
        if ((q.is_uniform || q.is_buffer || q.is_in || q.is_out) &&
            !is_type_name(c, t->text) && peek2(c)->kind == T_PUNCT &&
            peek2(c)->punct == P_LBRACE) {
            parse_block(c, &q);
            continue;
        }
        Type *ty = parse_type(c, &q);
        const Tok *nt = peek(c);
        if (nt->kind != T_IDENT) {
            fail(c, "expected a name");
        }
        if (peek2(c)->kind == T_PUNCT && peek2(c)->punct == P_LPAREN) {
            const char *name = expect_ident(c);
            if (q.is_in || q.is_out || q.is_uniform || q.is_buffer) {
                fail(c, "a function with storage qualifiers");
            }
            parse_function(c, ty, name);
            continue;
        }
        parse_variables(c, &q, ty, true);
    }
}

/* ---- the module ---- */

static void init_types(C *c)
{
    c->t_void = type_new(c, TY_VOID);
    c->t_void->id = new_id(c);
    op(&c->types, SpvOpTypeVoid, 1);
    buf_put(&c->types, c->t_void->id);
    c->t_bool = type_new(c, TY_BOOL);
    c->t_bool->id = new_id(c);
    op(&c->types, SpvOpTypeBool, 1);
    buf_put(&c->types, c->t_bool->id);
    c->t_int = type_new(c, TY_INT);
    c->t_int->id = new_id(c);
    op(&c->types, SpvOpTypeInt, 3);
    buf_put(&c->types, c->t_int->id);
    buf_put(&c->types, 32);
    buf_put(&c->types, 1);
    c->t_uint = type_new(c, TY_UINT);
    c->t_uint->id = new_id(c);
    op(&c->types, SpvOpTypeInt, 3);
    buf_put(&c->types, c->t_uint->id);
    buf_put(&c->types, 32);
    buf_put(&c->types, 0);
    c->t_float = type_new(c, TY_FLOAT);
    c->t_float->id = new_id(c);
    op(&c->types, SpvOpTypeFloat, 2);
    buf_put(&c->types, c->t_float->id);
    buf_put(&c->types, 32);
}

/* Count the module header, then write it directly into its final location. */
typedef struct Header {
    uint32_t *words;
    size_t n, capacity;
} Header;

static void header_put(Header *h, uint32_t word)
{
    if (h->n == SIZE_MAX / sizeof(uint32_t)) {
        abort();
    }
    if (h->words) {
        if (h->n >= h->capacity) {
            abort();
        }
        h->words[h->n] = word;
    }
    h->n++;
}

static void header_op(Header *h, SpvOp opcode, unsigned operands)
{
    header_put(h, ((operands + 1) << 16) | opcode);
}

static void header_string(Header *h, const char *string)
{
    size_t length = strlen(string) + 1;
    for (size_t i = 0; i < length; i += 4) {
        uint32_t word = 0;
        for (unsigned j = 0; j < 4 && i + j < length; j++) {
            word |= (uint32_t)(uint8_t)string[i + j] << (j * 8);
        }
        header_put(h, word);
    }
}

static void header_append(Header *h, const Buf *source)
{
    if (source->n > SIZE_MAX / sizeof(uint32_t) - h->n) {
        abort();
    }
    if (h->words && source->n) {
        if (source->n > h->capacity - h->n) {
            abort();
        }
        memcpy(h->words + h->n, source->w, source->n * sizeof(uint32_t));
    }
    h->n += source->n;
}

static void emit_header(C *c, Header *m)
{
    if (!c->have_main) {
        fail(c, "no main()");
    }
    header_put(m, SpvMagicNumber);
    header_put(m, c->opts.spirv_1_6 ? 0x00010600 : 0x00010500);
    header_put(m, 0);
    header_put(m, c->next_id);
    header_put(m, 0);
    /* capabilities */
    header_op(m, SpvOpCapability, 1);
    header_put(m, SpvCapabilityShader);
    if (c->stage == GLSLC_STAGE_GEOMETRY) {
        header_op(m, SpvOpCapability, 1);
        header_put(m, SpvCapabilityGeometry);
    }
    if (c->cap_image_query) {
        header_op(m, SpvOpCapability, 1);
        header_put(m, SpvCapabilityImageQuery);
    }
    if (c->cap_demote) {
        header_op(m, SpvOpCapability, 1);
        header_put(m, SpvCapabilityDemoteToHelperInvocation);
    }
    if (c->cap_image_read_without_format) {
        header_op(m, SpvOpCapability, 1);
        header_put(m, SpvCapabilityStorageImageReadWithoutFormat);
    }
    if (c->cap_image_write_without_format) {
        header_op(m, SpvOpCapability, 1);
        header_put(m, SpvCapabilityStorageImageWriteWithoutFormat);
    }
    /* the extended instruction set */
    header_op(m, SpvOpExtInstImport, 1 + string_words("GLSL.std.450"));
    header_put(m, c->glsl_ext_id);
    header_string(m, "GLSL.std.450");
    header_op(m, SpvOpMemoryModel, 2);
    header_put(m, SpvAddressingModelLogical);
    header_put(m, SpvMemoryModelGLSL450);
    /* entry point */
    {
        int model = c->stage == GLSLC_STAGE_VERTEX ? SpvExecutionModelVertex :
                    c->stage == GLSLC_STAGE_GEOMETRY ? SpvExecutionModelGeometry :
                    c->stage == GLSLC_STAGE_FRAGMENT ? SpvExecutionModelFragment :
                                                       SpvExecutionModelGLCompute;
        header_op(m, SpvOpEntryPoint, 2 + string_words("main") + (int)c->entry_iface.n);
        header_put(m, model);
        header_put(m, c->main_id);
        header_string(m, "main");
        header_append(m, &c->entry_iface);
    }
    /* execution modes */
    switch (c->stage) {
    case GLSLC_STAGE_FRAGMENT:
        header_op(m, SpvOpExecutionMode, 2);
        header_put(m, c->main_id);
        header_put(m, SpvExecutionModeOriginUpperLeft);
        if (c->depth_replacing) {
            header_op(m, SpvOpExecutionMode, 2);
            header_put(m, c->main_id);
            header_put(m, SpvExecutionModeDepthReplacing);
        }
        break;
    case GLSLC_STAGE_COMPUTE:
        if (!c->have_local_size) {
            fail(c, "a compute shader needs layout(local_size_x = ...)");
        }
        header_op(m, SpvOpExecutionMode, 5);
        header_put(m, c->main_id);
        header_put(m, SpvExecutionModeLocalSize);
        header_put(m, c->local_size[0] ? c->local_size[0] : 1);
        header_put(m, c->local_size[1] ? c->local_size[1] : 1);
        header_put(m, c->local_size[2] ? c->local_size[2] : 1);
        break;
    case GLSLC_STAGE_GEOMETRY:
        if (!c->geom_in_set || !c->geom_out_set) {
            fail(c, "a geometry shader needs its input and output layouts");
        }
        {
            /* Invocations 1 unless given */
            bool have_inv = false;
            size_t i = 0;
            while (i < c->exec_modes.n) {
                uint32_t len = c->exec_modes.w[i] >> 16;
                if (c->exec_modes.w[i + 2] == SpvExecutionModeInvocations) {
                    have_inv = true;
                }
                i += len;
            }
            if (!have_inv) {
                header_op(m, SpvOpExecutionMode, 3);
                header_put(m, c->main_id);
                header_put(m, SpvExecutionModeInvocations);
                header_put(m, 1);
            }
        }
        break;
    default:
        break;
    }
    header_append(m, &c->exec_modes);
}

static void assemble(C *c, GlslcResult *result)
{
    /* Fill mode placeholders before either header pass. */
    for (size_t i = 0; i < c->exec_modes.n;) {
        uint32_t length = c->exec_modes.w[i] >> 16;
        c->exec_modes.w[i + 1] = c->main_id;
        i += length;
    }
    Header count = {0};
    emit_header(c, &count);
    const Buf *sections[] = { &c->names, &c->decor, &c->types };
    size_t prefix = count.n;
    const size_t limit = SIZE_MAX / sizeof(uint32_t);
    for (unsigned i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
        if (sections[i]->n > limit - prefix) {
            abort();
        }
        prefix += sections[i]->n;
    }
    if (c->funcs.n > limit - prefix) {
        abort();
    }
    size_t total = prefix + c->funcs.n;
    /* The function stream becomes the result allocation. Reserve once at
     * its known final size; no separate assembled-module buffer is made. */
    if (c->funcs.cap < total) {
        uint32_t *words = realloc(c->funcs.w, total * sizeof(uint32_t));
        if (!words) {
            abort();
        }
        c->funcs.w = words;
        c->funcs.cap = total;
    }
    memmove(c->funcs.w + prefix, c->funcs.w,
            c->funcs.n * sizeof(uint32_t));
    Header output = { .words = c->funcs.w, .capacity = count.n };
    emit_header(c, &output);
    if (output.n != count.n) {
        abort();
    }
    size_t at = output.n;
    for (unsigned i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
        const Buf *source = sections[i];
        if (source->n) {
            memcpy(c->funcs.w + at, source->w, source->n * sizeof(uint32_t));
            at += source->n;
        }
    }
    result->words = c->funcs.w;
    result->num_words = total;
    memset(&c->funcs, 0, sizeof(c->funcs)); /* ownership moved to caller */
}

static void ctx_free(C *c)
{
    free(c->caps.w);
    free(c->exts.w);
    free(c->entry_iface.w);
    free(c->exec_modes.w);
    free(c->names.w);
    free(c->decor.w);
    free(c->types.w);
    free(c->funcs.w);
    free(c->fvars.w);
    free(c->fbody.w);
    free(c->global_inits.w);
    free(c->ginit_vars.w);
    free(c->type_list);
    free(c->consts);
    free(c->syms);
    free(c->fns);
    arena_free_all(&c->arena);
}

bool glslc_compile(GlslcStage stage, const char *source,
                   const GlslcOptions *opts, GlslcResult *result)
{
    C *c = calloc(1, sizeof(*c));
    if (!c) {
        abort();
    }
    memset(result, 0, sizeof(*result));
    c->error = result->error;
    c->error_cap = sizeof(result->error);
    c->error_line = &result->error_line;
    c->stage = stage;
    if (opts) {
        c->opts = *opts;
    }
    c->next_id = 1;
    if (setjmp(c->err)) {
        free(result->words);
        result->words = NULL;
        result->num_words = 0;
        ctx_free(c);
        free(c);
        return false;
    }
    glslc_lex_and_preprocess(&c->arena, source, &c->toks, &c->err, c->error,
                             c->error_cap, c->error_line);
    c->glsl_ext_id = new_id(c);
    init_types(c);
    parse_toplevel(c);
    assemble(c, result);
    ctx_free(c);
    free(c);
    return true;
}
