/*
 * ast.c — Node allocator, NodeList builder, and recursive free.
 */

#include "ast.h"

Node *node_new(NodeKind kind, int line)
{
    Node *n = calloc(1, sizeof *n);
    if (!n) die("out of memory allocating Node");
    n->kind = kind;
    n->line = line;
    return n;
}

NodeList *node_list_append(NodeList *list, Node *node)
{
    NodeList *cell = malloc(sizeof *cell);
    if (!cell) die("out of memory allocating NodeList");
    cell->node = node;
    cell->next = NULL;

    if (!list) return cell;

    /* Walk to the tail so nodes stay in insertion order. */
    NodeList *l = list;
    while (l->next) l = l->next;
    l->next = cell;
    return list;
}

void node_free(Node *n)
{
    if (!n) return;

    switch (n->kind) {
    case ND_PROGRAM:
        for (NodeList *l = n->program.funcs; l; ) {
            NodeList *next = l->next;
            node_free(l->node);
            free(l);
            l = next;
        }
        break;

    case ND_FUNC:
        free(n->func.name);
        free(n->func.ret_type);
        for (NodeList *l = n->func.params; l; ) {
            NodeList *next = l->next;
            node_free(l->node);
            free(l);
            l = next;
        }
        node_free(n->func.body);
        break;

    case ND_BLOCK:
        for (NodeList *l = n->block.stmts; l; ) {
            NodeList *next = l->next;
            node_free(l->node);
            free(l);
            l = next;
        }
        break;

    case ND_RETURN:   node_free(n->ret.expr);                          break;
    case ND_IF:       node_free(n->if_.cond);
                      node_free(n->if_.then_);
                      node_free(n->if_.else_);                         break;
    case ND_WHILE:    node_free(n->while_.cond);
                      node_free(n->while_.body);                       break;
    case ND_FOR:      node_free(n->for_.init);
                      node_free(n->for_.cond);
                      node_free(n->for_.step);
                      node_free(n->for_.body);                         break;

    case ND_VAR_DECL: free(n->var_decl.type_name);
                      free(n->var_decl.name);
                      node_free(n->var_decl.init);                     break;

    case ND_ASSIGN:   node_free(n->assign.lhs);
                      node_free(n->assign.rhs);                        break;

    case ND_BINOP:    free(n->binop.op);
                      node_free(n->binop.lhs);
                      node_free(n->binop.rhs);                         break;

    case ND_UNARY:    free(n->unary.op);
                      node_free(n->unary.operand);                     break;

    case ND_CALL:
        free(n->call.name);
        for (NodeList *l = n->call.args; l; ) {
            NodeList *next = l->next;
            node_free(l->node);
            free(l);
            l = next;
        }
        break;

    case ND_IDENT:    free(n->ident.name);                             break;
    case ND_STR_LIT:  free(n->str_lit.value);                         break;
    case ND_INT_LIT:                                                   break;
    }

    free(n);
}
