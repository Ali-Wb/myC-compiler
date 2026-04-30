#ifndef AST_PRINT_H
#define AST_PRINT_H

/*
 * ast_print.h — AST pretty-printer interface.
 *
 * print_ast() recursively walks a Node tree and writes a human-readable
 * indented representation to stdout.  Useful for verifying parser output
 * before the code generator is written.
 *
 * Call with indent = 0 on the root node.
 */

#include "ast.h"

void print_ast(Node *node, int indent);

#endif /* AST_PRINT_H */
