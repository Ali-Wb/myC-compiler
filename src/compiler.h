#ifndef COMPILER_H
#define COMPILER_H

/*
 * compiler.h — shared foundation for every compilation stage.
 *
 * Provides: standard includes, diagnostic macros, the TokenType enum,
 * and the Token struct.  Every other header includes this one first.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* ------------------------------------------------------------------ */
/*  Diagnostic macros                                                   */
/* ------------------------------------------------------------------ */

/* Print a plain message to stderr and exit. */
#define die(msg)                                                        \
    do {                                                                \
        fprintf(stderr, "error: %s (at %s:%d)\n",                      \
                (msg), __FILE__, __LINE__);                             \
        exit(1);                                                        \
    } while (0)

/* printf-style variant of die(). */
#define dief(fmt, ...)                                                  \
    do {                                                                \
        fprintf(stderr, "error: " fmt " (at %s:%d)\n",                 \
                __VA_ARGS__, __FILE__, __LINE__);                       \
        exit(1);                                                        \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Token types                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    /* --- Literals --- */
    TK_INT_LIT,     /* integer literal:  42, 0, 255                   */
    TK_STR_LIT,     /* string literal:   "hello"                      */
    TK_IDENT,       /* identifier:       foo, my_var                  */

    /* --- Keywords --- */
    TK_INT,         /* int                                            */
    TK_CHAR,        /* char                                           */
    TK_VOID,        /* void                                           */
    TK_RETURN,      /* return                                         */
    TK_IF,          /* if                                             */
    TK_ELSE,        /* else                                           */
    TK_WHILE,       /* while                                          */
    TK_FOR,         /* for                                            */

    /* --- Operators --- */
    TK_PLUS,        /* +                                              */
    TK_MINUS,       /* -                                              */
    TK_STAR,        /* *                                              */
    TK_SLASH,       /* /                                              */
    TK_PERCENT,     /* %                                              */
    TK_EQ,          /* =                                              */
    TK_EQEQ,        /* ==                                             */
    TK_NEQ,         /* !=                                             */
    TK_LT,          /* <                                              */
    TK_GT,          /* >                                              */
    TK_LE,          /* <=                                             */
    TK_GE,          /* >=                                             */
    TK_AND,         /* &&                                             */
    TK_OR,          /* ||                                             */
    TK_BANG,        /* !                                              */

    /* --- Punctuation --- */
    TK_LPAREN,      /* (                                              */
    TK_RPAREN,      /* )                                              */
    TK_LBRACE,      /* {                                              */
    TK_RBRACE,      /* }                                              */
    TK_LBRACKET,    /* [                                              */
    TK_RBRACKET,    /* ]                                              */
    TK_SEMI,        /* ;                                              */
    TK_COMMA,       /* ,                                              */

    /* --- Sentinel --- */
    TK_EOF,
} TokenType;

/* ------------------------------------------------------------------ */
/*  Token                                                               */
/* ------------------------------------------------------------------ */

/*
 * A single lexical token.
 *
 * `value` is a heap-allocated, NUL-terminated copy of the raw source
 * text for this token (the identifier name, the literal digits, the
 * string contents without surrounding quotes, or the operator/punct
 * characters).  The lexer owns the allocation; call token_free() when
 * discarding a standalone token, or lexer_free() to release the whole
 * token array.
 */
typedef struct {
    TokenType  type;
    char      *value;   /* heap-allocated text representation          */
    int        line;    /* 1-based source line number                  */
} Token;

#endif /* COMPILER_H */
