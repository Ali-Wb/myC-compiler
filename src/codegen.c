/*
 * codegen.c — x86-64 AT&T-syntax code generator.
 *
 * Calling convention: System V AMD64 ABI.
 *   Integer args (in order): %rdi %rsi %rdx %rcx %r8 %r9
 *   Return value:             %rax
 *   Caller-saved: %rax %rcx %rdx %rsi %rdi %r8-%r11
 *   Callee-saved: %rbx %rbp %r12-%r15
 *
 * Stack frame layout per function:
 *
 *   [higher address]
 *   return address        ← pushed by call instruction
 *   saved %rbp            ← pushed by prologue (pushq %rbp)
 *   %rbp points here  ─┐
 *   param 1  -8(%rbp)  │  spilled from %rdi
 *   param 2 -16(%rbp)  │  spilled from %rsi …
 *   local 1 -N(%rbp)   │  assigned by symtable_define()
 *   …                  │
 *   %rsp points here  ─┘  (after subq $frame, %rsp)
 *   [lower address]
 *
 * Expression results are left in %rax.  Binary operators save the LHS
 * on the stack (pushq/popq) so both sides can be live simultaneously.
 */

#include "codegen.h"

/* ------------------------------------------------------------------ */
/*  String literal pre-collection                                       */
/* ------------------------------------------------------------------ */

/*
 * All ND_STR_LIT nodes are registered before any .text is emitted so
 * that the .rodata section can be written at the top of the output file.
 * register_string() deduplicates: the same content always gets the same
 * label id.
 */
#define MAX_STRINGS 256

typedef struct { char *value; int id; } StringEntry;

static StringEntry str_table[MAX_STRINGS];
static int         str_count = 0;

static int register_string(const char *value)
{
    for (int i = 0; i < str_count; i++)
        if (!strcmp(str_table[i].value, value))
            return str_table[i].id;

    if (str_count >= MAX_STRINGS)
        dief("too many string literals (max %d)", MAX_STRINGS);

    str_table[str_count].value = strdup(value);
    if (!str_table[str_count].value) die("out of memory for string literal");
    str_table[str_count].id = str_count;
    return str_count++;
}

/* Walk the AST and register every ND_STR_LIT before any code is emitted. */
static void collect_strings(Node *n)
{
    if (!n) return;
    switch (n->kind) {
    case ND_STR_LIT:  register_string(n->str_lit.value);                    break;
    case ND_PROGRAM:  for (NodeList *l = n->program.funcs; l; l = l->next)
                          collect_strings(l->node);                          break;
    case ND_FUNC:     collect_strings(n->func.body);                        break;
    case ND_BLOCK:    for (NodeList *l = n->block.stmts; l; l = l->next)
                          collect_strings(l->node);                          break;
    case ND_VAR_DECL: collect_strings(n->var_decl.init);                    break;
    case ND_RETURN:   collect_strings(n->ret.expr);                         break;
    case ND_IF:       collect_strings(n->if_.cond);
                      collect_strings(n->if_.then_);
                      collect_strings(n->if_.else_);                        break;
    case ND_WHILE:    collect_strings(n->while_.cond);
                      collect_strings(n->while_.body);                      break;
    case ND_FOR:      collect_strings(n->for_.init);
                      collect_strings(n->for_.cond);
                      collect_strings(n->for_.step);
                      collect_strings(n->for_.body);                        break;
    case ND_ASSIGN:   collect_strings(n->assign.rhs);                       break;
    case ND_BINOP:    collect_strings(n->binop.lhs);
                      collect_strings(n->binop.rhs);                        break;
    case ND_UNARY:    collect_strings(n->unary.operand);                    break;
    case ND_CALL:     for (NodeList *l = n->call.args; l; l = l->next)
                          collect_strings(l->node);                          break;
    default:          break;
    }
}

/*
 * fprint_asm_string — write `s` as a GAS-safe quoted string to `out`.
 *
 * The lexer decoded escape sequences (e.g. \n → 0x0A) when it built the
 * token.  We must re-encode them here so that the .string directive does
 * not contain raw control characters or unescaped quotes, which would
 * confuse the assembler.
 */
static void fprint_asm_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n",  out); break;
        case '\t': fputs("\\t",  out); break;
        case '\r': fputs("\\r",  out); break;
        default:
            if (c < 0x20 || c == 0x7f)
                fprintf(out, "\\%03o", c);   /* octal escape for other controls */
            else
                fputc(c, out);
            break;
        }
    }
    fputc('"', out);
}

static void emit_rodata_section(FILE *out)
{
    if (str_count == 0) return;
    fprintf(out, ".section .rodata\n");
    for (int i = 0; i < str_count; i++) {
        fprintf(out, ".Lstr%d:\n\t.string ", str_table[i].id);
        fprint_asm_string(out, str_table[i].value);
        fputc('\n', out);
    }
    /* Caller is responsible for switching back to .text. */
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static int new_label(Codegen *cg) { return cg->label_cnt++; }

#define EMIT(cg, fmt, ...)  fprintf((cg)->out, "\t" fmt "\n", ##__VA_ARGS__)
#define LABEL(cg, id)       fprintf((cg)->out, ".L%d:\n", (id))

static const char *arg_regs[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
#define MAX_REG_ARGS 6

/*
 * count_locals — count all ND_VAR_DECL nodes reachable from n.
 * Only recurses into statement positions where declarations can appear.
 * Never recurses into expressions (ND_VAR_DECL cannot appear there).
 */
static int count_locals(Node *n)
{
    if (!n) return 0;
    switch (n->kind) {
    case ND_VAR_DECL: return 1;
    case ND_BLOCK: {
        int c = 0;
        for (NodeList *l = n->block.stmts; l; l = l->next)
            c += count_locals(l->node);
        return c;
    }
    case ND_IF:    return count_locals(n->if_.then_)  + count_locals(n->if_.else_);
    case ND_WHILE: return count_locals(n->while_.body);
    case ND_FOR:   return count_locals(n->for_.init)  + count_locals(n->for_.body);
    default:       return 0;
    }
}

/* Round size up to the nearest multiple of 16. */
static int align16(int size) { return (size + 15) & ~15; }

/* ------------------------------------------------------------------ */
/*  Expression emitter  (result left in %rax)                          */
/* ------------------------------------------------------------------ */

static void emit_expr(Codegen *cg, Node *n);

static void emit_call(Codegen *cg, Node *n)
{
    /*
     * Evaluate each argument left-to-right, placing results into the
     * SysV integer argument registers.  This is correct for simple
     * arguments; complex arguments that themselves contain calls would
     * clobber earlier registers — a known limitation of this simple
     * linear allocation strategy.
     */
    int   argc = 0;
    Node *argv_buf[MAX_REG_ARGS];
    for (NodeList *l = n->call.args; l && argc < MAX_REG_ARGS; l = l->next)
        argv_buf[argc++] = l->node;

    for (int i = 0; i < argc; i++) {
        emit_expr(cg, argv_buf[i]);
        EMIT(cg, "movq %%rax, %s", arg_regs[i]);
    }

    /*
     * SysV AMD64 requires %rsp to be 16-byte aligned at the point of call.
     * The prologue leaves %rsp aligned, but each ND_BINOP pushq shifts it
     * by 8.  If push_depth is odd we are 8 bytes off — pad with a dummy
     * subq so the callee sees a properly aligned stack.
     */
    int needs_align = (cg->push_depth % 2 != 0);
    if (needs_align)
        EMIT(cg, "subq $8, %%rsp");

    EMIT(cg, "xorl %%eax, %%eax");   /* 0 XMM args — required for variadic calls */
    EMIT(cg, "call %s", n->call.name);

    if (needs_align)
        EMIT(cg, "addq $8, %%rsp");
}

static void emit_expr(Codegen *cg, Node *n)
{
    switch (n->kind) {

    /* ── Integer literal ──────────────────────────────────────── */
    case ND_INT_LIT:
        EMIT(cg, "movq $%d, %%rax", n->int_lit.value);
        break;

    /* ── String literal — address of pre-collected .rodata label ── */
    case ND_STR_LIT: {
        int id = register_string(n->str_lit.value);  /* always deduplicates */
        EMIT(cg, "leaq .Lstr%d(%%rip), %%rax", id);
        break;
    }

    /* ── Variable read — load from its stack slot ─────────────── */
    case ND_IDENT: {
        Symbol *sym = symtable_lookup(cg->st, n->ident.name);
        if (!sym) dief("codegen: undefined variable '%s'", n->ident.name);
        EMIT(cg, "movq %d(%%rbp), %%rax", sym->stack_offset);
        break;
    }

    /* ── Assignment — evaluate RHS, store to lvalue's stack slot ─ */
    case ND_ASSIGN: {
        emit_expr(cg, n->assign.rhs);
        Symbol *sym = symtable_lookup(cg->st, n->assign.lhs->ident.name);
        if (!sym) dief("codegen: undefined variable '%s'", n->assign.lhs->ident.name);
        EMIT(cg, "movq %%rax, %d(%%rbp)", sym->stack_offset);
        /* %rax still holds the assigned value — correct for chained assignment */
        break;
    }

    /* ── Binary operators ─────────────────────────────────────── */
    case ND_BINOP: {
        /*
         * Evaluate LHS → push onto stack.
         * Evaluate RHS → %rax.
         * Move RHS to %rcx; pop LHS back to %rax.
         * Operate: %rax = LHS op RHS.
         */
        emit_expr(cg, n->binop.lhs);
        EMIT(cg, "pushq %%rax");
        cg->push_depth++;
        emit_expr(cg, n->binop.rhs);
        EMIT(cg, "movq %%rax, %%rcx");   /* rcx = rhs */
        EMIT(cg, "popq %%rax");          /* rax = lhs */
        cg->push_depth--;

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

    /* ── Unary operators ──────────────────────────────────────── */
    case ND_UNARY: {
        emit_expr(cg, n->unary.operand);
        const char *op = n->unary.op;
        if      (!strcmp(op, "-")) EMIT(cg, "negq %%rax");
        else if (!strcmp(op, "!")) {
            EMIT(cg, "testq %%rax, %%rax");
            EMIT(cg, "sete %%al");
            EMIT(cg, "movzbq %%al, %%rax");
        }
        else dief("unknown unary operator '%s'", op);
        break;
    }

    /* ── Function call ────────────────────────────────────────── */
    case ND_CALL:
        emit_call(cg, n);
        break;

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

    /*
     * Block — push a new scope, emit children in declaration order,
     * then pop the scope.  A later statement can see an earlier
     * declaration; earlier statements cannot see later ones.
     */
    case ND_BLOCK:
        symtable_enter_scope(cg->st);
        for (NodeList *l = n->block.stmts; l; l = l->next)
            emit_stmt(cg, l->node);
        symtable_exit_scope(cg->st);
        break;

    /*
     * Variable declaration — claim the next stack slot (symtable_define
     * decrements cg->st->stack_offset by 8).  If there is an initialiser,
     * evaluate it and store the result into the new slot.
     */
    case ND_VAR_DECL: {
        Symbol *sym = symtable_define(cg->st, n->var_decl.name,
                                              n->var_decl.type_name);
        if (n->var_decl.init) {
            emit_expr(cg, n->var_decl.init);
            EMIT(cg, "movq %%rax, %d(%%rbp)", sym->stack_offset);
        }
        break;
    }

    /* return expr? — restore the frame and return to caller. */
    case ND_RETURN:
        if (n->ret.expr) emit_expr(cg, n->ret.expr);
        EMIT(cg, "leave");   /* movq %rbp,%rsp  ;  popq %rbp */
        EMIT(cg, "ret");
        break;

    /* if (cond) then else? */
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

    /* while (cond) body */
    case ND_WHILE: {
        int loop_lbl = new_label(cg);
        int end_lbl  = new_label(cg);
        LABEL(cg, loop_lbl);
        emit_expr(cg, n->while_.cond);
        EMIT(cg, "testq %%rax, %%rax");
        EMIT(cg, "je .L%d", end_lbl);
        emit_stmt(cg, n->while_.body);
        EMIT(cg, "jmp .L%d", loop_lbl);
        LABEL(cg, end_lbl);
        break;
    }

    /*
     * for (init; cond; step) body
     *
     * A dedicated for-header scope lets  int i = 0  in the init clause
     * be visible through the whole loop but invisible after it.
     * The body's ND_BLOCK opens an inner scope on top of this one.
     */
    case ND_FOR: {
        int loop_lbl = new_label(cg);
        int end_lbl  = new_label(cg);

        symtable_enter_scope(cg->st);   /* for-header scope */

        if (n->for_.init) {
            if (n->for_.init->kind == ND_VAR_DECL)
                emit_stmt(cg, n->for_.init);   /* defines into for-scope */
            else
                emit_expr(cg, n->for_.init);   /* plain expression       */
        }

        LABEL(cg, loop_lbl);
        if (n->for_.cond) {
            emit_expr(cg, n->for_.cond);
            EMIT(cg, "testq %%rax, %%rax");
            EMIT(cg, "je .L%d", end_lbl);
        }
        emit_stmt(cg, n->for_.body);           /* body opens its own scope */
        if (n->for_.step) emit_expr(cg, n->for_.step);
        EMIT(cg, "jmp .L%d", loop_lbl);
        LABEL(cg, end_lbl);

        symtable_exit_scope(cg->st);    /* close for-header scope */
        break;
    }

    /* Expression used as a statement — evaluate for side effects. */
    case ND_CALL:
    case ND_ASSIGN:
    case ND_BINOP:
    case ND_UNARY:
    case ND_IDENT:
        emit_expr(cg, n);
        break;

    default:
        dief("cannot emit statement for node kind %d", (int)n->kind);
    }
}

/* ------------------------------------------------------------------ */
/*  Function emitter                                                    */
/* ------------------------------------------------------------------ */

static void emit_func(Codegen *cg, Node *fn)
{
    /* Reset per-function state. */
    cg->st->stack_offset = 0;
    cg->push_depth = 0;

    /*
     * Count every stack slot this function will need:
     *   params  — spilled from argument registers on entry
     *   locals  — all ND_VAR_DECL nodes anywhere in the body
     * Multiply by 8 bytes per slot and round up to 16-byte ABI boundary
     * so %rsp is always 16-byte aligned before any call instruction.
     */
    int param_count = 0;
    for (NodeList *l = fn->func.params; l; l = l->next)
        param_count++;
    int frame = align16((param_count + count_locals(fn->func.body)) * 8);

    fprintf(cg->out, ".globl %s\n", fn->func.name);
    fprintf(cg->out, ".type %s, @function\n", fn->func.name);
    fprintf(cg->out, "%s:\n", fn->func.name);

    /* ── Prologue ─────────────────────────────────────────────── */
    EMIT(cg, "pushq %%rbp");
    EMIT(cg, "movq %%rsp, %%rbp");
    if (frame > 0)
        EMIT(cg, "subq $%d, %%rsp", frame);

    /*
     * Parameter scope: define each param in the symbol table (which
     * assigns its stack slot) then spill its register value there.
     * The body's ND_BLOCK will open a nested inner scope on top of this.
     */
    symtable_enter_scope(cg->st);
    int pi = 0;
    for (NodeList *l = fn->func.params; l && pi < MAX_REG_ARGS; l = l->next, pi++) {
        Node   *p   = l->node;
        Symbol *sym = symtable_define(cg->st, p->var_decl.name,
                                              p->var_decl.type_name);
        EMIT(cg, "movq %s, %d(%%rbp)", arg_regs[pi], sym->stack_offset);
    }

    emit_stmt(cg, fn->func.body);

    symtable_exit_scope(cg->st);   /* close parameter scope */

    /* ── Implicit return 0 (fall-off-end) ────────────────────── */
    EMIT(cg, "xorl %%eax, %%eax");
    EMIT(cg, "leave");   /* movq %rbp,%rsp  ;  popq %rbp */
    EMIT(cg, "ret");
    fprintf(cg->out, ".size %s, .-%s\n\n", fn->func.name, fn->func.name);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void codegen_init(Codegen *cg, FILE *out)
{
    memset(cg, 0, sizeof *cg);
    cg->out = out;
    cg->st  = symtable_new();
}

void codegen_emit(Codegen *cg, Node *root)
{
    if (root->kind != ND_PROGRAM) die("codegen_emit: expected ND_PROGRAM");

    /* Reset string table for this compilation run. */
    str_count = 0;

    /* Pre-scan: register all string literals before emitting any code. */
    collect_strings(root);

    /* .rodata section (possibly empty) then .text section. */
    emit_rodata_section(cg->out);
    fprintf(cg->out, ".text\n");

    for (NodeList *l = root->program.funcs; l; l = l->next)
        emit_func(cg, l->node);

    /* Mark stack non-executable (required on hardened Linux systems). */
    fprintf(cg->out, ".section .note.GNU-stack,\"\",@progbits\n");
}

/*
 * Standalone wrappers — emit a single node using a caller-supplied
 * SymTable.  Useful for unit-testing individual codegen paths.
 * These do NOT emit the .rodata section; callers that use string
 * literals must arrange that separately.
 */

void codegen_expr(Node *node, SymTable *st, FILE *out)
{
    if (!node || !st || !out) return;
    Codegen cg = {0};
    cg.out = out;
    cg.st  = st;
    emit_expr(&cg, node);
}

void codegen_if(Node *node, SymTable *st, FILE *out)
{
    if (!node || !st || !out || node->kind != ND_IF) return;
    Codegen cg = {0};
    cg.out = out;
    cg.st  = st;
    emit_stmt(&cg, node);
}

void codegen_while(Node *node, SymTable *st, FILE *out)
{
    if (!node || !st || !out || node->kind != ND_WHILE) return;
    Codegen cg = {0};
    cg.out = out;
    cg.st  = st;
    emit_stmt(&cg, node);
}

void codegen_for(Node *node, SymTable *st, FILE *out)
{
    if (!node || !st || !out || node->kind != ND_FOR) return;
    Codegen cg = {0};
    cg.out = out;
    cg.st  = st;
    emit_stmt(&cg, node);
}

void codegen_function(Node *node, FILE *out)
{
    if (!node || !out || node->kind != ND_FUNC) return;
    Codegen cg;
    codegen_init(&cg, out);   /* allocates a fresh SymTable */
    emit_func(&cg, node);
}

void codegen_call(Node *node, SymTable *st, FILE *out)
{
    if (!node || !st || !out || node->kind != ND_CALL) return;
    Codegen cg = {0};
    cg.out = out;
    cg.st  = st;
    emit_call(&cg, node);
}
