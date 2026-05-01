#ifndef CODEGEN_H
#define CODEGEN_H

/*
 * codegen.h — x86-64 AT&T-syntax code generator interface.
 *
 * Walks the AST produced by the parser and emits assembly to an output
 * FILE*.  The generated file is then assembled and linked by gcc.
 *
 * Register allocation is deliberately simple: all temporaries go through
 * %rax (results) and push/pop (binary operator sub-expressions).
 * Function arguments follow the SysV AMD64 ABI (%rdi/%rsi/%rdx/…).
 */

#include "ast.h"
#include "symtable.h"

typedef struct {
    FILE     *out;        /* output stream for assembly text              */
    int       label_cnt;  /* monotonic counter for unique jump labels     */
    SymTable *st;         /* variable → stack-offset map, owned by Codegen */
    int       push_depth; /* outstanding pushq ops — tracks %rsp offset for call alignment */
} Codegen;

/* Initialise *cg and allocate its internal SymTable. */
void codegen_init(Codegen *cg, FILE *out);

/* Emit assembly for the entire ND_PROGRAM tree. */
void codegen_emit(Codegen *cg, Node *root);

/*
 * Standalone entry points — useful for testing individual nodes in
 * isolation.  They accept a caller-owned SymTable that must already
 * have the relevant symbols defined.
 */
void codegen_expr(Node *node, SymTable *st, FILE *out);
void codegen_if(Node *node, SymTable *st, FILE *out);
void codegen_while(Node *node, SymTable *st, FILE *out);
void codegen_for(Node *node, SymTable *st, FILE *out);
void codegen_function(Node *node, FILE *out);   /* manages its own SymTable */
void codegen_call(Node *node, SymTable *st, FILE *out);

#endif /* CODEGEN_H */
