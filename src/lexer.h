#ifndef LEXER_H
#define LEXER_H

/*
 * lexer.h — public tokeniser interface.
 *
 * The only things callers need: tokenize() to produce a Token array and
 * tokens_free() to release it.  Everything else is an implementation
 * detail inside lexer.c.
 *
 * TokenType and Token are defined in compiler.h.
 */

#include "compiler.h"

/*
 * Tokenise the NUL-terminated C source string `src`.
 *
 * Returns a heap-allocated array of Token structs.  The last element is
 * always a TK_EOF sentinel.  Writes the total number of tokens
 * (including TK_EOF) to *out_count.
 *
 * Each token's `value` field is a separately heap-allocated, NUL-
 * terminated string:
 *   TK_INT_LIT  — decimal digit characters ("42")
 *   TK_STR_LIT  — decoded string content, escape sequences resolved:
 *                   \n → newline, \t → tab, \\ → backslash, \" → quote
 *   TK_IDENT /
 *   keyword     — identifier or keyword text ("foo", "return")
 *   operator /
 *   punctuation — the source characters ("==", "+", "(")
 *   TK_EOF      — NULL
 *
 * On any lexical error (unknown character, unterminated string or
 * block comment) prints a message with the line number to stderr and
 * calls exit(1).
 */
Token *tokenize(const char *src, int *out_count);

/*
 * Free a token array produced by tokenize().
 * Releases each token's value string then the array itself.
 */
void tokens_free(Token *tokens, int count);

#endif /* LEXER_H */
