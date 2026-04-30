#ifndef AST_H
#define AST_H

/*
 * ast.h — Abstract Syntax Tree definitions.
 *
 * The parser produces a tree of Node structures.  Each Node carries a
 * NodeKind tag and a union whose active member is determined by that tag.
 * NodeList is a singly-linked cons-cell used wherever a variable-length
 * sequence of nodes is needed (block statements, argument lists, parameter
 * lists, top-level function definitions).
 *
 * Ownership: the tree is heap-allocated.  Call node_free() on the root to
 * release the entire tree (it recurses through children and NodeLists).
 */

#include "compiler.h"

/* ------------------------------------------------------------------ */
/*  Node kinds                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    /* --- Top-level / structural --- */
    ND_PROGRAM,     /* root: list of function definitions              */
    ND_FUNC,        /* function definition                             */
    ND_BLOCK,       /* compound statement  { stmt* }                   */

    /* --- Statements --- */
    ND_RETURN,      /* return expr?                                    */
    ND_IF,          /* if (cond) then else?                            */
    ND_WHILE,       /* while (cond) body                               */
    ND_FOR,         /* for (init; cond; step) body                     */
    ND_VAR_DECL,    /* type name (= init)?                             */

    /* --- Expressions --- */
    ND_ASSIGN,      /* lhs = rhs                                       */
    ND_BINOP,       /* lhs op rhs  (arithmetic, relational, logical)   */
    ND_UNARY,       /* op operand  (-, !, etc.)                        */
    ND_CALL,        /* name(args)                                      */
    ND_IDENT,       /* variable / parameter reference                  */
    ND_INT_LIT,     /* integer constant                                */
    ND_STR_LIT,     /* string constant                                 */
} NodeKind;

/* ------------------------------------------------------------------ */
/*  Forward declarations                                                */
/* ------------------------------------------------------------------ */

typedef struct Node     Node;
typedef struct NodeList NodeList;

/* ------------------------------------------------------------------ */
/*  NodeList — singly-linked sequence of nodes                         */
/* ------------------------------------------------------------------ */

struct NodeList {
    Node     *node;
    NodeList *next;
};

/* ------------------------------------------------------------------ */
/*  Node — tagged union                                                 */
/* ------------------------------------------------------------------ */

struct Node {
    NodeKind kind;

    union {
        /*
         * ND_PROGRAM
         *   funcs — list of ND_FUNC nodes in source order.
         */
        struct {
            NodeList *funcs;
        } program;

        /*
         * ND_FUNC
         *   name     — function name (heap-allocated).
         *   ret_type — return type spelling, e.g. "int" (heap-allocated).
         *   params   — list of ND_VAR_DECL nodes (no initialiser) for parameters.
         *   body     — ND_BLOCK.
         */
        struct {
            char     *name;
            char     *ret_type;
            NodeList *params;
            Node     *body;
        } func;

        /*
         * ND_BLOCK
         *   stmts — list of statement nodes in order.
         */
        struct {
            NodeList *stmts;
        } block;

        /*
         * ND_RETURN
         *   expr — value expression, or NULL for bare `return;`.
         */
        struct {
            Node *expr;
        } ret;

        /*
         * ND_IF
         *   cond  — condition expression.
         *   then_ — statement to execute when cond is true.
         *   else_ — optional else branch (NULL if absent).
         */
        struct {
            Node *cond;
            Node *then_;
            Node *else_;
        } if_;

        /*
         * ND_WHILE
         *   cond — loop condition.
         *   body — loop body statement.
         */
        struct {
            Node *cond;
            Node *body;
        } while_;

        /*
         * ND_FOR
         *   init — ND_VAR_DECL or ND_ASSIGN (NULL if omitted).
         *   cond — loop condition expression (NULL means infinite).
         *   step — post-iteration expression (NULL if omitted).
         *   body — loop body statement.
         */
        struct {
            Node *init;
            Node *cond;
            Node *step;
            Node *body;
        } for_;

        /*
         * ND_VAR_DECL
         *   type_name — type spelling, e.g. "int", "char*" (heap-allocated).
         *   name      — variable name (heap-allocated).
         *   init      — initialiser expression, or NULL.
         */
        struct {
            char *type_name;
            char *name;
            Node *init;
        } var_decl;

        /*
         * ND_ASSIGN
         *   lhs — must be an lvalue (ND_IDENT, subscript, etc.).
         *   rhs — value expression.
         */
        struct {
            Node *lhs;
            Node *rhs;
        } assign;

        /*
         * ND_BINOP
         *   op  — operator string: "+", "-", "*", "/", "%",
         *          "==", "!=", "<", ">", "<=", ">=", "&&", "||"
         *          (heap-allocated so it can outlive the token stream).
         *   lhs — left operand.
         *   rhs — right operand.
         */
        struct {
            char *op;
            Node *lhs;
            Node *rhs;
        } binop;

        /*
         * ND_UNARY
         *   op      — operator string: "-", "!" (heap-allocated).
         *   operand — the expression the operator applies to.
         */
        struct {
            char *op;
            Node *operand;
        } unary;

        /*
         * ND_CALL
         *   name — callee name (heap-allocated).
         *   args — list of argument expression nodes (left-to-right).
         */
        struct {
            char     *name;
            NodeList *args;
        } call;

        /*
         * ND_IDENT
         *   name — identifier spelling (heap-allocated).
         */
        struct {
            char *name;
        } ident;

        /*
         * ND_INT_LIT
         *   value — the integer value.
         */
        struct {
            int value;
        } int_lit;

        /*
         * ND_STR_LIT
         *   value — string contents without surrounding quotes
         *           (heap-allocated, NUL-terminated).
         */
        struct {
            char *value;
        } str_lit;
    };
};

/* ------------------------------------------------------------------ */
/*  Node / NodeList API                                                 */
/* ------------------------------------------------------------------ */

/* Allocate a zeroed Node with the given kind. */
Node     *node_new(NodeKind kind);

/* Append `node` to the end of `list` and return the (possibly new) head. */
NodeList *node_list_append(NodeList *list, Node *node);

/* Recursively free a Node and all its children / NodeLists. */
void      node_free(Node *node);

#endif /* AST_H */
