/*
 * main.c — mycc compiler driver.
 *
 * Usage: mycc [options] <source.c>
 *
 * Options:
 *   -o <file>    Write binary to <file> (default: a.out)
 *   --dump-ast   Print the AST to stdout and exit; do not compile.
 *
 * Pipeline (without --dump-ast):
 *   1. Read source file into memory.
 *   2. tokenize()       → flat Token array.
 *   3. parse()          → AST (Node tree).
 *   4. codegen_emit()   → x86-64 AT&T assembly in a temp file.
 *   5. gcc              → assemble + link into the final binary.
 */

#include "compiler.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "ast_print.h"
#include "codegen.h"

#include <unistd.h>

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) dief("cannot open '%s'", path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) die("out of memory");
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) dief("read error on '%s'", path);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    const char *src_path = NULL;
    const char *out_path = "a.out";
    int         dump_ast = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(argv[i], "--dump-ast")) {
            dump_ast = 1;
        } else if (argv[i][0] != '-') {
            src_path = argv[i];
        } else {
            dief("unknown flag '%s'", argv[i]);
        }
    }

    if (!src_path)
        die("usage: mycc [--dump-ast] [-o output] <source.c>");

    /* 1. Read */
    char *src = read_file(src_path);

    /* 2. Lex */
    int    tok_count;
    Token *tokens = tokenize(src, &tok_count);

    /* 3. Parse */
    Node *ast = parse(tokens, tok_count);

    /* --dump-ast: print tree and exit without compiling */
    if (dump_ast) {
        print_ast(ast, 0);
        node_free(ast);
        tokens_free(tokens, tok_count);
        free(src);
        return 0;
    }

    /* 4. Codegen → temp .s file */
    char asm_path[] = "/tmp/mycc_XXXXXX.s";
    int  fd         = mkstemps(asm_path, 2);
    if (fd < 0) die("cannot create temp assembly file");
    FILE *asm_out = fdopen(fd, "w");
    if (!asm_out) die("fdopen failed");

    Codegen cg;
    codegen_init(&cg, asm_out);
    codegen_emit(&cg, ast);
    fclose(asm_out);

    /* 5. Assemble + link */
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "gcc -o %s %s", out_path, asm_path);
    int rc = system(cmd);
    unlink(asm_path);
    if (rc != 0) dief("gcc failed with exit code %d", rc);

    node_free(ast);
    tokens_free(tokens, tok_count);
    free(src);
    return 0;
}
