#ifndef SYMTABLE_H
#define SYMTABLE_H

/*
 * symtable.h — lexically-scoped symbol table for local variables.
 *
 * Scoping model
 * -------------
 * Scopes form a singly-linked chain from innermost to outermost.
 * symtable_enter_scope() pushes a new scope onto the front; symtable_exit_scope()
 * pops and frees it.  symtable_lookup() walks the chain outward until it
 * finds the name or runs out of scopes.
 *
 * Stack offsets
 * -------------
 * Every local variable is allocated a unique slot on the function's stack
 * frame.  SymTable.stack_offset starts at 0 and decreases by 8 with each
 * symtable_define() call, giving slots at -8(%rbp), -16(%rbp), etc.
 * The caller (sema / codegen) is responsible for resetting stack_offset
 * to 0 at the start of each new function.
 * After resolving a function the magnitude of stack_offset is the number
 * of bytes that must be subtracted from %rsp in the function prologue.
 */

#include "compiler.h"

/* ------------------------------------------------------------------ */
/*  Symbol                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char *name;          /* heap-allocated copy of the variable name   */
    char *type;          /* heap-allocated copy of the type spelling    */
    int   stack_offset;  /* negative offset from %rbp, e.g. -8, -16   */
} Symbol;

/* ------------------------------------------------------------------ */
/*  Scope — a single lexical block's symbol array                      */
/* ------------------------------------------------------------------ */

typedef struct Scope {
    Symbol      *syms;    /* growable array of symbols in this scope   */
    int          count;   /* number of symbols defined so far          */
    int          cap;     /* allocated capacity of syms[]              */
    struct Scope *parent; /* enclosing scope, or NULL at the top       */
} Scope;

/* ------------------------------------------------------------------ */
/*  SymTable — the whole table with its allocation counter             */
/* ------------------------------------------------------------------ */

typedef struct {
    Scope *current;      /* innermost (current) scope                  */
    int    stack_offset; /* next frame slot to hand out (0, -8, -16…) */
} SymTable;

/* ------------------------------------------------------------------ */
/*  API                                                                 */
/* ------------------------------------------------------------------ */

/* Allocate and return an empty symbol table (no scopes yet). */
SymTable *symtable_new(void);

/* Free the symbol table and all remaining scopes. */
void      symtable_free(SymTable *st);

/* Push a new, empty scope onto the scope stack. */
void      symtable_enter_scope(SymTable *st);

/* Pop the current scope, freeing its symbols. */
void      symtable_exit_scope(SymTable *st);

/*
 * Define a new symbol in the current scope.
 *
 * Assigns the next stack slot (decrements st->stack_offset by 8),
 * copies name and type, and appends to the current scope.
 *
 * Returns a pointer to the new Symbol on success.
 * Returns NULL if `name` is already defined in the CURRENT scope
 * (the caller should treat this as a "declared twice" error).
 */
Symbol   *symtable_define(SymTable *st, const char *name, const char *type);

/*
 * Look up `name` starting from the current scope and walking outward.
 * Returns a pointer to the Symbol if found, or NULL if not found in
 * any enclosing scope.
 */
Symbol   *symtable_lookup(SymTable *st, const char *name);

#endif /* SYMTABLE_H */
