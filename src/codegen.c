/*
 * codegen.c — x86-64 AT&T-syntax code generator.
 *
 * Calling convention: System V AMD64 ABI.
 *   Integer args (in order): %rdi %rsi %rdx %rcx %r8 %r9
 *   Return value: %rax
 *   Caller-saved: %rax %rcx %rdx %rsi %rdi %r8 %r9 %r10 %r11
 *   Callee-saved: %rbx %rbp %r12–%r15
 *
 * Stack frame per function:
 *   higher address:  return address   (pushed by call)
 *                    saved %rbp       (pushed by prologue)
 *   %rbp  →          [rbp-8]  first local
 *                    [rbp-16] second local  …
 *
 * Expression results are left in %rax.  Sub-expressions are saved on the
 * stack via push/pop when a binary operator needs both sides live.
 */

#include "codegen.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static int new_label(Codegen *cg) { return cg->label_cnt++; }

#define EMIT(cg, fmt, ...)  fprintf((cg)->out, "\t" fmt "\n", ##__VA_ARGS__)
#define LABEL(cg, id)       fprintf((cg)->out, ".L%d:\n", (id))

static const char *arg_regs[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
#define MAX_REG_ARGS 6

/* ------------------------------------------------------------------ */
/*  Expression emitter  (result in %rax)                               */
/* ------------------------------------------------------------------ */

static void emit_expr(Codegen *cg, Node *n);

static void emit_call(Codegen *cg, Node *n)
{
    /* Collect up to MAX_REG_ARGS argument nodes, then evaluate left→right. */
    int   argc = 0;
    Node *argv_buf[MAX_REG_ARGS];
    for (NodeList *l = n->call.args; l && argc < MAX_REG_ARGS; l = l->next)
        argv_buf[argc++] = l->node;

    for (int i = 0; i < argc; i++) {
        emit_expr(cg, argv_buf[i]);
        EMIT(cg, "movq %%rax, %s", arg_regs[i]);
    }

    EMIT(cg, "xorl %%eax, %%eax");   /* signal: 0 floating-point args */
    EMIT(cg, "call %s", n->call.name);
}

static void emit_expr(Codegen *cg, Node *n)
{
    switch (n->kind) {

    case ND_INT_LIT:
        EMIT(cg, "movq $%d, %%rax", n->int_lit.value);
        break;

    case ND_STR_LIT: {
        int id = cg->str_cnt++;
        fprintf(cg->out, ".section .rodata\n");
        fprintf(cg->out, ".LS%d:\n\t.string \"%s\"\n", id, n->str_lit.value);
        fprintf(cg->out, ".text\n");
        EMIT(cg, "leaq .LS%d(%%rip), %%rax", id);
        break;
    }

    case ND_IDENT:
        /* TODO: look up stack offset in symbol table and emit movq. */
        dief("variable reference '%s' not yet supported in codegen", n->ident.name);
        break;

    case ND_CALL:
        emit_call(cg, n);
        break;

    case ND_ASSIGN:
        /* TODO: resolve lvalue address, then store rhs. */
        emit_expr(cg, n->assign.rhs);
        dief("assignment codegen not yet implemented");
        break;

    case ND_BINOP: {
        /* Evaluate lhs → push; evaluate rhs → %rcx; operate on %rax/%rcx. */
        emit_expr(cg, n->binop.lhs);
        EMIT(cg, "pushq %%rax");
        emit_expr(cg, n->binop.rhs);
        EMIT(cg, "movq %%rax, %%rcx");
        EMIT(cg, "popq %%rax");

        const char *op = n->binop.op;
        if      (!strcmp(op, "+"))  EMIT(cg, "addq %%rcx, %%rax");
        else if (!strcmp(op, "-"))  EMIT(cg, "subq %%rcx, %%rax");
        else if (!strcmp(op, "*"))  EMIT(cg, "imulq %%rcx, %%rax");
        else if (!strcmp(op, "/"))  { EMIT(cg, "cqto"); EMIT(cg, "idivq %%rcx"); }
        else if (!strcmp(op, "%"))  { EMIT(cg, "cqto"); EMIT(cg, "idivq %%rcx"); EMIT(cg, "movq %%rdx, %%rax"); }
        else if (!strcmp(op, "==")) { EMIT(cg, "cmpq %%rcx, %%rax"); EMIT(cg, "sete %%al");  EMIT(cg, "movzbq %%al, %%rax"); }
        else if (!strcmp(op, "!=")) { EMIT(cg, "cmpq %%rcx, %%rax"); EMIT(cg, "setne %%al"); EMIT(cg, "movzbq %%al, %%rax"); }
        else if (!strcmp(op, "<"))  { EMIT(cg, "cmpq %%rcx, %%rax"); EMIT(cg, "setl %%al");  EMIT(cg, "movzbq %%al, %%rax"); }
        else if (!strcmp(op, "<=")) { EMIT(cg, "cmpq %%rcx, %%rax"); EMIT(cg, "setle %%al"); EMIT(cg, "movzbq %%al, %%rax"); }
        else if (!strcmp(op, ">"))  { EMIT(cg, "cmpq %%rcx, %%rax"); EMIT(cg, "setg %%al");  EMIT(cg, "movzbq %%al, %%rax"); }
        else if (!strcmp(op, ">=")) { EMIT(cg, "cmpq %%rcx, %%rax"); EMIT(cg, "setge %%al"); EMIT(cg, "movzbq %%al, %%rax"); }
        else if (!strcmp(op, "&&")) {
            EMIT(cg, "testq %%rax, %%rax"); EMIT(cg, "setne %%al");
            EMIT(cg, "testq %%rcx, %%rcx"); EMIT(cg, "setne %%cl");
            EMIT(cg, "andb %%cl, %%al");    EMIT(cg, "movzbq %%al, %%rax");
        }
        else if (!strcmp(op, "||")) {
            EMIT(cg, "orq %%rcx, %%rax");
            EMIT(cg, "setne %%al");
            EMIT(cg, "movzbq %%al, %%rax");
        }
        else dief("unknown binary operator '%s'", op);
        break;
    }

    case ND_UNARY: {
        emit_expr(cg, n->unary.operand);
        const char *op = n->unary.op;
        if      (!strcmp(op, "-")) EMIT(cg, "negq %%rax");
        else if (!strcmp(op, "!")) { EMIT(cg, "testq %%rax, %%rax"); EMIT(cg, "sete %%al"); EMIT(cg, "movzbq %%al, %%rax"); }
        else dief("unknown unary operator '%s'", op);
        break;
    }

    default:
        dief("cannot emit expression for node kind %d", (int)n->kind);
    }
}

/* ------------------------------------------------------------------ */
/*  Statement emitter                                                   */
/* ------------------------------------------------------------------ */

static void emit_stmt(Codegen *cg, Node *n)
{
    switch (n->kind) {

    case ND_RETURN:
        if (n->ret.expr) emit_expr(cg, n->ret.expr);
        EMIT(cg, "popq %%rbp");
        EMIT(cg, "ret");
        break;

    case ND_BLOCK:
        for (NodeList *l = n->block.stmts; l; l = l->next)
            emit_stmt(cg, l->node);
        break;

    case ND_IF: {
        int else_lbl = new_label(cg);
        int end_lbl  = new_label(cg);
        emit_expr(cg, n->if_.cond);
        EMIT(cg, "testq %%rax, %%rax");
        EMIT(cg, "je .L%d", else_lbl);
        emit_stmt(cg, n->if_.then_);
        EMIT(cg, "jmp .L%d", end_lbl);
        LABEL(cg, else_lbl);
        if (n->if_.else_) emit_stmt(cg, n->if_.else_);
        LABEL(cg, end_lbl);
        break;
    }

    case ND_WHILE: {
        int cond_lbl = new_label(cg);
        int end_lbl  = new_label(cg);
        LABEL(cg, cond_lbl);
        emit_expr(cg, n->while_.cond);
        EMIT(cg, "testq %%rax, %%rax");
        EMIT(cg, "je .L%d", end_lbl);
        emit_stmt(cg, n->while_.body);
        EMIT(cg, "jmp .L%d", cond_lbl);
        LABEL(cg, end_lbl);
        break;
    }

    case ND_FOR: {
        int cond_lbl = new_label(cg);
        int end_lbl  = new_label(cg);
        if (n->for_.init) emit_stmt(cg, n->for_.init);
        LABEL(cg, cond_lbl);
        if (n->for_.cond) {
            emit_expr(cg, n->for_.cond);
            EMIT(cg, "testq %%rax, %%rax");
            EMIT(cg, "je .L%d", end_lbl);
        }
        emit_stmt(cg, n->for_.body);
        if (n->for_.step) emit_expr(cg, n->for_.step);
        EMIT(cg, "jmp .L%d", cond_lbl);
        LABEL(cg, end_lbl);
        break;
    }

    case ND_VAR_DECL:
        /* TODO: allocate stack slot in symbol table; emit init if present. */
        break;

    /* Expression-as-statement: evaluate for side effects, discard %rax. */
    case ND_CALL:
    case ND_ASSIGN:
    case ND_BINOP:
    case ND_UNARY:
        emit_expr(cg, n);
        break;

    default:
        dief("cannot emit statement for node kind %d", (int)n->kind);
    }
}

/* ------------------------------------------------------------------ */
/*  Function and program                                                */
/* ------------------------------------------------------------------ */

static void emit_func(Codegen *cg, Node *fn)
{
    fprintf(cg->out, ".globl %s\n", fn->func.name);
    fprintf(cg->out, ".type %s, @function\n", fn->func.name);
    fprintf(cg->out, "%s:\n", fn->func.name);
    EMIT(cg, "pushq %%rbp");
    EMIT(cg, "movq %%rsp, %%rbp");

    emit_stmt(cg, fn->func.body);

    /* Implicit return 0 for functions that fall off the end. */
    EMIT(cg, "xorl %%eax, %%eax");
    EMIT(cg, "popq %%rbp");
    EMIT(cg, "ret");
    fprintf(cg->out, ".size %s, .-%s\n\n", fn->func.name, fn->func.name);
}

void codegen_init(Codegen *cg, FILE *out)
{
    memset(cg, 0, sizeof *cg);
    cg->out = out;
}

void codegen_emit(Codegen *cg, Node *root)
{
    if (root->kind != ND_PROGRAM) die("codegen_emit: expected ND_PROGRAM");
    fprintf(cg->out, ".text\n");
    for (NodeList *l = root->program.funcs; l; l = l->next)
        emit_func(cg, l->node);
}
