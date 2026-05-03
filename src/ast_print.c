/*
 * ast_print.c — recursive AST pretty-printer.
 *
 * Output format
 * -------------
 * Each node occupies one line, indented by (indent * 2) spaces.  The line
 * starts with the NodeKind label in UPPER_SNAKE_CASE.  For leaf-like
 * nodes the relevant value follows on the same line in square brackets.
 *
 * For nodes that have multiple distinct named children (IF/WHILE/FOR),
 * each slot gets a lower-case label line at indent+1 and its child tree
 * at indent+2.  This makes the tree shape unambiguous even when a child
 * is itself a complex subtree.
 *
 * Example — int main() { int x = 42; return x + 1; }
 *
 *   PROGRAM
 *     FUNC [main] -> int
 *       BLOCK
 *         VAR_DECL [int x]
 *           INT_LIT [42]
 *         RETURN
 *           BINOP [+]
 *             IDENT [x]
 *             INT_LIT [1]
 */

#include "ast_print.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Print (indent * 2) spaces — no newline. */
static void pad(int indent)
{
    for (int i = 0; i < indent * 2; i++) putchar(' ');
}

/*
 * slot(indent, name) — print a named child slot header.
 * Used for IF/WHILE/FOR where we need to label each sub-tree.
 *
 *   e.g.  slot(2, "cond")  prints "    cond:\n"
 */
static void slot(int indent, const char *name)
{
    pad(indent);
    printf("%s:\n", name);
}

/* Walk a NodeList and print each element at the given indent. */
static void print_list(NodeList *list, int indent)
{
    for (NodeList *l = list; l; l = l->next)
        print_ast(l->node, indent);
}

/* ------------------------------------------------------------------ */
/*  Main printer                                                        */
/* ------------------------------------------------------------------ */

void print_ast(Node *node, int indent)
{
    if (!node) {
        pad(indent);
        printf("(null)\n");
        return;
    }

    switch (node->kind) {

    /* ---- Top-level ---- */

    case ND_PROGRAM:
        pad(indent);
        printf("PROGRAM\n");
        print_list(node->program.funcs, indent + 1);
        break;

    case ND_FUNC:
        /*  FUNC [name] -> ret_type
         *    PARAM …
         *    PARAM …
         *    BLOCK … */
        pad(indent);
        { char tb[64]; printf("FUNC [%s] -> %s\n", node->func.name,
                              type_str(node->func.ret_type, tb, sizeof tb)); }
        if (node->func.params) {
            pad(indent + 1);
            printf("params:\n");
            print_list(node->func.params, indent + 2);
        }
        print_ast(node->func.body, indent + 1);
        break;

    case ND_BLOCK:
        pad(indent);
        printf("BLOCK\n");
        print_list(node->block.stmts, indent + 1);
        break;

    /* ---- Statements ---- */

    case ND_RETURN:
        pad(indent);
        printf("RETURN\n");
        if (node->ret.expr)
            print_ast(node->ret.expr, indent + 1);
        break;

    case ND_IF:
        /*  IF
         *    cond:
         *      <expr>
         *    then:
         *      <stmt>
         *    else:          (only when present)
         *      <stmt>       */
        pad(indent);
        printf("IF\n");
        slot(indent + 1, "cond");
        print_ast(node->if_.cond,  indent + 2);
        slot(indent + 1, "then");
        print_ast(node->if_.then_, indent + 2);
        if (node->if_.else_) {
            slot(indent + 1, "else");
            print_ast(node->if_.else_, indent + 2);
        }
        break;

    case ND_WHILE:
        /*  WHILE
         *    cond:
         *      <expr>
         *    body:
         *      <stmt>       */
        pad(indent);
        printf("WHILE\n");
        slot(indent + 1, "cond");
        print_ast(node->while_.cond, indent + 2);
        slot(indent + 1, "body");
        print_ast(node->while_.body, indent + 2);
        break;

    case ND_FOR:
        /*  FOR
         *    init:          (or "(none)" when absent)
         *      <stmt|expr>
         *    cond:
         *      <expr>
         *    step:
         *      <expr>
         *    body:
         *      <stmt>       */
        pad(indent);
        printf("FOR\n");
        slot(indent + 1, "init");
        if (node->for_.init)
            print_ast(node->for_.init, indent + 2);
        else { pad(indent + 2); printf("(none)\n"); }
        slot(indent + 1, "cond");
        if (node->for_.cond)
            print_ast(node->for_.cond, indent + 2);
        else { pad(indent + 2); printf("(none)\n"); }
        slot(indent + 1, "step");
        if (node->for_.step)
            print_ast(node->for_.step, indent + 2);
        else { pad(indent + 2); printf("(none)\n"); }
        slot(indent + 1, "body");
        print_ast(node->for_.body, indent + 2);
        break;

    case ND_VAR_DECL:
        /*  VAR_DECL [type name]
         *    <init-expr>    (only when there is an initialiser) */
        pad(indent);
        { char tb[64]; printf("VAR_DECL [%s %s]\n",
                              type_str(node->var_decl.type, tb, sizeof tb),
                              node->var_decl.name); }
        if (node->var_decl.init)
            print_ast(node->var_decl.init, indent + 1);
        break;

    /* ---- Expressions ---- */

    case ND_ASSIGN:
        /*  ASSIGN
         *    <lhs>
         *    <rhs>          */
        pad(indent);
        printf("ASSIGN\n");
        print_ast(node->assign.lhs, indent + 1);
        print_ast(node->assign.rhs, indent + 1);
        break;

    case ND_BINOP:
        /*  BINOP [op]
         *    <lhs>
         *    <rhs>          */
        pad(indent);
        printf("BINOP [%s]\n", node->binop.op);
        print_ast(node->binop.lhs, indent + 1);
        print_ast(node->binop.rhs, indent + 1);
        break;

    case ND_UNARY:
        /*  UNARY [op]
         *    <operand>      */
        pad(indent);
        printf("UNARY [%s]\n", node->unary.op);
        print_ast(node->unary.operand, indent + 1);
        break;

    case ND_CALL:
        /*  CALL [name]
         *    <arg0>
         *    <arg1> …       */
        pad(indent);
        printf("CALL [%s]\n", node->call.name);
        print_list(node->call.args, indent + 1);
        break;

    /* ---- Leaves ---- */

    case ND_IDENT:
        pad(indent);
        printf("IDENT [%s]\n", node->ident.name);
        break;

    case ND_INT_LIT:
        pad(indent);
        printf("INT_LIT [%d]\n", node->int_lit.value);
        break;

    case ND_STR_LIT:
        pad(indent);
        /* Print the value with C-style escaping so control chars are visible. */
        printf("STR_LIT [\"");
        for (const char *p = node->str_lit.value; *p; p++) {
            switch (*p) {
            case '\n': printf("\\n");  break;
            case '\t': printf("\\t");  break;
            case '\\': printf("\\\\"); break;
            case '"':  printf("\\\""); break;
            default:
                if ((unsigned char)*p < 0x20)
                    printf("\\x%02x", (unsigned char)*p);
                else
                    putchar(*p);
            }
        }
        printf("\"]\n");
        break;

    case ND_ADDR:
        pad(indent);
        printf("ADDR\n");
        print_ast(node->addr.operand, indent + 1);
        break;

    case ND_DEREF:
        pad(indent);
        printf("DEREF\n");
        print_ast(node->deref.operand, indent + 1);
        break;

    default:
        pad(indent);
        printf("??NODE kind=%d\n", (int)node->kind);
        break;
    }
}
