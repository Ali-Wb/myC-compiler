/*
 * main.c — mycc compiler driver.
 *
 * Usage: mycc [options] <source.c> [output]
 *
 * Options:
 *   --dump-ast   Print the AST to stdout and exit before codegen.
 *
 * Arguments:
 *   <source.c>   Source file to compile.
 *   [output]     Output binary name (default: a.out).
 *
 * Pipeline:
 *   1. read_file()     — read entire source into memory.
 *   2. strip_hashes()  — blank out preprocessor lines (# ...) so the
 *                        lexer never sees them; we have no preprocessor.
 *   3. tokenize()      — produce a flat Token array.
 *   4. parse()         — build the AST.
 *   5. resolve()       — semantic analysis: scope + declaration checking.
 *   6. codegen_emit()  — emit x86-64 AT&T assembly to a temp file.
 *   7. gcc             — assemble, link with libc, produce the binary.
 */

#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "ast_print.h"
#include "sema.h"
#include "codegen.h"

#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  File reader                                                         */
/* ------------------------------------------------------------------ */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) dief("cannot open '%s'", path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) die("out of memory reading source file");
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
        dief("read error on '%s'", path);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------------ */
/*  Preprocessor-line stripper                                          */
/* ------------------------------------------------------------------ */

/*
 * strip_hashes — replace every preprocessor directive with spaces.
 *
 * Any line whose first non-whitespace character is '#' is blanked out
 * by overwriting every character up to (but not including) the newline
 * with a space.  This preserves line numbers in error messages while
 * ensuring the lexer never sees #include, #define, etc.
 *
 * Operates in-place on the heap buffer returned by read_file().
 */
static void strip_hashes(char *src)
{
    char *p = src;
    while (*p) {
        /* Find the first non-space character on this line. */
        char *line_start = p;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '#') {
            /* Blank the entire line up to (not including) the newline. */
            for (char *q = line_start; *q && *q != '\n'; q++)
                *q = ' ';
        }

        /* Advance past the rest of the line. */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

/* ------------------------------------------------------------------ */
/*  Driver                                                              */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *src_path  = NULL;
    const char *out_path  = "a.out";
    int         dump_ast  = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-ast")) {
            dump_ast = 1;
        } else if (argv[i][0] != '-') {
            /* First non-flag arg is the source file, second is the output. */
            if (!src_path)
                src_path = argv[i];
            else
                out_path = argv[i];
        } else {
            dief("unknown flag '%s'", argv[i]);
        }
    }

    if (!src_path)
        die("usage: mycc [--dump-ast] <source.c> [output]");

    /* 1. Read */
    char *src = read_file(src_path);

    /* 2. Strip preprocessor lines */
    strip_hashes(src);

    /* 3. Lex */
    int    tok_count;
    Token *tokens = tokenize(src, &tok_count);

    /* 4. Parse */
    Node *ast = parse(tokens, tok_count);

    /* --dump-ast: print tree and exit */
    if (dump_ast) {
        print_ast(ast, 0);
        node_free(ast);
        tokens_free(tokens, tok_count);
        free(src);
        return 0;
    }

    /* 5. Semantic analysis */
    SymTable *st = symtable_new();
    resolve(ast, st);
    symtable_free(st);

    /* 6. Codegen → temp .s file */
    char asm_path[] = "/tmp/mycc_XXXXXX.s";
    int  fd         = mkstemps(asm_path, 2);
    if (fd < 0) die("cannot create temp assembly file");
    FILE *asm_out = fdopen(fd, "w");
    if (!asm_out) die("fdopen failed");

    Codegen cg;
    codegen_init(&cg, asm_out);
    codegen_emit(&cg, ast);
    fclose(asm_out);

    /* 7. Assemble + link with libc */
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "gcc -o %s %s -lc", out_path, asm_path);
    int rc = system(cmd);
    unlink(asm_path);
    if (rc != 0)
        dief("%s: gcc failed (exit code %d)", src_path, rc);

    node_free(ast);
    tokens_free(tokens, tok_count);
    free(src);
    return 0;
}
