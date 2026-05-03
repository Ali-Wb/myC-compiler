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
/*  Type system                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    TYPE_INT,
    TYPE_CHAR,
    TYPE_VOID,
    TYPE_POINTER,
} TypeKind;

typedef struct Type {
    TypeKind     kind;
    struct Type *base;  /* non-NULL only for TYPE_POINTER */
} Type;

static inline Type *type_new(TypeKind kind, Type *base)
{
    Type *t = malloc(sizeof *t);
    if (!t) { fprintf(stderr, "error: out of memory allocating Type\n"); exit(1); }
    t->kind = kind;
    t->base = base;
    return t;
}

static inline Type *type_int(void)           { return type_new(TYPE_INT,     NULL); }
static inline Type *type_char(void)          { return type_new(TYPE_CHAR,    NULL); }
static inline Type *type_void(void)          { return type_new(TYPE_VOID,    NULL); }
static inline Type *type_pointer(Type *base) { return type_new(TYPE_POINTER, base); }

static inline void type_free(Type *t)
{
    if (!t) return;
    type_free(t->base);
    free(t);
}

static inline Type *type_copy(const Type *t)
{
    if (!t) return NULL;
    return type_new(t->kind, type_copy(t->base));
}

/* Size in bytes of the pointed-at value; used for pointer arithmetic. */
static inline int type_sizeof(const Type *t)
{
    if (!t) return 8;
    switch (t->kind) {
    case TYPE_CHAR:    return 1;
    case TYPE_POINTER: return 8;
    default:           return 8;  /* int, void */
    }
}

/* Write a human-readable spelling of t into buf (e.g. "int", "char*", "int**"). */
static inline const char *type_str(const Type *t, char *buf, int bufsz)
{
    if (!t) { snprintf(buf, (size_t)bufsz, "(null)"); return buf; }
    if (t->kind == TYPE_POINTER) {
        type_str(t->base, buf, bufsz);
        int len = (int)strlen(buf);
        if (len < bufsz - 1) { buf[len] = '*'; buf[len + 1] = '\0'; }
    } else {
        switch (t->kind) {
        case TYPE_INT:  snprintf(buf, (size_t)bufsz, "int");  break;
        case TYPE_CHAR: snprintf(buf, (size_t)bufsz, "char"); break;
        case TYPE_VOID: snprintf(buf, (size_t)bufsz, "void"); break;
        default:        snprintf(buf, (size_t)bufsz, "?");    break;
        }
    }
    return buf;
}

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
    TK_AMP,         /* &   (address-of in unary context)              */

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
