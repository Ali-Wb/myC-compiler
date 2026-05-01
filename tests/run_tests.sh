#!/usr/bin/env bash
#
# run_tests.sh — compile and run every *.c test under tests/
#
# For each test.c that has a matching test.expected:
#   1. Compile with ./mycc test.c /tmp/mycc_test_$$
#   2. Run the binary, capture its exit code
#   3. Compare against the integer in test.expected
#   4. Print PASS ✓ or FAIL ✗
#
# Exit code: 0 if all tests pass, 1 if any fail.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MYCC="$SCRIPT_DIR/../mycc"
TMP_BIN="/tmp/mycc_test_$$"

pass=0
fail=0
skip=0

# ── Sanity check ──────────────────────────────────────────────────────
if [ ! -x "$MYCC" ]; then
    echo "error: compiler not found or not executable: $MYCC"
    echo "       Build it first:  make"
    exit 1
fi

# ── Run each test ─────────────────────────────────────────────────────
for src in "$SCRIPT_DIR"/*.c; do
    name="$(basename "$src")"
    expected_file="${src%.c}.expected"

    # Skip test files that have no .expected companion
    if [ ! -f "$expected_file" ]; then
        printf "SKIP  %s\n" "$name"
        skip=$((skip + 1))
        continue
    fi

    expected=$(tr -d '[:space:]' < "$expected_file")

    # Compile — capture stderr; check exit code manually
    compile_out=$("$MYCC" "$src" "$TMP_BIN" 2>&1)
    compile_rc=$?
    if [ "$compile_rc" -ne 0 ]; then
        printf "FAIL ✗  %-38s (compile error)\n" "$name"
        if [ -n "$compile_out" ]; then
            # Indent each line of compiler output
            while IFS= read -r line; do
                printf "         %s\n" "$line"
            done <<< "$compile_out"
        fi
        fail=$((fail + 1))
        continue
    fi

    # Run — discard stdout/stderr, keep only exit code
    "$TMP_BIN" >/dev/null 2>&1
    actual=$?

    if [ "$actual" -eq "$expected" ]; then
        printf "PASS ✓  %s\n" "$name"
        pass=$((pass + 1))
    else
        printf "FAIL ✗  %-38s (expected exit %s, got %s)\n" \
               "$name" "$expected" "$actual"
        fail=$((fail + 1))
    fi
done

# ── Cleanup ───────────────────────────────────────────────────────────
rm -f "$TMP_BIN"

# ── Summary ───────────────────────────────────────────────────────────
total=$((pass + fail))
echo ""
echo "──────────────────────────────────"
printf "  %d / %d passed" "$pass" "$total"
[ "$skip" -gt 0 ] && printf "  (%d skipped)" "$skip"
printf "\n"
echo "──────────────────────────────────"

[ "$fail" -eq 0 ]
