#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 -m unittest discover -s "$ROOT/tests" -p 'test_*.py'
if [ "${B2_ROM_SMOKE:-0}" = 1 ]; then
    : "${B2_BENCH_MANIFEST:?set B2_BENCH_MANIFEST to a ROM/disk manifest}"
    "$ROOT/scripts/bench.sh" run --manifest "$B2_BENCH_MANIFEST" --output "${B2_TEST_RESULTS:-$ROOT/test-results.json}"
fi
