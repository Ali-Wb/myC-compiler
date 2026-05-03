/*
 * symtable.c — lexically-scoped symbol table implementation.
 */

#include "symtable.h"

/* ------------------------------------------------------------------ */
/*  Internal scope helpers                                              */
/* ------------------------------------------------------------------ */

static Scope *scope_new(Scope *parent)
{
    Scope *s = calloc(1, sizeof *s);
    if (!s) die("out of memory allocating Scope");
    s->parent = parent;
    return s;
}

/* Free all symbols in a scope and the scope struct itself.
   Does NOT touch the parent. */
static void scope_free(Scope *s)
{
    for (int i = 0; i < s->count; i++) {
        free(s->syms[i].name);
        type_free(s->syms[i].type);
    }
    free(s->syms);
    free(s);
}

/* Return a pointer to the symbol named `name` within scope `s` only,
   or NULL if not present.  Does not look at parent scopes. */
static Symbol *scope_find(Scope *s, const char *name)
{
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->syms[i].name, name) == 0)
            return &s->syms[i];
    return NULL;
}

/* Append a symbol to `s`, growing the array if necessary.
   Takes ownership of `type`.  Returns a pointer to the newly added slot. */
static Symbol *scope_push(Scope *s, const char *name, Type *type, int offset)
{
    if (s->count == s->cap) {
        s->cap  = s->cap ? s->cap * 2 : 8;
        s->syms = realloc(s->syms, (size_t)s->cap * sizeof(Symbol));
        if (!s->syms) die("out of memory growing symbol array");
    }
    Symbol *sym       = &s->syms[s->count++];
    sym->name         = strdup(name);
    sym->type         = type;   /* ownership transferred */
    sym->stack_offset = offset;
    if (!sym->name) die("out of memory in symtable_define");
    return sym;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

SymTable *symtable_new(void)
{
    SymTable *st = calloc(1, sizeof *st);
    if (!st) die("out of memory allocating SymTable");
    /* stack_offset starts at 0; the first define will set it to -8. */
    return st;
}

void symtable_free(SymTable *st)
{
    while (st->current)
        symtable_exit_scope(st);
    free(st);
}

void symtable_enter_scope(SymTable *st)
{
    st->current = scope_new(st->current);
}

void symtable_exit_scope(SymTable *st)
{
    if (!st->current)
        die("symtable_exit_scope: no active scope to pop");
    Scope *parent = st->current->parent;
    scope_free(st->current);
    st->current = parent;
}

Symbol *symtable_define(SymTable *st, const char *name, Type *type)
{
    if (!st->current)
        die("symtable_define: called with no active scope");

    /* Duplicate in the current scope — return NULL so the caller can error. */
    if (scope_find(st->current, name))
        return NULL;

    /* Claim the next 8-byte slot below %rbp. */
    st->stack_offset -= 8;
    return scope_push(st->current, name, type, st->stack_offset);
}

Symbol *symtable_lookup(SymTable *st, const char *name)
{
    for (Scope *s = st->current; s; s = s->parent) {
        Symbol *sym = scope_find(s, name);
        if (sym) return sym;
    }
    return NULL;
}
