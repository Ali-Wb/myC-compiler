#ifndef SEMA_H
#define SEMA_H

/*
 * sema.h — semantic analysis pass interface.
 *
 * resolve() walks an ND_PROGRAM tree, builds scoped symbol-table entries
 * for every variable declaration, and verifies every identifier reference
 * is in scope.  It exits with an error on the first violation.
 *
 * After resolve() returns, every ND_IDENT and ND_VAR_DECL in the tree
 * has a corresponding Symbol in the table and callers (e.g. the codegen)
 * can use symtable_lookup() to find stack offsets.
 */

#include "ast.h"
#include "symtable.h"

/*
 * Resolve all variable declarations and identifier uses in `program`.
 *
 * `st` must be a freshly created SymTable (symtable_new()).
 * The function resets st->stack_offset to 0 at the start of each
 * function definition so each function gets its own frame layout.
 *
 * Errors that cause an immediate exit:
 *   - Identifier used before it is declared in any enclosing scope.
 *   - Variable declared more than once in the same scope.
 *   - Duplicate parameter name within one function.
 */
void resolve(Node *program, SymTable *st);

#endif /* SEMA_H */
