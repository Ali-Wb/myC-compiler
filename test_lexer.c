/*
 * test_lexer.c — standalone lexer exerciser.
 *
 * Usage: ./test_lexer <file.c>
 *
 * Reads the given source file, calls tokenize(), and prints every token
 * in the format:
 *   [LINE] TYPENAME 'value'
 *
 * Compile (from the project root):
 *   gcc -std=c11 -Wall -I src src/lexer.c test_lexer.c -o test_lexer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/compiler.h"
#include "src/lexer.h"

/* Human-readable name for each TokenType. */
static const char *type_name(TokenType t)
{
    switch (t) {
    case TK_INT_LIT:  return "TK_INT_LIT";
    case TK_STR_LIT:  return "TK_STR_LIT";
    case TK_IDENT:    return "TK_IDENT";
    case TK_INT:      return "TK_INT";
    case TK_CHAR:     return "TK_CHAR";
    case TK_VOID:     return "TK_VOID";
    case TK_RETURN:   return "TK_RETURN";
    case TK_IF:       return "TK_IF";
    case TK_ELSE:     return "TK_ELSE";
    case TK_WHILE:    return "TK_WHILE";
    case TK_FOR:      return "TK_FOR";
    case TK_PLUS:     return "TK_PLUS";
    case TK_MINUS:    return "TK_MINUS";
    case TK_STAR:     return "TK_STAR";
    case TK_SLASH:    return "TK_SLASH";
    case TK_PERCENT:  return "TK_PERCENT";
    case TK_EQ:       return "TK_EQ";
    case TK_EQEQ:     return "TK_EQEQ";
    case TK_NEQ:      return "TK_NEQ";
    case TK_LT:       return "TK_LT";
    case TK_GT:       return "TK_GT";
    case TK_LE:       return "TK_LE";
    case TK_GE:       return "TK_GE";
    case TK_AND:      return "TK_AND";
    case TK_OR:       return "TK_OR";
    case TK_BANG:     return "TK_BANG";
    case TK_LPAREN:   return "TK_LPAREN";
    case TK_RPAREN:   return "TK_RPAREN";
    case TK_LBRACE:   return "TK_LBRACE";
    case TK_RBRACE:   return "TK_RBRACE";
    case TK_LBRACKET: return "TK_LBRACKET";
    case TK_RBRACKET: return "TK_RBRACKET";
    case TK_SEMI:     return "TK_SEMI";
    case TK_COMMA:    return "TK_COMMA";
    case TK_EOF:      return "TK_EOF";
    default:          return "TK_UNKNOWN";
    }
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.c>\n", argv[0]);
        return 1;
    }

    char  *src    = read_file(argv[1]);
    int    count  = 0;
    Token *tokens = tokenize(src, &count);

    for (int i = 0; i < count; i++) {
        Token *t = &tokens[i];
        if (t->type == TK_EOF)
            printf("[%d] %s\n", t->line, type_name(t->type));
        else
            printf("[%d] %-14s '%s'\n", t->line, type_name(t->type), t->value);
    }

    tokens_free(tokens, count);
    free(src);
    return 0;
}
