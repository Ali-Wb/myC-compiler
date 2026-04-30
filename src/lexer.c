/*
 * lexer.c — tokeniser implementation.
 *
 * Internal design
 * ---------------
 * A private `Lex` struct tracks the scan position and a growable Token
 * array.  `tokenize()` fills the array, appends TK_EOF, then hands the
 * raw array back to the caller.  The Lex struct itself is stack-allocated
 * inside tokenize() and never escapes.
 *
 * Value strings
 * -------------
 * Every token (except TK_EOF) owns a heap-allocated `value` string.
 *
 * For string literals the raw source bytes are decoded before storage:
 *   \" → "   \n → newline   \t → tab   \\ → backslash   \0 → NUL
 * Any other \x sequence is stored as the character after the backslash.
 *
 * For everything else (integers, identifiers, keywords, operators,
 * punctuation) `value` is a verbatim copy of the source text.
 */

#include "lexer.h"

/* ------------------------------------------------------------------ */
/*  Private scanner state                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *src;    /* original source (not mutated)          */
    const char *p;      /* current scan position                  */
    int         line;   /* 1-based current line                   */
    Token      *tokens; /* growable output array                  */
    int         count;  /* tokens written so far                  */
    int         cap;    /* allocated capacity                     */
} Lex;

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static void push(Lex *lx, TokenType type, char *value, int line)
{
    if (lx->count == lx->cap) {
        lx->cap    = lx->cap ? lx->cap * 2 : 128;
        lx->tokens = realloc(lx->tokens, (size_t)lx->cap * sizeof(Token));
        if (!lx->tokens) die("out of memory in lexer");
    }
    lx->tokens[lx->count++] = (Token){ .type = type, .value = value, .line = line };
}

/* Copy exactly `n` bytes from `s` into a new NUL-terminated string. */
static char *dup(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (!p) die("out of memory in lexer");
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_alnum(char c) { return is_alpha(c) || (c >= '0' && c <= '9'); }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* Map a keyword spelling to its TokenType, or TK_IDENT if not a keyword. */
static TokenType keyword_or_ident(const char *s, int len)
{
    /* Table of all recognised keywords. */
    static const struct { const char *kw; TokenType type; } kws[] = {
        { "int",    TK_INT    },
        { "char",   TK_CHAR   },
        { "void",   TK_VOID   },
        { "return", TK_RETURN },
        { "if",     TK_IF     },
        { "else",   TK_ELSE   },
        { "while",  TK_WHILE  },
        { "for",    TK_FOR    },
    };
    for (size_t i = 0; i < sizeof kws / sizeof *kws; i++)
        if ((int)strlen(kws[i].kw) == len && memcmp(kws[i].kw, s, (size_t)len) == 0)
            return kws[i].type;
    return TK_IDENT;
}

/*
 * Decode a string literal body (the bytes between the opening and closing
 * quotes, with `len` raw source bytes) into a freshly allocated string
 * with escape sequences replaced by their actual characters.
 *
 * Supported:  \"  \n  \t  \\  \0   — anything else: copy literal char.
 */
static char *decode_string(const char *raw, int len, int line)
{
    /* Output can only be shorter than the input, so len+1 is always safe. */
    char *out = malloc((size_t)len + 1);
    if (!out) die("out of memory in lexer");
    int j = 0;
    for (int i = 0; i < len; i++) {
        if (raw[i] != '\\') {
            out[j++] = raw[i];
            continue;
        }
        /* Escape sequence: look at the next character. */
        i++;
        if (i >= len) {
            fprintf(stderr, "error: unterminated escape sequence in string on line %d\n", line);
            exit(1);
        }
        switch (raw[i]) {
        case 'n':  out[j++] = '\n'; break;
        case 't':  out[j++] = '\t'; break;
        case '\\': out[j++] = '\\'; break;
        case '"':  out[j++] = '"';  break;
        case '0':  out[j++] = '\0'; break;
        default:
            /* Unknown escape: keep the character after the backslash. */
            out[j++] = raw[i];
            break;
        }
    }
    out[j] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/*  Main scanner loop                                                   */
/* ------------------------------------------------------------------ */

Token *tokenize(const char *src, int *out_count)
{
    Lex lx = { .src = src, .p = src, .line = 1 };

    while (1) {

        /* --- Skip whitespace, track newlines --- */
        while (*lx.p == ' ' || *lx.p == '\t' || *lx.p == '\r' || *lx.p == '\n') {
            if (*lx.p == '\n') lx.line++;
            lx.p++;
        }

        /* --- Single-line comment  //…\n --- */
        if (lx.p[0] == '/' && lx.p[1] == '/') {
            while (*lx.p != '\0' && *lx.p != '\n')
                lx.p++;
            continue; /* re-enter loop to skip the newline and update line */
        }

        /* --- Block comment  /* … */ --- */
        if (lx.p[0] == '/' && lx.p[1] == '*') {
            int start_line = lx.line;
            lx.p += 2;
            while (*lx.p != '\0') {
                if (lx.p[0] == '*' && lx.p[1] == '/') { lx.p += 2; break; }
                if (*lx.p == '\n') lx.line++;
                lx.p++;
            }
            if (*lx.p == '\0' && !(lx.p[-1] == '/' && lx.p[-2] == '*')) {
                fprintf(stderr, "error: unterminated block comment opened on line %d\n", start_line);
                exit(1);
            }
            continue;
        }

        /* --- End of input --- */
        if (*lx.p == '\0') {
            push(&lx, TK_EOF, NULL, lx.line);
            break;
        }

        int         line = lx.line;   /* line of the token being scanned */
        char        c    = lx.p[0];
        char        c2   = lx.p[1];

        /* --- Integer literal --- */
        if (is_digit(c)) {
            const char *start = lx.p;
            while (is_digit(*lx.p)) lx.p++;
            push(&lx, TK_INT_LIT, dup(start, (size_t)(lx.p - start)), line);
            continue;
        }

        /* --- String literal --- */
        if (c == '"') {
            lx.p++;                         /* skip the opening quote      */
            const char *body = lx.p;
            while (*lx.p != '\0' && *lx.p != '"') {
                if (*lx.p == '\\' && *(lx.p + 1) != '\0')
                    lx.p++;                 /* skip the escaped character   */
                if (*lx.p == '\n') lx.line++;
                lx.p++;
            }
            if (*lx.p == '\0') {
                fprintf(stderr, "error: unterminated string literal on line %d\n", line);
                exit(1);
            }
            int   body_len = (int)(lx.p - body);
            char *decoded  = decode_string(body, body_len, line);
            lx.p++;                         /* skip the closing quote      */
            push(&lx, TK_STR_LIT, decoded, line);
            continue;
        }

        /* --- Identifier or keyword --- */
        if (is_alpha(c)) {
            const char *start = lx.p;
            while (is_alnum(*lx.p)) lx.p++;
            int       len  = (int)(lx.p - start);
            TokenType type = keyword_or_ident(start, len);
            push(&lx, type, dup(start, (size_t)len), line);
            continue;
        }

        /* --- Two-character operators (must come before single-char) --- */
#define TWO(a, b, k)                                        \
        if (c == (a) && c2 == (b)) {                        \
            push(&lx, (k), dup(lx.p, 2), line);            \
            lx.p += 2; continue;                            \
        }
        TWO('=', '=', TK_EQEQ)
        TWO('!', '=', TK_NEQ)
        TWO('<', '=', TK_LE)
        TWO('>', '=', TK_GE)
        TWO('&', '&', TK_AND)
        TWO('|', '|', TK_OR)
#undef TWO

        /* --- Single-character tokens --- */
#define ONE(ch, k)                                          \
        case (ch):                                          \
            push(&lx, (k), dup(lx.p, 1), line);            \
            lx.p++; continue;

        switch (c) {
            ONE('(', TK_LPAREN)    ONE(')', TK_RPAREN)
            ONE('{', TK_LBRACE)    ONE('}', TK_RBRACE)
            ONE('[', TK_LBRACKET)  ONE(']', TK_RBRACKET)
            ONE(';', TK_SEMI)      ONE(',', TK_COMMA)
            ONE('+', TK_PLUS)      ONE('-', TK_MINUS)
            ONE('*', TK_STAR)      ONE('/', TK_SLASH)
            ONE('%', TK_PERCENT)
            ONE('!', TK_BANG)
            ONE('=', TK_EQ)
            ONE('<', TK_LT)        ONE('>', TK_GT)
            default:
                fprintf(stderr, "error: unexpected character '%c' (0x%02x) on line %d\n",
                        c, (unsigned char)c, line);
                exit(1);
        }
#undef ONE
    }

    *out_count = lx.count;
    return lx.tokens;
}

/* ------------------------------------------------------------------ */
/*  Cleanup                                                             */
/* ------------------------------------------------------------------ */

void tokens_free(Token *tokens, int count)
{
    for (int i = 0; i < count; i++)
        free(tokens[i].value);  /* free(NULL) is a no-op for TK_EOF */
    free(tokens);
}
