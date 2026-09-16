#!/bin/sh
set -eu

usage() {
    echo "usage: $0 run --manifest FILE --output FILE" >&2
    exit 2
}

[ "${1-}" = run ] || usage
shift
manifest=
output=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --manifest) [ "$#" -ge 2 ] || usage; manifest=$2; shift 2 ;;
        --output) [ "$#" -ge 2 ] || usage; output=$2; shift 2 ;;
        *) usage ;;
    esac
done
[ -n "$manifest" ] && [ -f "$manifest" ] || usage
[ -n "$output" ] || usage

# Keep the benchmark contract dependency-free on macOS.  Python is used only
# as a JSON parser/process runner; all benchmark commands remain supplied by
# the manifest and are executed without a shell unless the manifest requests
# one explicitly.
/usr/bin/python3 - "$manifest" "$output" <<'PY'
import json
import os
import subprocess
import sys
import time

manifest_path, output_path = sys.argv[1:]
with open(manifest_path, encoding="utf-8") as handle:
    manifest = json.load(handle)

runs = []
for scenario in manifest.get("scenarios", []):
    command = scenario.get("command")
    if not isinstance(command, list) or not command:
        raise SystemExit("scenario command must be a non-empty argv array")
    started = time.monotonic()
    process = subprocess.run(command, capture_output=True, text=True, check=False)
    elapsed = time.monotonic() - started
    combined = process.stdout + process.stderr
    metrics = {}
    for line in combined.splitlines():
        if line.startswith("B2_METRIC "):
            key, separator, value = line[10:].partition("=")
            if separator:
                try:
                    metrics[key] = int(value)
                except ValueError:
                    try:
                        metrics[key] = float(value)
                    except ValueError:
                        metrics[key] = value
    runs.append({
        "name": scenario.get("name", "unnamed"),
        "status": process.returncode,
        "duration_seconds": elapsed,
        "metrics": metrics,
    })

result = {
    "schema_version": 1,
    "ok": all(run["status"] == 0 for run in runs),
    "runs": runs,
}
with open(output_path, "w", encoding="utf-8") as handle:
    json.dump(result, handle, indent=2, sort_keys=True)
    handle.write("\n")
if not result["ok"]:
    raise SystemExit(1)
PY
