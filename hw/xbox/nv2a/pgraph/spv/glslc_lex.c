/*
 * GLSL to SPIR-V: lexer and preprocessor
 *
 * The preprocessor does what the generators use: comments, #version
 * (ignored, the dialect is 4.50), #define of object-like and function-like
 * macros with rescanning, #undef. Anything else is an error.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "glslc_int.h"

/* ---- arena ---- */

void *arena_alloc(Arena *a, size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (n > a->left) {
        size_t sz = n > 65536 ? n : 65536;
        char *b = malloc(sz);
        if (!b) {
            abort();
        }
        if (a->nblocks == a->cap) {
            a->cap = a->cap ? a->cap * 2 : 16;
            a->blocks = realloc(a->blocks, a->cap * sizeof(*a->blocks));
            if (!a->blocks) {
                abort();
            }
        }
        a->blocks[a->nblocks++] = b;
        a->cur = b;
        a->left = sz;
    }
    void *p = a->cur;
    a->cur += n;
    a->left -= n;
    return p;
}

const char *arena_strndup(Arena *a, const char *s, size_t n)
{
    char *d = arena_alloc(a, n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

void arena_free_all(Arena *a)
{
    for (size_t i = 0; i < a->nblocks; i++) {
        free(a->blocks[i]);
    }
    free(a->blocks);
    memset(a, 0, sizeof(*a));
}

/* ---- errors ---- */

typedef struct Lexer {
    Arena *a;
    jmp_buf *err;
    char *error;
    size_t error_cap;
    int *error_line;
} Lexer;

static void lex_fail(Lexer *lx, int line, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

static void lex_fail(Lexer *lx, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lx->error, lx->error_cap, fmt, ap);
    va_end(ap);
    *lx->error_line = line;
    longjmp(*lx->err, 1);
}

/* ---- token lists ---- */

static void tl_push(Arena *a, TokList *l, const Tok *t)
{
    if (l->n == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 256;
        Tok *nt = arena_alloc(a, ncap * sizeof(*nt));
        if (l->n) {
            memcpy(nt, l->t, l->n * sizeof(*nt));
        }
        l->t = nt;
        l->cap = ncap;
    }
    l->t[l->n++] = *t;
}

/* ---- identifiers, interned so names compare by pointer ---- */

#define INTERN_SLOTS 2048 /* a power of two; shaders have a few hundred names */

typedef struct Intern {
    const char *names[INTERN_SLOTS];
    uint32_t hashes[INTERN_SLOTS];
    size_t n;
} Intern;

static uint32_t hash_bytes(const char *s, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ (unsigned char)s[i]) * 16777619u;
    }
    return h ? h : 1;
}

static const char *intern(Lexer *lx, Intern *in, const char *s, size_t len)
{
    uint32_t h = hash_bytes(s, len);
    size_t i = h & (INTERN_SLOTS - 1);
    for (;;) {
        if (!in->names[i]) {
            break;
        }
        if (in->hashes[i] == h && strlen(in->names[i]) == len &&
            memcmp(in->names[i], s, len) == 0) {
            return in->names[i];
        }
        i = (i + 1) & (INTERN_SLOTS - 1);
    }
    if (in->n >= INTERN_SLOTS / 2) {
        lex_fail(lx, 0, "too many distinct names");
    }
    in->names[i] = arena_strndup(lx->a, s, len);
    in->hashes[i] = h;
    in->n++;
    return in->names[i];
}

/* ---- the lexer ---- */

/* The operator at p and its length, or 0 */
static int match_punct(const char *p, int *len)
{
    char a = p[0], b = p[1], d = p[2];
    *len = 1;
    switch (a) {
    case '(': return P_LPAREN;
    case ')': return P_RPAREN;
    case '[': return P_LBRACKET;
    case ']': return P_RBRACKET;
    case '{': return P_LBRACE;
    case '}': return P_RBRACE;
    case '.': return P_DOT;
    case ',': return P_COMMA;
    case ';': return P_SEMI;
    case '?': return P_QUESTION;
    case ':': return P_COLON;
    case '~': return P_TILDE;
    case '+':
        if (b == '+') { *len = 2; return P_INC; }
        if (b == '=') { *len = 2; return P_ADD_ASSIGN; }
        return P_PLUS;
    case '-':
        if (b == '-') { *len = 2; return P_DEC; }
        if (b == '=') { *len = 2; return P_SUB_ASSIGN; }
        return P_MINUS;
    case '*':
        if (b == '=') { *len = 2; return P_MUL_ASSIGN; }
        return P_STAR;
    case '/':
        if (b == '=') { *len = 2; return P_DIV_ASSIGN; }
        return P_SLASH;
    case '%':
        if (b == '=') { *len = 2; return P_MOD_ASSIGN; }
        return P_PERCENT;
    case '<':
        if (b == '<') {
            if (d == '=') { *len = 3; return P_SHL_ASSIGN; }
            *len = 2;
            return P_SHL;
        }
        if (b == '=') { *len = 2; return P_LE; }
        return P_LT;
    case '>':
        if (b == '>') {
            if (d == '=') { *len = 3; return P_SHR_ASSIGN; }
            *len = 2;
            return P_SHR;
        }
        if (b == '=') { *len = 2; return P_GE; }
        return P_GT;
    case '=':
        if (b == '=') { *len = 2; return P_EQ; }
        return P_ASSIGN;
    case '!':
        if (b == '=') { *len = 2; return P_NE; }
        return P_NOT;
    case '&':
        if (b == '&') { *len = 2; return P_ANDAND; }
        if (b == '=') { *len = 2; return P_AND_ASSIGN; }
        return P_AND;
    case '|':
        if (b == '|') { *len = 2; return P_OROR; }
        if (b == '=') { *len = 2; return P_OR_ASSIGN; }
        return P_OR;
    case '^':
        if (b == '^') { *len = 2; return P_XORXOR; }
        if (b == '=') { *len = 2; return P_XOR_ASSIGN; }
        return P_XOR;
    default:
        return 0;
    }
}

static void tokenize(Lexer *lx, Intern *in, const char *src, TokList *out)
{
    const char *p = src;
    int line = 1;
    bool space = false;
    bool line_start = true;

    for (;;) {
        Tok t;
        memset(&t, 0, sizeof(t));
        /* whitespace and comments */
        for (;;) {
            if (*p == '\n') {
                t.kind = T_NEWLINE;
                t.line = line;
                tl_push(lx->a, out, &t);
                p++;
                line++;
                line_start = true;
                space = true;
                continue;
            }
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' ||
                *p == '\v') {
                p++;
                space = true;
                continue;
            }
            if (p[0] == '/' && p[1] == '/') {
                while (*p && *p != '\n') {
                    p++;
                }
                space = true;
                continue;
            }
            if (p[0] == '/' && p[1] == '*') {
                p += 2;
                while (*p && !(p[0] == '*' && p[1] == '/')) {
                    if (*p == '\n') {
                        line++;
                    }
                    p++;
                }
                if (!*p) {
                    lex_fail(lx, line, "unterminated comment");
                }
                p += 2;
                space = true;
                continue;
            }
            if (*p == '\\' && p[1] == '\n') { /* line continuation */
                p += 2;
                line++;
                space = true;
                continue;
            }
            break;
        }
        t.line = line;
        t.space_before = space;
        space = false;
        if (!*p) {
            t.kind = T_EOF;
            tl_push(lx->a, out, &t);
            return;
        }
        if (*p == '#') {
            if (!line_start) {
                lex_fail(lx, line, "'#' not at the start of a line");
            }
            t.kind = T_HASH;
            tl_push(lx->a, out, &t);
            p++;
            continue;
        }
        line_start = false;
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p == '_') {
                p++;
            }
            t.kind = T_IDENT;
            t.text = intern(lx, in, s, p - s);
            tl_push(lx->a, out, &t);
            continue;
        }
        if (isdigit((unsigned char)*p) ||
            (*p == '.' && isdigit((unsigned char)p[1]))) {
            const char *s = p;
            bool is_float = false;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                while (isxdigit((unsigned char)*p)) {
                    p++;
                }
            } else {
                while (isdigit((unsigned char)*p)) {
                    p++;
                }
                if (*p == '.') {
                    is_float = true;
                    p++;
                    while (isdigit((unsigned char)*p)) {
                        p++;
                    }
                }
                if (*p == 'e' || *p == 'E') {
                    is_float = true;
                    p++;
                    if (*p == '+' || *p == '-') {
                        p++;
                    }
                    while (isdigit((unsigned char)*p)) {
                        p++;
                    }
                }
            }
            if (is_float) {
                t.kind = T_FLOAT;
                t.val.f = strtof(s, NULL);
                if (*p == 'f' || *p == 'F') {
                    p++;
                } else if ((p[0] == 'l' || p[0] == 'L') &&
                           (p[1] == 'f' || p[1] == 'F')) {
                    lex_fail(lx, line, "double literals are not supported");
                }
            } else {
                unsigned long long v = strtoull(s, NULL, 0);
                if (v > 0xffffffffull) {
                    lex_fail(lx, line, "integer literal does not fit 32 bits");
                }
                t.val.u = (uint32_t)v;
                if (*p == 'u' || *p == 'U') {
                    t.kind = T_UINT;
                    p++;
                } else {
                    t.kind = T_INT;
                }
            }
            if (isalnum((unsigned char)*p) || *p == '_') {
                lex_fail(lx, line, "bad number suffix");
            }
            tl_push(lx->a, out, &t);
            continue;
        }
        {
            int l;
            int pc = match_punct(p, &l);
            if (!pc) {
                lex_fail(lx, line, "unexpected character '%c'", *p);
            }
            t.kind = T_PUNCT;
            t.punct = pc;
            tl_push(lx->a, out, &t);
            p += l;
        }
    }
}

/* ---- the preprocessor ---- */

typedef struct Macro {
    const char *name;
    bool function_like;
    int nparams;
    const char **params;
    TokList body;
} Macro;

typedef struct Macros {
    Macro *m;
    size_t n, cap;
} Macros;

static Macro *macro_find(Macros *ms, const char *name)
{
    for (size_t i = 0; i < ms->n; i++) {
        if (ms->m[i].name == name) {
            return &ms->m[i];
        }
    }
    return NULL;
}

static void macro_remove(Macros *ms, const char *name)
{
    for (size_t i = 0; i < ms->n; i++) {
        if (ms->m[i].name == name) {
            ms->m[i] = ms->m[ms->n - 1];
            ms->n--;
            return;
        }
    }
}

static Macro *macro_add(Arena *a, Macros *ms, const char *name)
{
    macro_remove(ms, name);
    if (ms->n == ms->cap) {
        size_t ncap = ms->cap ? ms->cap * 2 : 64;
        Macro *nm = arena_alloc(a, ncap * sizeof(*nm));
        if (ms->n) {
            memcpy(nm, ms->m, ms->n * sizeof(*nm));
        }
        ms->m = nm;
        ms->cap = ncap;
    }
    Macro *m = &ms->m[ms->n++];
    memset(m, 0, sizeof(*m));
    m->name = name;
    return m;
}

/* An expansion in progress: tokens read from here before the source,
 * with the macro that made them disabled while they last */
typedef struct Frame {
    const Tok *t;
    size_t n, i;
    const char *disabled; /* macro name, or NULL */
} Frame;

typedef struct PP {
    Lexer *lx;
    Macros macros;
    const TokList *in;
    size_t pos;
    Frame *frames;
    size_t nframes, fcap;
    /* macros disabled by the frames on the stack */
} PP;

static void pp_push_frame(PP *pp, const Tok *t, size_t n, const char *disabled)
{
    if (pp->nframes == pp->fcap) {
        size_t ncap = pp->fcap ? pp->fcap * 2 : 16;
        Frame *nf = arena_alloc(pp->lx->a, ncap * sizeof(*nf));
        if (pp->nframes) {
            memcpy(nf, pp->frames, pp->nframes * sizeof(*nf));
        }
        pp->frames = nf;
        pp->fcap = ncap;
    }
    Frame *f = &pp->frames[pp->nframes++];
    f->t = t;
    f->n = n;
    f->i = 0;
    f->disabled = disabled;
}

static bool pp_disabled(PP *pp, const char *name)
{
    for (size_t i = 0; i < pp->nframes; i++) {
        if (pp->frames[i].disabled == name) {
            return true;
        }
    }
    return false;
}

/* The next raw token, from the innermost frame with tokens left, else
 * from the source; frames that run out are popped. */
static const Tok *pp_peek(PP *pp)
{
    while (pp->nframes) {
        Frame *f = &pp->frames[pp->nframes - 1];
        if (f->i < f->n) {
            return &f->t[f->i];
        }
        pp->nframes--;
    }
    return &pp->in->t[pp->pos];
}

static const Tok *pp_next(PP *pp)
{
    const Tok *t = pp_peek(pp);
    if (pp->nframes) {
        pp->frames[pp->nframes - 1].i++;
    } else if (t->kind != T_EOF) {
        pp->pos++;
    }
    return t;
}

static bool tok_is_punct(const Tok *t, int p)
{
    return t->kind == T_PUNCT && t->punct == p;
}

/* Reads a directive line (after '#'), from the source only */
static void pp_directive(PP *pp)
{
    Lexer *lx = pp->lx;
    const TokList *in = pp->in;
    const Tok *t = &in->t[pp->pos];
    if (t->kind == T_NEWLINE) { /* an empty directive */
        return;
    }
    if (t->kind != T_IDENT) {
        lex_fail(lx, t->line, "bad preprocessor directive");
    }
    if (strcmp(t->text, "version") == 0 || strcmp(t->text, "extension") == 0 ||
        strcmp(t->text, "line") == 0 || strcmp(t->text, "pragma") == 0) {
        while (in->t[pp->pos].kind != T_NEWLINE &&
               in->t[pp->pos].kind != T_EOF) {
            pp->pos++;
        }
        return;
    }
    if (strcmp(t->text, "undef") == 0) {
        pp->pos++;
        t = &in->t[pp->pos];
        if (t->kind != T_IDENT) {
            lex_fail(lx, t->line, "#undef needs a name");
        }
        macro_remove(&pp->macros, t->text);
        pp->pos++;
        if (in->t[pp->pos].kind != T_NEWLINE && in->t[pp->pos].kind != T_EOF) {
            lex_fail(lx, t->line, "junk after #undef");
        }
        return;
    }
    if (strcmp(t->text, "define") != 0) {
        lex_fail(lx, t->line, "#%s is not supported", t->text);
    }
    pp->pos++;
    t = &in->t[pp->pos];
    if (t->kind != T_IDENT) {
        lex_fail(lx, t->line, "#define needs a name");
    }
    Macro *m = macro_add(lx->a, &pp->macros, t->text);
    pp->pos++;
    t = &in->t[pp->pos];
    if (tok_is_punct(t, P_LPAREN) && !t->space_before) {
        m->function_like = true;
        pp->pos++;
        const char *params[32];
        int np = 0;
        t = &in->t[pp->pos];
        if (!tok_is_punct(t, P_RPAREN)) {
            for (;;) {
                t = &in->t[pp->pos];
                if (t->kind != T_IDENT || np == 32) {
                    lex_fail(lx, t->line, "bad macro parameter list");
                }
                params[np++] = t->text;
                pp->pos++;
                t = &in->t[pp->pos];
                if (tok_is_punct(t, P_COMMA)) {
                    pp->pos++;
                    continue;
                }
                if (tok_is_punct(t, P_RPAREN)) {
                    break;
                }
                lex_fail(lx, t->line, "bad macro parameter list");
            }
        }
        pp->pos++;
        m->nparams = np;
        m->params = arena_alloc(lx->a, (np ? np : 1) * sizeof(*m->params));
        memcpy(m->params, params, np * sizeof(*params));
    }
    while (in->t[pp->pos].kind != T_NEWLINE && in->t[pp->pos].kind != T_EOF) {
        tl_push(lx->a, &m->body, &in->t[pp->pos]);
        pp->pos++;
    }
}

static void pp_expand_into(PP *pp, TokList *out, bool stop_at_eof);

/* Collects a function-like macro's arguments after its '(' */
static void pp_collect_args(PP *pp, const Macro *m, TokList *args, int *nargs,
                            int line)
{
    Lexer *lx = pp->lx;
    int depth = 0;
    int n = 0;
    memset(&args[0], 0, sizeof(args[0]));
    for (;;) {
        const Tok *t = pp_next(pp);
        if (t->kind == T_EOF) {
            lex_fail(lx, line, "unterminated call of macro %s", m->name);
        }
        if (t->kind == T_NEWLINE) {
            continue;
        }
        if (tok_is_punct(t, P_LPAREN)) {
            depth++;
        } else if (tok_is_punct(t, P_RPAREN)) {
            if (depth == 0) {
                break;
            }
            depth--;
        } else if (tok_is_punct(t, P_COMMA) && depth == 0) {
            n++;
            if (n >= 32) {
                lex_fail(lx, line, "too many macro arguments");
            }
            memset(&args[n], 0, sizeof(args[n]));
            continue;
        }
        tl_push(lx->a, &args[n], t);
    }
    *nargs = (n == 0 && args[0].n == 0) ? 0 : n + 1;
}

/* Expands one identifier token if it names a macro; returns whether it
 * did (its expansion was pushed as a frame to read next) */
static bool pp_try_expand(PP *pp, const Tok *t)
{
    Lexer *lx = pp->lx;
    Macro *m = macro_find(&pp->macros, t->text);
    if (!m || pp_disabled(pp, m->name)) {
        return false;
    }
    if (!m->function_like) {
        /* the body, with the line of its use */
        Tok *body = arena_alloc(lx->a, (m->body.n ? m->body.n : 1) * sizeof(*body));
        for (size_t i = 0; i < m->body.n; i++) {
            body[i] = m->body.t[i];
            body[i].line = t->line;
        }
        pp_push_frame(pp, body, m->body.n, m->name);
        return true;
    }
    {
        const Tok *n = pp_peek(pp);
        if (!tok_is_punct(n, P_LPAREN)) {
            return false; /* a function-like macro's name alone stays */
        }
        pp_next(pp);
    }
    TokList args[32];
    int nargs;
    pp_collect_args(pp, m, args, &nargs, t->line);
    if (nargs != m->nparams) {
        lex_fail(lx, t->line, "macro %s takes %d arguments, %d given", m->name,
                 m->nparams, nargs);
    }
    /* arguments fully expanded first, as C does */
    TokList xargs[32];
    for (int i = 0; i < nargs; i++) {
        memset(&xargs[i], 0, sizeof(xargs[i]));
        size_t saved_nframes = pp->nframes;
        pp_push_frame(pp, args[i].t, args[i].n, NULL);
        pp_expand_into(pp, &xargs[i], false);
        pp->nframes = saved_nframes;
    }
    /* substitute */
    TokList body;
    memset(&body, 0, sizeof(body));
    for (size_t i = 0; i < m->body.n; i++) {
        const Tok *bt = &m->body.t[i];
        int pi = -1;
        if (bt->kind == T_IDENT) {
            for (int k = 0; k < m->nparams; k++) {
                if (m->params[k] == bt->text) {
                    pi = k;
                    break;
                }
            }
        }
        if (pi >= 0) {
            for (size_t k = 0; k < xargs[pi].n; k++) {
                Tok c = xargs[pi].t[k];
                c.line = t->line;
                tl_push(lx->a, &body, &c);
            }
        } else {
            Tok c = *bt;
            c.line = t->line;
            tl_push(lx->a, &body, &c);
        }
    }
    pp_push_frame(pp, body.t, body.n, m->name);
    return true;
}

/* Reads tokens, expanding macros, until the current frames are
 * exhausted (stop_at_eof false: used for argument pre-expansion, where
 * the pushed frame is the whole input) or until EOF of the source. */
static void pp_expand_into(PP *pp, TokList *out, bool from_source)
{
    Lexer *lx = pp->lx;
    size_t base = pp->nframes;
    for (;;) {
        if (!from_source && pp->nframes < base) {
            return;
        }
        if (!from_source) {
            /* exhausted the argument frame? */
            bool any = false;
            for (size_t i = base - 1; i < pp->nframes; i++) {
                if (pp->frames[i].i < pp->frames[i].n) {
                    any = true;
                    break;
                }
            }
            if (!any) {
                return;
            }
        }
        const Tok *t = pp_next(pp);
        if (t->kind == T_EOF) {
            if (from_source) {
                tl_push(lx->a, out, t);
            }
            return;
        }
        if (t->kind == T_NEWLINE) {
            continue;
        }
        if (t->kind == T_HASH) {
            if (pp->nframes) {
                lex_fail(lx, t->line, "'#' inside a macro");
            }
            pp_directive(pp);
            continue;
        }
        if (t->kind == T_IDENT && pp_try_expand(pp, t)) {
            continue;
        }
        tl_push(lx->a, out, t);
    }
}

void glslc_lex_and_preprocess(Arena *a, const char *source, TokList *out,
                              jmp_buf *err, char *error, size_t error_cap,
                              int *error_line)
{
    Lexer lx = { a, err, error, error_cap, error_line };
    Intern *in = arena_alloc(a, sizeof(*in));
    TokList raw;
    PP pp;
    memset(in, 0, sizeof(*in));
    memset(&raw, 0, sizeof(raw));
    memset(&pp, 0, sizeof(pp));
    tokenize(&lx, in, source, &raw);
    pp.lx = &lx;
    pp.in = &raw;
    memset(out, 0, sizeof(*out));
    pp_expand_into(&pp, out, true);
}
