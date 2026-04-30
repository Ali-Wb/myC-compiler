#ifndef CODEGEN_H
#define CODEGEN_H

/*
 * codegen.h — x86-64 AT&T-syntax code generator interface.
 *
 * Walks the AST produced by the parser and emits assembly to an output
 * FILE*.  The generated file is then assembled and linked by gcc.
 *
 * Register allocation is deliberately simple: all temporaries go through
 * the stack (%rax for results, push/pop for sub-expressions), and
 * function arguments are placed in the SysV AMD64 integer registers.
 */

#include "ast.h"
#include "symtable.h"

typedef struct {
    FILE *out;          /* output stream for assembly text              */
    int   label_cnt;    /* monotonic counter for unique jump labels     */
    int   str_cnt;      /* counter for .rodata string literal labels    */
    /* A symbol table (stack-slot map) will be added here. */
} Codegen;

void codegen_init(Codegen *cg, FILE *out);
void codegen_emit(Codegen *cg, Node *root);           /* root must be ND_PROGRAM */
void codegen_expr(Node *node, SymTable *st, FILE *out);  /* evaluate expr, result in %rax */
void codegen_if(Node *node, SymTable *st, FILE *out);    /* emit if statement */
void codegen_while(Node *node, SymTable *st, FILE *out); /* emit while loop */
void codegen_for(Node *node, SymTable *st, FILE *out);   /* emit for loop */

#endif /* CODEGEN_H */
