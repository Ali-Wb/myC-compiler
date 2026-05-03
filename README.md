# mycc — A C Compiler Written in C

`mycc` is a hand-written C compiler that takes a C source file and produces a native Linux x86-64 binary. It implements its own lexer, recursive-descent parser, semantic analysis pass, and x86-64 code generator — no LLVM, no Bison, no Flex. Just C.

---

## Requirements

- Linux x86-64 (or WSL on Windows)
- `gcc` on PATH — used as the final assembler/linker
- C11-capable compiler to build `mycc` itself (gcc or clang)

---

## Building

```bash
make
```

This produces the `mycc` binary in the project root. To clean up:

```bash
make clean
```

---

## Usage

```
mycc [--dump-ast] <source.c> [output]
```

| Argument / Flag | Description |
|---|---|
| `<source.c>` | The C source file to compile |
| `[output]` | Output binary name. Defaults to `a.out` |
| `--dump-ast` | Print the AST to stdout and exit (no binary produced) |

### Basic example

```bash
./mycc hello.c
./a.out
echo $?
```

### Specify output name

```bash
./mycc hello.c hello
./hello
```

### Inspect the AST

```bash
./mycc --dump-ast hello.c
```

---

## What mycc supports right now

### 1. Functions

You can define multiple functions. Functions can take up to 6 integer/pointer parameters (System V AMD64 ABI register limit: `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`).

```c
int add(int a, int b) {
    return a + b;
}

int main() {
    return add(10, 5);
}
```

Return types: `int`, `char`, `void`.

---

### 2. Variables

Declare local variables with `int` or `char`. Optionally initialize them inline.

```c
int main() {
    int x = 10;
    int y;
    y = x + 5;
    return y;
}
```

Variables are block-scoped — a variable declared inside `{}` is not visible outside it.

---

### 3. Arithmetic

All standard arithmetic operators are supported:

| Operator | Meaning |
|---|---|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division (signed) |
| `%` | Remainder (signed) |

Operator precedence follows standard C rules (`*`, `/`, `%` before `+`, `-`).

```c
int main() {
    return 3 + 4 * 2;   // → 11
}
```

---

### 4. Comparison & logical operators

```c
int main() {
    int x = 10;
    if (x >= 5 && x != 7) {
        return 1;
    }
    return 0;
}
```

| Operator | Meaning |
|---|---|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |
| `&&` | Logical AND |
| `\|\|` | Logical OR |
| `!` | Logical NOT |

---

### 5. Unary operators

```c
int main() {
    int x = -5;
    int y = !0;   // y = 1
    return x + y; // → -4
}
```

---

### 6. if / else

```c
int main() {
    if (10 > 5) {
        return 1;
    } else {
        return 0;
    }
}
```

The `else` branch is optional. Nesting works:

```c
int classify(int n) {
    if (n < 0) {
        return -1;
    } else if (n == 0) {
        return 0;
    } else {
        return 1;
    }
}
```

---

### 7. while loops

```c
int main() {
    int i = 0;
    while (i < 5) {
        i = i + 1;
    }
    return i;  // → 5
}
```

---

### 8. for loops

```c
int main() {
    int sum = 0;
    for (int i = 0; i < 10; i = i + 1) {
        sum = sum + i;
    }
    return sum;  // → 45
}
```

All three parts of the `for` header are optional:

```c
// infinite loop (you'll need a return inside the body)
for (;;) { ... }
```

The loop variable declared in the `for` init is scoped to the loop — it's not visible after the closing `}`.

---

### 9. String literals & calling libc

`mycc` links against libc via `gcc -lc`, so you can call standard library functions like `printf` directly. String literals are stored in `.rodata` and deduplicated automatically.

```c
int main() {
    printf("Hello, World!\n");
    return 0;
}
```

```c
int main() {
    int x = 21;
    int y = 21;
    printf("Result: %d\n", x + y);
    return 0;
}
```

Supported escape sequences in strings: `\n`, `\t`, `\r`, `\"`, `\\`.

---

### 10. Assignment as expression

Assignment is right-associative and can be used as part of a larger expression context:

```c
int main() {
    int a;
    int b;
    a = b = 5;   // both a and b become 5
    return a + b;
}
```

---

### 11. Nested scopes

Variables declared in inner blocks shadow outer ones and are cleaned up at the end of their block:

```c
int main() {
    int x = 1;
    {
        int x = 2;   // different x
        // inner x is 2 here
    }
    return x;  // outer x is still 1
}
```

---

## Preprocessor directives

`mycc` has no preprocessor. Lines starting with `#` (like `#include`, `#define`) are silently ignored so that files with standard headers don't cause a lex error. The declarations themselves are not processed — you can call `printf` without `#include <stdio.h>` because `mycc` links against libc directly.

---

## Error messages

`mycc` reports errors with a stage label and source line number. Examples:

```
error: line 3: expected ';' but got 'return'
error: 'y' used before declaration (line 2)
error: 'x' already declared in this scope (line 4)
```

Errors from the lexer, parser, semantic analysis pass, and codegen all follow this format.

---

## Running the test suite

Tests live in `tests/`. Each test is a `.c` file paired with a `.expected` file containing the expected exit code.

```bash
cd tests
bash run_tests.sh
```

Expected output:

```
PASS ✓  test01_return.c
PASS ✓  test02_arithmetic.c
PASS ✓  test03_variable.c
PASS ✓  test04_if_else.c
PASS ✓  test05_while.c
PASS ✓  test06_function.c
PASS ✓  test07_printf_string.c
PASS ✓  test08_printf_int.c

──────────────────────────────────
  8 / 8 passed
──────────────────────────────────
```

---

## What's not supported yet

These are known limitations, not bugs:

- No preprocessor (`#include`, `#define`, macros)
- No pointers or arrays
- No structs or enums
- No `break` / `continue` in loops
- No compound assignment operators (`+=`, `-=`, etc.)
- No increment / decrement (`++`, `--`)
- No floating-point types
- No global variables
- Functions with more than 6 arguments (stack-passed args not yet implemented)
- No type checking (e.g. assigning a string to an `int` is not caught)

---

## How it works (the pipeline)

```
source.c
   │
   ▼
strip_hashes()     — blank out # lines so the lexer never sees them
   │
   ▼
tokenize()         — produce a flat array of tokens (lexer.c)
   │
   ▼
parse()            — build an AST via recursive descent (parser.c)
   │
   ▼
resolve()          — check all identifiers are declared; catch duplicates (sema.c)
   │
   ▼
codegen_emit()     — walk the AST, emit x86-64 AT&T-syntax assembly (codegen.c)
   │
   ▼
gcc -lc            — assemble and link against libc → native binary
```

The generated assembly follows the System V AMD64 ABI. All expression results pass through `%rax`. Binary operators spill the left-hand side onto the stack while evaluating the right-hand side, then combine them.
