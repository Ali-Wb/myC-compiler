/*
 * sema.c — semantic analysis: variable declaration and use checking.
 *
 * Pass overview
 * -------------
 * resolve() iterates over every ND_FUNC in the program.  For each
 * function it:
 *
 *   1. Resets st->stack_offset to 0  (each function owns its frame).
 *   2. Opens a "parameter scope" and defines each parameter.
 *   3. Calls resolve_stmt() on the function body (an ND_BLOCK), which
 *      opens its own inner scope for block-local variables.
 *   4. Closes the parameter scope.
 *
 * resolve_stmt() dispatches on node kind:
 *   ND_BLOCK    — opens/closes a scope; recurses on each statement.
 *   ND_VAR_DECL — resolves the optional initialiser first (so that
 *                 `int x = x;` correctly catches the use), then defines
 *                 the new symbol; dies if the name is already in the
 *                 current scope.
 *   ND_RETURN / ND_IF / ND_WHILE / ND_FOR — recurse on sub-expressions
 *                 and sub-statements.
 *   expression kinds — delegated to resolve_expr().
 *
 * resolve_expr() recurses into every sub-expression.  ND_IDENT triggers
 * a lookup; any miss is a fatal "undefined variable" error.
 *
 * Scoping rules implemented
 * -------------------------
 *   - Variables are visible from the point of declaration to the end of
 *     their enclosing block.
 *   - An inner scope may shadow a name from an outer scope.
 *   - Redeclaring a name within the SAME scope is an error.
 *   - Function calls are not checked (no global function table yet).
 */

#include "sema.h"

/* ------------------------------------------------------------------ */
/*  Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void resolve_expr(Node *expr, SymTable *st);
static void resolve_stmt(Node *stmt, SymTable *st);

/* ------------------------------------------------------------------ */
/*  Expression resolver                                                 */
/* ------------------------------------------------------------------ */

static void resolve_expr(Node *expr, SymTable *st)
{
    if (!expr) return;

    switch (expr->kind) {

    /* Leaves with no sub-expressions — nothing to check. */
    case ND_INT_LIT:
    case ND_STR_LIT:
        break;

    /* Variable reference — must be in scope. */
    case ND_IDENT:
        if (!symtable_lookup(st, expr->ident.name))
            dief("undefined variable '%s'", expr->ident.name);
        break;

    /* Assignment: resolve both sides (lhs is typically an ND_IDENT). */
    case ND_ASSIGN:
        resolve_expr(expr->assign.lhs, st);
        resolve_expr(expr->assign.rhs, st);
        break;

    /* Binary operation: resolve both operands. */
    case ND_BINOP:
        resolve_expr(expr->binop.lhs, st);
        resolve_expr(expr->binop.rhs, st);
        break;

    /* Unary operation: resolve the single operand. */
    case ND_UNARY:
        resolve_expr(expr->unary.operand, st);
        break;

    /* Function call: resolve each argument.
       We do not check whether the callee itself is defined — that
       requires a global function table and is left for a later pass. */
    case ND_CALL:
        for (NodeList *l = expr->call.args; l; l = l->next)
            resolve_expr(l->node, st);
        break;

    default:
        /* Should not appear in expression position; ignore. */
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Statement resolver                                                  */
/* ------------------------------------------------------------------ */

static void resolve_stmt(Node *stmt, SymTable *st)
{
    if (!stmt) return;

    switch (stmt->kind) {

    /*
     * Block — opens a new scope, resolves each child statement in
     * order, then closes the scope.  Order matters: a later statement
     * in the same block can use a variable declared earlier, but not
     * the other way around.
     */
    case ND_BLOCK:
        symtable_enter_scope(st);
        for (NodeList *l = stmt->block.stmts; l; l = l->next)
            resolve_stmt(l->node, st);
        symtable_exit_scope(st);
        break;

    /*
     * Variable declaration — resolve the initialiser BEFORE defining
     * the symbol so that  int x = x + 1;  catches the undefined 'x'.
     * Then define the symbol in the current scope; if the name is
     * already taken in that scope, symtable_define returns NULL.
     */
    case ND_VAR_DECL:
        if (stmt->var_decl.init)
            resolve_expr(stmt->var_decl.init, st);
        if (!symtable_define(st, stmt->var_decl.name, stmt->var_decl.type_name))
            dief("variable '%s' declared twice in the same scope",
                 stmt->var_decl.name);
        break;

    /* return expr? */
    case ND_RETURN:
        resolve_expr(stmt->ret.expr, st);
        break;

    /*
     * if (cond) then else?
     * The condition is resolved in the current scope.  Each branch is
     * resolved as-is: if it is an ND_BLOCK, resolve_stmt will open a
     * new inner scope for it automatically.
     */
    case ND_IF:
        resolve_expr(stmt->if_.cond, st);
        resolve_stmt(stmt->if_.then_, st);
        resolve_stmt(stmt->if_.else_, st);
        break;

    /* while (cond) body */
    case ND_WHILE:
        resolve_expr(stmt->while_.cond, st);
        resolve_stmt(stmt->while_.body, st);
        break;

    /*
     * for (init; cond; step) body
     *
     * A dedicated scope wraps the entire for-construct so that a
     * variable declared in the init clause (e.g. `int i = 0`) is
     * visible in cond, step, and body, but not after the loop.
     *
     *   for-header scope  ←  init var_decl / assign_expr
     *     body scope      ←  opened by ND_BLOCK if body is a block
     */
    case ND_FOR:
        symtable_enter_scope(st);

        /* init: may be a var_decl, an expression, or absent (NULL). */
        if (stmt->for_.init) {
            if (stmt->for_.init->kind == ND_VAR_DECL)
                resolve_stmt(stmt->for_.init, st);   /* uses VAR_DECL case */
            else
                resolve_expr(stmt->for_.init, st);   /* plain expression  */
        }

        resolve_expr(stmt->for_.cond, st);   /* cond: optional (NULL ok)  */
        resolve_expr(stmt->for_.step, st);   /* step: optional (NULL ok)  */
        resolve_stmt(stmt->for_.body, st);   /* body: opens its own scope
                                                if it is an ND_BLOCK       */
        symtable_exit_scope(st);
        break;

    /*
     * Expression used as a statement (call, assignment, arithmetic
     * with side effects, etc.).  Resolve it as an expression.
     */
    default:
        resolve_expr(stmt, st);
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Top-level pass                                                      */
/* ------------------------------------------------------------------ */

void resolve(Node *program, SymTable *st)
{
    if (program->kind != ND_PROGRAM)
        die("resolve: expected ND_PROGRAM root");

    for (NodeList *fl = program->program.funcs; fl; fl = fl->next) {
        Node *fn = fl->node;

        /*
         * Each function gets a fresh stack frame: reset the offset
         * counter so that the first local in every function starts at
         * -8(%rbp) regardless of what previous functions used.
         */
        st->stack_offset = 0;

        /*
         * Open the parameter scope.  Parameters live here; the function
         * body block will open an inner scope for its own locals.
         * This means a local can shadow a parameter (C allows this).
         */
        symtable_enter_scope(st);

        for (NodeList *pl = fn->func.params; pl; pl = pl->next) {
            Node *param = pl->node;  /* each param is an ND_VAR_DECL */
            if (!symtable_define(st, param->var_decl.name,
                                     param->var_decl.type_name))
                dief("duplicate parameter '%s' in function '%s'",
                     param->var_decl.name, fn->func.name);
        }

        /* Resolve the body.  It is always an ND_BLOCK, so resolve_stmt
           will open a nested scope for block-local variables. */
        resolve_stmt(fn->func.body, st);

        symtable_exit_scope(st);  /* close parameter scope */
    }
}
