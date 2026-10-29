/*
 * GLSL to SPIR-V: what the lexer hands the compiler
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PGRAPH_SPV_GLSLC_INT_H
#define PGRAPH_SPV_GLSLC_INT_H

#include "glslc.h"
#include <setjmp.h>

enum TokKind {
    T_EOF = 0,
    T_IDENT,
    T_INT,   /* int literal, val.u */
    T_UINT,  /* uint literal, val.u */
    T_FLOAT, /* float literal, val.f */
    T_PUNCT, /* punct holds the operator */
    T_NEWLINE, /* only before preprocessing */
    T_HASH,    /* '#' at a line's start, only before preprocessing */
};

/* Operators and punctuation, one code each */
enum Punct {
    P_NONE = 0,
    P_LPAREN, P_RPAREN, P_LBRACKET, P_RBRACKET, P_LBRACE, P_RBRACE,
    P_DOT, P_COMMA, P_SEMI, P_QUESTION, P_COLON,
    P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT,
    P_LT, P_GT, P_LE, P_GE, P_EQ, P_NE,
    P_SHL, P_SHR, P_AND, P_OR, P_XOR, P_TILDE, P_NOT, P_ANDAND, P_OROR,
    P_XORXOR, P_INC, P_DEC,
    P_ASSIGN, P_ADD_ASSIGN, P_SUB_ASSIGN, P_MUL_ASSIGN, P_DIV_ASSIGN,
    P_MOD_ASSIGN, P_SHL_ASSIGN, P_SHR_ASSIGN, P_AND_ASSIGN, P_OR_ASSIGN,
    P_XOR_ASSIGN,
};

typedef struct Tok {
    uint8_t kind;
    uint8_t punct;
    uint8_t space_before; /* whitespace ended just before it */
    uint32_t line;
    const char *text; /* identifier: NUL-terminated, interned */
    union {
        uint32_t u;
        float f;
    } val;
} Tok;

typedef struct TokList {
    Tok *t;
    size_t n, cap;
} TokList;

/* The compiler's context the lexer needs: errors go through it */
typedef struct GlslcCtx GlslcCtx;

/* Everything allocated during a compile: freed in one go */
typedef struct Arena {
    char **blocks;
    size_t nblocks, cap;
    char *cur;
    size_t left;
} Arena;

void *arena_alloc(Arena *a, size_t n);
const char *arena_strndup(Arena *a, const char *s, size_t n);
void arena_free_all(Arena *a);

/* Tokenizes and preprocesses source into out (no newlines, no
 * directives). On an error, longjmps through err with the message set. */
void glslc_lex_and_preprocess(Arena *a, const char *source, TokList *out,
                              jmp_buf *err, char *error, size_t error_cap,
                              int *error_line);

#endif
