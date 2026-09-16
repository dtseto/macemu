#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${B2_BINARY:?set B2_BINARY to the BasiliskII executable}"
: "${B2_PREFS:?set B2_PREFS to a private test preferences file}"
RESULT=${B2_TEST_RESULTS:-$ROOT/test-results/rom-smoke.json}
MANIFEST=$(mktemp "${TMPDIR:-/tmp}/basiliskii-rom-smoke.XXXXXX")
trap 'rm -f "$MANIFEST"' EXIT HUP INT TERM
python3 - "$MANIFEST" <<'PY'
import json, os, sys
manifest = {"scenarios": [{
    "name": "rom-smoke",
    "command": [os.environ["B2_BINARY"], "--config", os.environ["B2_PREFS"]],
    "timeout_seconds": int(os.environ.get("B2_TIMEOUT", "120")),
    "timeout_is_success": True,
    "allowed_exit_codes": [0],
    "environment": {"B2_BENCHMARK_METRICS": "1", "SDL_VIDEODRIVER": os.environ.get("SDL_VIDEODRIVER", "dummy"),
                    "SDL_AUDIODRIVER": os.environ.get("SDL_AUDIODRIVER", "dummy")},
    "milestones": {"rom_started": os.environ.get("B2_ROM_MARKER", "Using ROM file|PatchROM"),
                   "desktop": os.environ.get("B2_DESKTOP_MARKER", "B2_DESKTOP_READY")}
}]}
with open(sys.argv[1], "w") as stream: json.dump(manifest, stream)
PY
exec "$ROOT/scripts/bench.sh" run --manifest "$MANIFEST" --output "$RESULT"
