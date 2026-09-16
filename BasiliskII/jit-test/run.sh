#!/bin/bash
set -euo pipefail

# Minimal upstream-compatible opcode equivalence harness.  The emulator-side
# B2_TEST_HEX mode is deliberately test-only and must emit exactly one dump.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=${B2_TEST_BIN:-$ROOT/src/Unix/BasiliskII}
ROM=${B2_TEST_ROM:-}
TIMEOUT_SECONDS=${B2_TEST_TIMEOUT:-10}

usage() {
    echo "usage: $0 [--tests nop,moveq]" >&2
}

tests="nop,moveq"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --tests) [ "$#" -ge 2 ] || { usage; exit 2; }; tests=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done

fail_infra=0
pass=0
fail=0
total=0
if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "INFRA: set B2_TEST_ROM to a readable ROM image" >&2
    fail_infra=1
elif [ ! -x "$BIN" ]; then
    echo "INFRA: emulator not found or not executable: $BIN" >&2
    fail_infra=1
fi

vector_hex() {
    case "$1" in
        nop) echo 4E71 ;;
        moveq) echo 707F ;;
        move_w) echo "303C 1234" ;;
        add_l) echo "7001 7202 D081" ;;
        sub_l) echo "7202 7401 9482" ;;
        and_l) echo "700F 7203 C081" ;;
        or_l) echo "700F 7203 8081" ;;
        eor_l) echo "700F 7203 B381" ;;
        cmp_l) echo "7001 7201 B081" ;;
        tst_l) echo "7000 4A80" ;;
        bne_taken) echo "7001 6602 7202" ;;
        bne_not_taken) echo "7000 6602 7202" ;;
        memory_word) echo "41F9 0000 2000 30FC 1234 0010 3010" ;;
        addq_w) echo "7001 5240" ;;
        swap) echo "7001 4840" ;;
        movem) echo "7001 7202 48E7 0003 4CDF 0003" ;;
        *) return 1 ;;
    esac
}

run_case() {
    local name=$1 hex=$2 jit=$3 prefs=$4 log=$5
    cat >"$prefs" <<EOF
rom $ROM
ramsize 8388608
modelid 14
cpu 4
fpu false
jit $jit
screen win/640/480
nosound true
nocdrom true
nogui true
ignoresegv false
EOF
    env HOME="$(dirname "$prefs")" \
        B2_TEST_HEX="$hex 2C7C 1234 5678" \
        B2_TEST_DUMP=1 \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        /usr/bin/python3 - "$BIN" "$prefs" "$TIMEOUT_SECONDS" >"$log" 2>&1 <<'PY'
import subprocess
import sys

binary, prefs, timeout = sys.argv[1], sys.argv[2], float(sys.argv[3])
try:
    result = subprocess.run([binary, "--config", prefs], timeout=timeout,
                            check=False, capture_output=True, text=True)
except subprocess.TimeoutExpired as error:
    stdout = error.stdout or b""
    stderr = error.stderr or b""
    if isinstance(stdout, bytes):
        stdout = stdout.decode(errors="replace")
    if isinstance(stderr, bytes):
        stderr = stderr.decode(errors="replace")
    sys.stdout.write(stdout + stderr)
    raise SystemExit(124)
sys.stdout.write(result.stdout)
sys.stdout.write(result.stderr)
raise SystemExit(result.returncode)
PY
    local rc=$?
    [ "$rc" -eq 0 ] || return 1
    [ "$(grep -c '^REGDUMP:' "$log" || true)" -eq 1 ] || return 1
    grep -q 'A6=12345678' "$log"
}

if [ "$fail_infra" -eq 0 ]; then
    run_dir=$(mktemp -d "${TMPDIR:-/tmp}/basilisk-jit-test.XXXXXX")
    trap 'rm -rf "$run_dir"' EXIT
    IFS=',' read -r -a selected <<<"$tests"
    for name in "${selected[@]}"; do
        hex=$(vector_hex "$name") || { echo "INFRA: unknown vector $name" >&2; fail_infra=1; break; }
        interpreter="$run_dir/$name.interpreter.log"
        jit="$run_dir/$name.jit.log"
        total=$((total + 1))
        if run_case "$name" "$hex" false "$run_dir/$name.interpreter.prefs" "$interpreter" &&
           run_case "$name" "$hex" true "$run_dir/$name.jit.prefs" "$jit" &&
           diff -u <(grep '^REGDUMP:' "$interpreter") <(grep '^REGDUMP:' "$jit") >/dev/null; then
            pass=$((pass + 1))
            echo "PASS $name"
        else
            fail=$((fail + 1))
            echo "FAIL $name" >&2
        fi
    done
fi

if [ "$fail_infra" -ne 0 ]; then
    pass=0
    fail=0
    total=0
fi
score=0
[ "$total" -gt 0 ] && score=$((pass * 100 / total))
echo "METRIC pass=$pass"
echo "METRIC fail=$fail"
echo "METRIC total=$total"
echo "METRIC score=$score"
echo "METRIC infra_fail=$fail_infra"
[ "$fail_infra" -eq 0 ] && [ "$fail" -eq 0 ]
