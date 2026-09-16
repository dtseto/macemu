#!/bin/bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=${B2_TEST_BIN:-$ROOT/src/Unix/BasiliskII}
ROM=${B2_ROM:-}
SECS=${B2_TIMEOUT:-30}

[ -n "$ROM" ] && [ -f "$ROM" ] || { echo "FAIL: set B2_ROM to a readable ROM image" >&2; exit 1; }
[ -x "$BIN" ] || { echo "FAIL: emulator not found or not executable: $BIN" >&2; exit 1; }
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/basilisk-rom-test.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT
cat >"$run_dir/prefs" <<EOF
rom $ROM
ramsize 8388608
modelid 14
cpu 4
fpu false
jit ${B2_JIT:-false}
screen win/640/480
nosound true
nocdrom true
nogui true
ignoresegv false
EOF

set +e
env HOME="$run_dir" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    /usr/bin/python3 - "$BIN" "$run_dir/prefs" "$SECS" >"$run_dir/log" 2>&1 <<'PY'
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
    print(stdout + stderr, end="")
    raise SystemExit(124)
print(result.stdout, end="")
print(result.stderr, end="")
raise SystemExit(result.returncode)
PY
rc=$?
set -e
cat "$run_dir/log"
echo "METRIC rom_timeout=$([ "$rc" -eq 124 ] && echo 1 || echo 0)"
echo "METRIC rom_exit=$rc"

# A bounded timeout is success only if the process produced output.  Any crash,
# clean early exit, or silent timeout is a failed smoke test.
[ "$rc" -eq 124 ] && [ -s "$run_dir/log" ]
