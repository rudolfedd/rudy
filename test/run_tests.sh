#!/bin/sh
# rudy test suite. Run: make test  (or ./test/run_tests.sh)
# Requires a POSIX shell (Linux/macOS/WSL/MSYS2).

set -u
cd "$(dirname "$0")/.."
RUDY=./rudy
TMP=$(mktemp -d 2>/dev/null || echo /tmp/rudy-test-$$)
mkdir -p "$TMP"
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); printf '  \033[32mok\033[0m   %s\n' "$1"; }
bad() { FAIL=$((FAIL+1)); printf '  \033[31mFAIL\033[0m %s\n' "$1"; }

# byte-exact file comparison (cmp, md5sum or od as available)
same_file() {
    if command -v cmp >/dev/null 2>&1; then
        cmp -s "$1" "$2"
    elif command -v md5sum >/dev/null 2>&1; then
        [ "$(md5sum < "$1" | cut -d' ' -f1)" = "$(md5sum < "$2" | cut -d' ' -f1)" ]
    else
        [ "$(od -An -c "$1" | tr -d ' \n')" = "$(od -An -c "$2" | tr -d ' \n')" ]
    fi
}

# byte-exact file comparison: check_file name 'printf format' file
check_file() {
    printf "$2" > "$TMP/.expected"
    if same_file "$TMP/.expected" "$3"; then ok "$1"; else
        bad "$1"
        printf '       expected: %s\n' "$(od -c "$TMP/.expected" | head -3 | tr '\n' ' ')"
        printf '       actual  : %s\n' "$(od -c "$3" | head -3 | tr '\n' ' ')"
    fi
}

# substring check on the editor output: check_out name 'substring' output
check_out() {
    case "$3" in
        *"$2"*) ok "$1" ;;
        *) bad "$1" -- missing: $2 ;;
    esac
}

# ------------------------------------------------------------------- tests

F="$TMP/a.txt"
printf 'x\n' > "$F"
printf 'hello\023' | $RUDY "$F" > /dev/null 2>&1
check_file "insert text and save (^S)" 'hellox\n' "$F"

F="$TMP/b.txt"
printf 'x\n' > "$F"
printf 'abc\032\023' | $RUDY "$F" > /dev/null 2>&1
check_file "undo the edit (^Z)" 'x\n' "$F"

F="$TMP/c.txt"
printf 'one two three\n' > "$F"
printf '\027two\r\023' | $RUDY "$F" > /dev/null 2>&1
check_file "search does not modify the file (^W)" 'one two three\n' "$F"

F="$TMP/d.txt"
printf 'aaa\nbbb\n' > "$F"
printf '\022bbb\rzzz\r\023' | $RUDY "$F" > /dev/null 2>&1
check_file "replace all occurrences (^R)" 'aaa\nzzz\n' "$F"

F="$TMP/e.txt"
printf 'x\n' > "$F"
printf '\033c:wq\r' | $RUDY "$F" > /dev/null 2>&1
check_file "command line :wq" 'x\n' "$F"

F="$TMP/f.c"
printf 'int main(void) { return 0; }\n' > "$F"
OUT=$(printf '\023' | $RUDY "$F" 2>/dev/null)
check_out "highlight C: keyword int" '38;5;81m' "$OUT"

F="$TMP/g.rs"
printf 'fn main() { let x = 1; }\n' > "$F"
OUT=$(printf '\023' | $RUDY "$F" 2>/dev/null)
check_out "highlight Rust: keyword fn" '38;5;81m' "$OUT"

F="$TMP/h.asm"
printf 'mov eax, 1 ; comment\n' > "$F"
OUT=$(printf '\023' | $RUDY "$F" 2>/dev/null)
check_out "highlight NASM: instruction mov" '38;5;81m' "$OUT"

F="$TMP/i.c"
printf 'int x; // comment\n' > "$F"
OUT=$(printf '\023' | $RUDY "$F" 2>/dev/null)
check_out "highlight C: comment" '38;5;245m' "$OUT"

F="$TMP/j.cs"
printf 'public class Foo { }\n' > "$F"
OUT=$(printf '\023' | $RUDY "$F" 2>/dev/null)
check_out "highlight C#: type" '38;5;111m' "$OUT"

F="$TMP/k.java"
printf 'public class Main { public static void main(String[] a) {} }\n' > "$F"
OUT=$(printf '\023' | $RUDY "$F" 2>/dev/null)
check_out "highlight Java: keyword" '38;5;81m' "$OUT"

F="$TMP/l.cpp"
printf 'int main() { return 0; }\n' > "$F"
OUT=$(printf '\023' | $RUDY "$F" 2>/dev/null)
check_out "highlight C++: keyword" '38;5;81m' "$OUT"

F="$TMP/m.txt"
printf 'x\n' > "$F"
# Ctrl+E (end of line), Enter inserts a newline
printf '\005\r\023' | $RUDY "$F" > /dev/null 2>&1
check_file "insert newline (^E, Enter)" 'x\n\n' "$F"

F="$TMP/n.txt"
printf 'x\n' > "$F"
printf '\013\023' | $RUDY "$F" > /dev/null 2>&1
check_file "cut a line (^K)" '' "$F"

F="$TMP/o.txt"
printf 'abc\n' > "$F"
printf '\031\025\023' | $RUDY "$F" > /dev/null 2>&1
check_file "copy/paste a line (^Y/^U)" 'abc\nabc\n' "$F"

F="$TMP/p.txt"
printf 'x\n' > "$F"
printf '\0375\r\023' | $RUDY "$F" > /dev/null 2>&1
check_file "go to line (^_)" 'x\n' "$F"

F="$TMP/q.txt"
printf 'x\n' > "$F"
OUT=$(printf '\024\023' | $RUDY "$F" 2>/dev/null)
check_out "toggle highlighting (^T)" 'Syntax highlighting off' "$OUT"

F="$TMP/r.txt"
printf 'x\n' > "$F"
printf '\033c:s/x/y/g\r\023' | $RUDY "$F" > /dev/null 2>&1
check_file "command :s/pat/rep/g" 'y\n' "$F"

F="$TMP/s.txt"
printf 'x\n' > "$F"
# Ctrl+Q with unsaved changes -> "d" + Enter (save and quit)
printf 'zz\021d\r' | $RUDY "$F" > /dev/null 2>&1
check_file "quit and save (^Q)" 'zzx\n' "$F"

F="$TMP/t.txt"
printf 'x\n' > "$F"
printf 'zz\021n\r' | $RUDY "$F" > /dev/null 2>&1
check_file "quit without saving (^Q, n)" 'x\n' "$F"

F="$TMP/u.txt"
printf 'x\n' > "$F"
printf '\033c:set tabsize=8\r\023' | $RUDY "$F" > /dev/null 2>&1
check_file "command :set tabsize=8" 'x\n' "$F"

F="$TMP/v.txt"
printf 'x\n' > "$F"
OUT=$(printf '\033c:set syntax=rust\r' | $RUDY "$F" 2>/dev/null)
check_out "command :set syntax=rust" 'Language: Rust' "$OUT"

F="$TMP/w.txt"
printf 'abc\n' > "$F"
printf '\013\032\023' | $RUDY "$F" > /dev/null 2>&1
check_file "undo the cut (^K, ^Z)" 'abc\n' "$F"

F="$TMP/x.txt"
printf 'abc\n' > "$F"
printf '\013\025\023' | $RUDY "$F" > /dev/null 2>&1
check_file "cut a line and paste it back (^K, ^U)" 'abc\n\n' "$F"

F="$TMP/y.c"
printf 'int x;\n' > "$F"
printf '\033m\023' | $RUDY "$F" > /dev/null 2>&1
check_file "toggle comment (Alt+M)" '//int x;\n' "$F"

F="$TMP/z.c"
printf '//int x;\n' > "$F"
printf '\033m\023' | $RUDY "$F" > /dev/null 2>&1
check_file "uncomment a line (Alt+M)" 'int x;\n' "$F"

printf '\n  Passed: %d, failed: %d\n' "$PASS" "$FAIL"
rm -rf "$TMP"
[ "$FAIL" -eq 0 ]
