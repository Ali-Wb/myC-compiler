#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h — recursive-descent parser interface.
 *
 * parse() is the sole public entry point.  It loads the token array into
 * the parser's global scanner state and returns a fully-built AST.
 *
 * parse_expression() is also exposed so test code can call it directly
 * after initialising the scanner via parse() on a small token stream.
 */

#include "lexer.h"
#include "ast.h"

/*
 * parse — tokenise and parse in one shot.
 *
 * tokens  : array produced by tokenize()
 * count   : total number of tokens (including the TK_EOF sentinel)
 *
 * Returns a heap-allocated ND_PROGRAM root node.
 * Calls die() on any syntax error.
 */
Node *parse(Token *tokens, int count);

/*
 * parse_expression — public expression entry point.
 *
 * Parses one full expression (up to logical-or precedence) from the
 * current scanner position.  parse() must have been called first so
 * the global scanner state is initialised.
 */
Node *parse_expression(void);

#endif /* PARSER_H */
