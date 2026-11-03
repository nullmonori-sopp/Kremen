#!/bin/sh
# Native Windows integration under the MSYS2 UCRT64 build shell.
set -eu
case "$(uname -s)" in
    MINGW*|MSYS*) ;;
    *) printf '%s\n' 'SKIP: Windows integration requires an MSYS2 UCRT64 shell.'; exit 0 ;;
esac
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen.exe"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-windows047.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok windows047.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
expect() {
    tr -d '\r' < "$WORK/output" > "$WORK/normalized"
    printf '%s\n' "$2" > "$WORK/expected"
    cmp -s "$WORK/expected" "$WORK/normalized" || fail "$1 output"
    pass "$1"
}
reject() {
    label=$1; code=$2; shift 2
    status=0
    "$KREMEN" "$@" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label status=$status"
    "$WORK/json.exe" "$WORK/error" || fail "$label JSON"
    grep -F "\"code\":\"$code\"" "$WORK/error" > /dev/null || fail "$label diagnostic"
    pass "$label"
}
"${CC:-gcc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/json_validate.c" -o "$WORK/json.exe" > "$WORK/output" 2> "$WORK/error" || fail 'JSON validator build'
printf 'func main() { println(7) }\n' > "$WORK/main.kr"
"$KREMEN" --version > "$WORK/output" 2> "$WORK/error" || fail 'compiler version'
tr -d '\r' < "$WORK/output" > "$WORK/normalized"
grep -F "Kremen $(cat "$ROOT/VERSION")" "$WORK/normalized" > /dev/null || fail 'compiler version value'
pass 'native compiler version'
"$KREMEN" doctor > "$WORK/output" 2> "$WORK/error" || fail 'doctor'
grep -F 'Worker spawn/join: OK' "$WORK/output" > /dev/null || fail 'doctor runtime probe'
pass 'doctor compiles and executes the native pthread runtime'
reject 'missing backend is a structured doctor error' E016 doctor --cc kremen-missing-compiler-047
for mode in work learn; do
    "$KREMEN" check "$WORK/main.kr" --mode="$mode" > "$WORK/output" 2> "$WORK/error" || fail "$mode check"
    pass "$mode source checking"
    "$KREMEN" run "$WORK/main.kr" --mode="$mode" > "$WORK/output" 2> "$WORK/error" || fail "$mode run"
    expect "$mode native compile and execution" 7
done
(cd "$WORK" && "$KREMEN" build main.kr) > "$WORK/output" 2> "$WORK/error" || fail 'default executable build'
[ -f "$WORK/a.exe" ] || fail 'default executable suffix'
"$WORK/a.exe" > "$WORK/output" 2> "$WORK/error" || fail 'default executable execution'
expect 'default build creates a runnable a.exe' 7
mkdir "$WORK/path with ' quote & ampersand"
cp "$WORK/main.kr" "$WORK/path with ' quote & ampersand/source.kr"
QUOTED_PATH=$(cygpath -m "$WORK/path with ' quote & ampersand")
"$KREMEN" build "$QUOTED_PATH/source.kr" -o "$QUOTED_PATH/result.exe" > "$WORK/output" 2> "$WORK/error" || fail 'quoted paths build'
"$QUOTED_PATH/result.exe" > "$WORK/output" 2> "$WORK/error" || fail 'quoted paths executable'
expect 'source and output paths preserve spaces apostrophes and shell characters' 7
mkdir "$WORK/run temp"
TEMP=$(cygpath -m "$WORK/run temp") TMP=$(cygpath -m "$WORK/run temp") "$KREMEN" run "$WORK/main.kr" > "$WORK/output" 2> "$WORK/error" || fail 'temporary path execution'
expect 'run accepts a Windows temporary directory containing spaces' 7
[ -z "$(find "$WORK/run temp" -mindepth 1 -print)" ] || fail 'temporary run artifacts remain'
pass 'run removes temporary executable and staging directories'
printf 'func main() {\r\n    println(7)\r\n}\r\n' > "$WORK/crlf.kr"
"$KREMEN" run "$WORK/crlf.kr" > "$WORK/output" 2> "$WORK/error" || fail 'CRLF source'
expect 'CRLF sources retain normal semantics' 7
"$KREMEN" fmt -w "$WORK/crlf.kr" > "$WORK/output" 2> "$WORK/error" || fail 'formatter replacement'
"$KREMEN" fmt --check "$WORK/crlf.kr" > "$WORK/output" 2> "$WORK/error" || fail 'formatter canonical source'
pass 'formatter replaces an existing source and remains idempotent'
cat > "$WORK/mode.c" <<'C'
#include <io.h>
#include <sys/stat.h>
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    return _chmod(argv[1], _S_IREAD | (argv[2][0] == 'w' ? _S_IWRITE : 0)) ? 1 : 0;
}
C
"${CC:-gcc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$WORK/mode.c" -o "$WORK/mode.exe" > "$WORK/output" 2> "$WORK/error" || fail 'file-mode helper build'
cp "$WORK/main.kr" "$WORK/readonly.kr"
cp "$WORK/readonly.kr" "$WORK/readonly-original.kr"
"$WORK/mode.exe" "$WORK/readonly.kr" read > "$WORK/output" 2> "$WORK/error" || fail 'read-only source setup'
status=0
"$KREMEN" fmt -w "$WORK/readonly.kr" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
"$WORK/mode.exe" "$WORK/readonly.kr" write || fail 'restore read-only source permissions'
[ "$status" -eq 1 ] || fail 'read-only formatter status'
"$WORK/json.exe" "$WORK/error" || fail 'read-only formatter JSON'
grep -F '"code":"E015"' "$WORK/error" > /dev/null || fail 'read-only formatter diagnostic'
cmp -s "$WORK/readonly.kr" "$WORK/readonly-original.kr" || fail 'read-only source changed'
[ -z "$(find "$WORK" -name '*.kremen-*' -print)" ] || fail 'read-only formatter left staged output'
pass 'read-only formatter failure preserves source and removes staged output'
"$KREMEN" run "$ROOT/tests/native/modules/main.kr" > "$WORK/output" 2> "$WORK/error" || fail 'modules and headers'
expect 'directory imports and paired headers compile natively' '7 15'
WINROOT=$(cygpath -m "$WORK" | tr '[:lower:]' '[:upper:]')
"$KREMEN" check "$WORK/main.kr" --root "$WINROOT" > "$WORK/output" 2> "$WORK/error" || fail 'canonical case-insensitive root'
pass 'project roots accept equivalent Windows path case'
mkdir "$WORK/root"
printf 'import "C:outside"\nfunc main() {}\n' > "$WORK/root/invalid.kr"
reject 'drive-relative imports are rejected' E013 check "$WORK/root/invalid.kr"
printf 'import "C:/Windows"\nfunc main() {}\n' > "$WORK/root/invalid.kr"
reject 'drive-absolute imports are rejected' E013 check "$WORK/root/invalid.kr"
cp "$WORK/main.kr" "$WORK/original.kr"
SOURCE_ALIAS=$(cygpath -m "$WORK/main.kr" | tr '[:lower:]' '[:upper:]')
reject 'case aliases cannot overwrite a loaded source' E001 emit-c "$WORK/main.kr" -o "$SOURCE_ALIAS"
cmp -s "$WORK/main.kr" "$WORK/original.kr" || fail 'case alias overwrote source'
cat > "$WORK/link.c" <<'C'
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int main(int argc, char **argv) {
    return argc == 3 && CreateHardLinkA(argv[1], argv[2], NULL) ? 0 : 1;
}
C
"${CC:-gcc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$WORK/link.c" -o "$WORK/link.exe" > "$WORK/output" 2> "$WORK/error" || fail 'hardlink helper build'
"$WORK/link.exe" "$WORK/alias.kr" "$WORK/main.kr" > "$WORK/output" 2> "$WORK/error" || fail 'hardlink creation'
reject 'hard links cannot overwrite a loaded source' E001 emit-c "$WORK/main.kr" -o "$WORK/alias.kr"
cmp -s "$WORK/main.kr" "$WORK/original.kr" || fail 'hardlink overwrote source'
for mode in work learn; do
    (cd "$WORK" && "$KREMEN" run "$ROOT/tests/windows/runtime.kr" --mode="$mode") > "$WORK/output" 2> "$WORK/error" || fail "$mode runtime fixture"
    expect "$mode binary files replacement listing cleanup and worker join" 'windows runtime ok'
done
CC=${CC:-gcc} sh "$ROOT/tests/native/runtime_047.sh" > "$WORK/runtime.log" 2> "$WORK/error" || { cat "$WORK/runtime.log" >&2; fail 'direct Windows runtime checks'; }
grep -F '12' "$WORK/runtime.log" > /dev/null || fail 'direct runtime test count'
pass 'direct runtime regressions run against the native Windows CRT'
PREFIX="$WORK/install prefix"
CC=${CC:-gcc} sh "$ROOT/install.sh" --prefix "$PREFIX" > "$WORK/output" 2> "$WORK/error" || fail 'Windows source installation'
[ -f "$PREFIX/bin/kremen.exe" ] || fail 'installed executable'
[ ! -f "$PREFIX/bin/kremen-selfhost" ] || fail 'unsupported subset installed'
"$PREFIX/bin/kremen.exe" doctor > "$WORK/output" 2> "$WORK/error" || fail 'installed doctor'
pass 'installer publishes the full compiler and discovers its runtime'
mv "$PREFIX" "$WORK/relocated prefix"
PREFIX="$WORK/relocated prefix"
"$PREFIX/bin/kremen.exe" run "$PREFIX/share/doc/kremen/examples/hello.kr" > "$WORK/output" 2> "$WORK/error" || fail 'relocated compiler execution'
tr -d '\r' < "$WORK/output" > "$WORK/normalized"
grep -F 'Hello' "$WORK/normalized" > /dev/null || fail 'relocated example output'
pass 'installed compiler and resources survive prefix relocation'
CC=${CC:-gcc} sh "$ROOT/install.sh" --prefix "$PREFIX" --uninstall > "$WORK/output" 2> "$WORK/error" || fail 'Windows uninstall'
[ ! -e "$PREFIX/bin/kremen.exe" ] || fail 'installed executable remains'
pass 'Windows uninstall removes owned compiler artifacts'
printf 'Windows 0.4.7 integration checks passed: %s.\n' "$TOTAL"
