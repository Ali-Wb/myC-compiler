/*
 * parser.c — recursive-descent parser.
 *
 * Full grammar handled:
 *
 *   program       ::= function*
 *   function      ::= type IDENT '(' params ')' block
 *   params        ::= (type IDENT (',' type IDENT)*)?
 *
 *   block         ::= '{' statement* '}'
 *   statement     ::= var_decl
 *                   | return_stmt
 *                   | if_stmt
 *                   | while_stmt
 *                   | for_stmt
 *                   | block
 *                   | assign_expr ';'
 *
 *   var_decl      ::= type IDENT ('=' expression)? ';'
 *   return_stmt   ::= 'return' expression? ';'
 *   if_stmt       ::= 'if' '(' expression ')' statement ('else' statement)?
 *   while_stmt    ::= 'while' '(' expression ')' statement
 *   for_stmt      ::= 'for' '(' for_init ';' expression? ';' assign_expr? ')' statement
 *   for_init      ::= var_decl_head | assign_expr | ε
 *   var_decl_head ::= type IDENT ('=' expression)?    -- no semicolon
 *
 *   assign_expr   ::= expression ('=' assign_expr)?   -- right-associative
 *
 *   expression    ::= logical_or
 *   logical_or    ::= logical_and  ('||' logical_and)*
 *   logical_and   ::= equality     ('&&' equality)*
 *   equality      ::= comparison   (('=='|'!=') comparison)*
 *   comparison    ::= additive     (('<'|'>'|'<='|'>=') additive)*
 *   additive      ::= multiplicative (('+' | '-') multiplicative)*
 *   multiplicative::= unary        (('*'|'/'|'%') unary)*
 *   unary         ::= ('-'|'!') unary | primary
 *   primary       ::= INT_LIT | STR_LIT
 *                   | IDENT '(' args ')'
 *                   | IDENT
 *                   | '(' expression ')'
 */

#include "parser.h"

/* ================================================================== */
/*  Global scanner state                                               */
/* ================================================================== */

static Token *g_tokens;  /* full token array produced by tokenize()    */
static int    g_count;   /* total tokens including the TK_EOF sentinel  */
static int    g_pos;     /* index of the current (next unread) token    */

/* ================================================================== */
/*  Scanner helpers                                                    */
/* ================================================================== */

/*
 * peek() — return the current token without consuming it.
 * Safe to call at any time: clamped to the TK_EOF sentinel.
 */
static Token *peek(void)
{
    if (g_pos >= g_count) return &g_tokens[g_count - 1];
    return &g_tokens[g_pos];
}

/*
 * consume() — return the current token and advance the cursor by one.
 * Never steps past the TK_EOF sentinel.
 */
static Token *consume(void)
{
    Token *t = peek();
    if (t->type != TK_EOF) g_pos++;
    return t;
}

/*
 * expect(type, what) — like consume(), but calls die() if the current
 * token is not of the expected type.  `what` is a human-readable name
 * for the expected token, used in the error message.
 */
static Token *expect(TokenType type, const char *what)
{
    Token *t = peek();
    if (t->type != type)
        dief("expected %s on line %d (got '%s')",
             what, t->line, t->value ? t->value : "EOF");
    return consume();
}

/*
 * match(type) — if the current token matches `type`, consume it and
 * return 1; otherwise leave the cursor alone and return 0.
 */
static int match(TokenType type)
{
    if (peek()->type == type) { consume(); return 1; }
    return 0;
}

/* ================================================================== */
/*  AST construction helpers                                           */
/* ================================================================== */

/* Allocate an ND_BINOP node; takes ownership of the heap string `op`. */
static Node *make_binop(char *op, Node *lhs, Node *rhs)
{
    Node *n    = node_new(ND_BINOP);
    n->binop.op  = op;
    n->binop.lhs = lhs;
    n->binop.rhs = rhs;
    return n;
}

/* ================================================================== */
/*  Forward declarations                                               */
/* ================================================================== */

Node *parse_expression(void);        /* used inside parse_primary     */
static Node *parse_statement(void);  /* used inside block / if / while / for */

/* ================================================================== */
/*  Expression layer                                                   */
/* ================================================================== */

/*
 * parse_primary — highest-precedence atomic expressions.
 *
 *   INT_LIT               → ND_INT_LIT
 *   STR_LIT               → ND_STR_LIT   (value pre-decoded by lexer)
 *   IDENT '(' args ')'    → ND_CALL
 *   IDENT                 → ND_IDENT
 *   '(' expression ')'   → inner node (no wrapper node)
 */
static Node *parse_primary(void)
{
    Token *t = peek();

    if (t->type == TK_INT_LIT) {
        consume();
        Node *n          = node_new(ND_INT_LIT);
        n->int_lit.value = (int)strtol(t->value, NULL, 10);
        return n;
    }

    if (t->type == TK_STR_LIT) {
        consume();
        Node *n          = node_new(ND_STR_LIT);
        n->str_lit.value = strdup(t->value);
        return n;
    }

    if (t->type == TK_IDENT) {
        consume();
        /* Function call: IDENT '(' arg* ')' */
        if (peek()->type == TK_LPAREN) {
            consume();  /* eat '(' */
            Node *call      = node_new(ND_CALL);
            call->call.name = strdup(t->value);
            call->call.args = NULL;
            if (peek()->type != TK_RPAREN) {
                do {
                    call->call.args =
                        node_list_append(call->call.args, parse_expression());
                } while (match(TK_COMMA));
            }
            expect(TK_RPAREN, "')'");
            return call;
        }
        /* Plain variable reference */
        Node *n       = node_new(ND_IDENT);
        n->ident.name = strdup(t->value);
        return n;
    }

    if (t->type == TK_LPAREN) {
        consume();  /* eat '(' */
        Node *inner = parse_expression();
        expect(TK_RPAREN, "')'");
        return inner;
    }

    dief("unexpected token '%s' on line %d",
         t->value ? t->value : "EOF", t->line);
}

/*
 * parse_unary — prefix - and ! operators, right-recursive.
 *
 *   '-' unary  →  ND_UNARY { op="-", operand }
 *   '!' unary  →  ND_UNARY { op="!", operand }
 *   otherwise  →  parse_primary()
 */
static Node *parse_unary(void)
{
    Token *t = peek();
    if (t->type == TK_MINUS || t->type == TK_BANG) {
        consume();
        Node *n          = node_new(ND_UNARY);
        n->unary.op      = strdup(t->value);
        n->unary.operand = parse_unary();   /* recurse: !!x, --x, etc. */
        return n;
    }
    return parse_primary();
}

/* parse_multiplicative — left-associative  * / % */
static Node *parse_multiplicative(void)
{
    Node *left = parse_unary();
    while (peek()->type == TK_STAR  ||
           peek()->type == TK_SLASH ||
           peek()->type == TK_PERCENT) {
        char *op = strdup(consume()->value);
        left = make_binop(op, left, parse_unary());
    }
    return left;
}

/* parse_additive — left-associative  + - */
static Node *parse_additive(void)
{
    Node *left = parse_multiplicative();
    while (peek()->type == TK_PLUS || peek()->type == TK_MINUS) {
        char *op = strdup(consume()->value);
        left = make_binop(op, left, parse_multiplicative());
    }
    return left;
}

/* parse_comparison — left-associative  < > <= >= */
static Node *parse_comparison(void)
{
    Node *left = parse_additive();
    while (peek()->type == TK_LT || peek()->type == TK_GT ||
           peek()->type == TK_LE || peek()->type == TK_GE) {
        char *op = strdup(consume()->value);
        left = make_binop(op, left, parse_additive());
    }
    return left;
}

/* parse_equality — left-associative  == != */
static Node *parse_equality(void)
{
    Node *left = parse_comparison();
    while (peek()->type == TK_EQEQ || peek()->type == TK_NEQ) {
        char *op = strdup(consume()->value);
        left = make_binop(op, left, parse_comparison());
    }
    return left;
}

/* parse_logical_and — left-associative  && */
static Node *parse_logical_and(void)
{
    Node *left = parse_equality();
    while (peek()->type == TK_AND) {
        char *op = strdup(consume()->value);
        left = make_binop(op, left, parse_equality());
    }
    return left;
}

/* parse_logical_or — left-associative  || */
static Node *parse_logical_or(void)
{
    Node *left = parse_logical_and();
    while (peek()->type == TK_OR) {
        char *op = strdup(consume()->value);
        left = make_binop(op, left, parse_logical_and());
    }
    return left;
}

/*
 * parse_expression — public expression entry point (declared in parser.h).
 *
 * Covers everything up to logical-or precedence.  Assignment is handled
 * separately in parse_assign_expr() so that '=' is not treated as an
 * infix operator in arbitrary expression positions.
 */
Node *parse_expression(void)
{
    return parse_logical_or();
}

/*
 * parse_assign_expr — right-associative assignment, private to the
 * statement layer.
 *
 *   expression ('=' assign_expr)?
 *
 * Only called from parse_var_decl, parse_return, parse_for (step clause),
 * and the expression-statement fallback in parse_statement.
 */
static Node *parse_assign_expr(void)
{
    Node *left = parse_expression();
    if (peek()->type == TK_EQ) {
        consume();  /* eat '=' */
        Node *n       = node_new(ND_ASSIGN);
        n->assign.lhs = left;
        n->assign.rhs = parse_assign_expr();   /* right-associative */
        return n;
    }
    return left;
}

/* ================================================================== */
/*  Statement layer — one function per construct                       */
/* ================================================================== */

/* Predicate: is the current token a type keyword? */
static int is_type_kw(TokenType t)
{
    return t == TK_INT || t == TK_CHAR || t == TK_VOID;
}

/* ------------------------------------------------------------------ */

/*
 * parse_block — parses { statement* } and returns an ND_BLOCK node
 * whose stmts list contains the child statement nodes in order.
 *
 *   block ::= '{' statement* '}'
 */
static Node *parse_block(void)
{
    expect(TK_LBRACE, "'{'");

    Node *block        = node_new(ND_BLOCK);
    block->block.stmts = NULL;

    while (peek()->type != TK_RBRACE && peek()->type != TK_EOF)
        block->block.stmts =
            node_list_append(block->block.stmts, parse_statement());

    expect(TK_RBRACE, "'}'");
    return block;
}

/* ------------------------------------------------------------------ */

/*
 * parse_var_decl — parses a local variable declaration statement.
 * Consumes the trailing semicolon.
 *
 *   var_decl ::= type IDENT ('=' expression)? ';'
 *
 * Returns ND_VAR_DECL.
 */
static Node *parse_var_decl(void)
{
    Token *type_tok = consume();                      /* int / char / void  */
    Token *name_tok = expect(TK_IDENT, "variable name");

    Node *n               = node_new(ND_VAR_DECL);
    n->var_decl.type_name = strdup(type_tok->value);
    n->var_decl.name      = strdup(name_tok->value);
    n->var_decl.init      = match(TK_EQ) ? parse_expression() : NULL;

    expect(TK_SEMI, "';'");
    return n;
}

/* ------------------------------------------------------------------ */

/*
 * parse_return — parses a return statement.
 *
 *   return_stmt ::= 'return' expression? ';'
 *
 * The expression is optional: bare `return;` sets ret.expr to NULL.
 * Returns ND_RETURN.
 */
static Node *parse_return(void)
{
    expect(TK_RETURN, "'return'");

    Node *n     = node_new(ND_RETURN);
    n->ret.expr = (peek()->type == TK_SEMI) ? NULL : parse_assign_expr();

    expect(TK_SEMI, "';'");
    return n;
}

/* ------------------------------------------------------------------ */

/*
 * parse_if — parses an if statement with an optional else branch.
 *
 *   if_stmt ::= 'if' '(' expression ')' statement ('else' statement)?
 *
 * The bodies can be any statement (a bare block { } is also a statement).
 * Returns ND_IF.
 */
static Node *parse_if(void)
{
    expect(TK_IF, "'if'");
    expect(TK_LPAREN, "'('");

    Node *n     = node_new(ND_IF);
    n->if_.cond = parse_expression();

    expect(TK_RPAREN, "')'");

    n->if_.then_ = parse_statement();
    n->if_.else_ = match(TK_ELSE) ? parse_statement() : NULL;

    return n;
}

/* ------------------------------------------------------------------ */

/*
 * parse_while — parses a while loop.
 *
 *   while_stmt ::= 'while' '(' expression ')' statement
 *
 * Returns ND_WHILE.
 */
static Node *parse_while(void)
{
    expect(TK_WHILE, "'while'");
    expect(TK_LPAREN, "'('");

    Node *n        = node_new(ND_WHILE);
    n->while_.cond = parse_expression();

    expect(TK_RPAREN, "')'");

    n->while_.body = parse_statement();
    return n;
}

/* ------------------------------------------------------------------ */

/*
 * parse_for — parses a for loop.
 *
 *   for_stmt ::= 'for' '(' for_init ';' expression? ';' assign_expr? ')' statement
 *
 *   for_init is one of:
 *     - empty (next token is ';')
 *     - a variable declaration head: type IDENT ('=' expression)?
 *     - any assignment expression
 *
 * The init, cond, and step clauses are all optional (NULL when absent).
 * Returns ND_FOR.
 */
static Node *parse_for(void)
{
    expect(TK_FOR, "'for'");
    expect(TK_LPAREN, "'('");

    Node *n = node_new(ND_FOR);

    /* --- init clause --- */
    if (peek()->type == TK_SEMI) {
        n->for_.init = NULL;                    /* empty init            */
    } else if (is_type_kw(peek()->type)) {
        /* Variable declaration without trailing semicolon.
           We handle it inline instead of calling parse_var_decl() so
           we don't consume the ';' that belongs to the for header. */
        Token *type_tok = consume();
        Token *name_tok = expect(TK_IDENT, "variable name");
        Node  *decl               = node_new(ND_VAR_DECL);
        decl->var_decl.type_name  = strdup(type_tok->value);
        decl->var_decl.name       = strdup(name_tok->value);
        decl->var_decl.init       = match(TK_EQ) ? parse_expression() : NULL;
        n->for_.init = decl;
    } else {
        n->for_.init = parse_assign_expr();     /* e.g. i = 0           */
    }
    expect(TK_SEMI, "';'");

    /* --- condition clause --- */
    n->for_.cond = (peek()->type == TK_SEMI) ? NULL : parse_expression();
    expect(TK_SEMI, "';'");

    /* --- step clause --- */
    n->for_.step = (peek()->type == TK_RPAREN) ? NULL : parse_assign_expr();
    expect(TK_RPAREN, "')'");

    /* --- body --- */
    n->for_.body = parse_statement();
    return n;
}

/* ------------------------------------------------------------------ */

/*
 * parse_statement — dispatcher that routes to the correct parse_* function
 * based on the current token.
 *
 *   TK_RETURN      → parse_return()
 *   TK_IF          → parse_if()
 *   TK_WHILE       → parse_while()
 *   TK_FOR         → parse_for()
 *   TK_LBRACE      → parse_block()          (nested block)
 *   type keyword   → parse_var_decl()
 *   anything else  → assign_expr ';'        (expression statement)
 */
static Node *parse_statement(void)
{
    switch (peek()->type) {
    case TK_RETURN: return parse_return();
    case TK_IF:     return parse_if();
    case TK_WHILE:  return parse_while();
    case TK_FOR:    return parse_for();
    case TK_LBRACE: return parse_block();
    case TK_INT:
    case TK_CHAR:
    case TK_VOID:   return parse_var_decl();
    default: {
        /* Expression statement: any expression (call, assignment, etc.)
           used for its side effects, followed by a semicolon.         */
        Node *e = parse_assign_expr();
        expect(TK_SEMI, "';'");
        return e;
    }
    }
}

/* ================================================================== */
/*  Top-level                                                          */
/* ================================================================== */

/*
 * parse_function — parses one complete function definition.
 *
 *   function ::= type IDENT '(' params ')' block
 *   params   ::= (type IDENT (',' type IDENT)*)?
 *
 * Parameters are represented as ND_VAR_DECL nodes with init = NULL.
 * Returns ND_FUNC.
 */
static Node *parse_function(void)
{
    Token *ret_tok  = consume();              /* return-type keyword    */
    Token *name_tok = expect(TK_IDENT, "function name");

    Node *fn          = node_new(ND_FUNC);
    fn->func.ret_type = strdup(ret_tok->value);
    fn->func.name     = strdup(name_tok->value);
    fn->func.params   = NULL;

    expect(TK_LPAREN, "'('");

    if (peek()->type != TK_RPAREN) {
        do {
            if (!is_type_kw(peek()->type))
                dief("expected parameter type on line %d", peek()->line);

            Token *pty  = consume();
            Token *pnam = expect(TK_IDENT, "parameter name");

            Node *param               = node_new(ND_VAR_DECL);
            param->var_decl.type_name = strdup(pty->value);
            param->var_decl.name      = strdup(pnam->value);
            param->var_decl.init      = NULL;  /* params have no initialisers */

            fn->func.params = node_list_append(fn->func.params, param);
        } while (match(TK_COMMA));
    }

    expect(TK_RPAREN, "')'");
    fn->func.body = parse_block();
    return fn;
}

/* ------------------------------------------------------------------ */

/*
 * parse_program — top-level entry: parses a sequence of function
 * definitions until TK_EOF.
 *
 *   program ::= function*
 *
 * Returns ND_PROGRAM whose program.funcs list contains each ND_FUNC
 * node in source order.
 */
static Node *parse_program(void)
{
    Node *prog          = node_new(ND_PROGRAM);
    prog->program.funcs = NULL;

    while (peek()->type != TK_EOF) {
        if (!is_type_kw(peek()->type))
            dief("expected function return type on line %d", peek()->line);
        prog->program.funcs =
            node_list_append(prog->program.funcs, parse_function());
    }

    return prog;
}

/* ================================================================== */
/*  Public entry point                                                 */
/* ================================================================== */

/*
 * parse — initialise the global scanner state and parse the full program.
 *
 * tokens : array produced by tokenize()
 * count  : total token count (including TK_EOF sentinel)
 *
 * Returns a heap-allocated ND_PROGRAM root; caller owns it and must
 * eventually call node_free() on it.
 */
Node *parse(Token *tokens, int count)
{
    g_tokens = tokens;
    g_count  = count;
    g_pos    = 0;
    return parse_program();
}
