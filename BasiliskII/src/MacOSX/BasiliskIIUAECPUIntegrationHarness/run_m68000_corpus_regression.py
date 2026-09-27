#!/usr/bin/env python3
"""Run the complete available SingleStepTests family regression sweep."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


FAMILIES = (
    "NOP", "SWAP", "EXT.w", "EXT.l", "AND.b", "OR.b", "SUB.b",
    "ADD.b", "CLR.b",
)
RESULT = re.compile(
    r"UAE_CPU_CORPUS_RESULT total=(\d+) passed=(\d+) skipped=(\d+) "
    r"malformed=(\d+) (PASS|FAIL)"
)


def run_family(adapter: Path, harness: Path, corpus_root: Path,
               output_dir: Path, family: str, limit: int) -> tuple[bool, str]:
    source = corpus_root / f"{family}.json"
    output = output_dir / f"{family}-68020.jsonl"
    adapter_args = [sys.executable, str(adapter)]
    if family == "CLR.b":
        adapter_args.append("--adapt-indexed-for-68020")
    adapter_args += ["--limit", str(limit), "-o", str(output), str(source)]
    adapted = subprocess.run(adapter_args, capture_output=True, text=True)
    if adapted.returncode != 0:
        return False, f"{family}: adapter failed: {adapted.stderr.strip()}"

    environment = os.environ.copy()
    environment["B2_TEST_24BIT_ADDRESS"] = "1"
    result = subprocess.run(
        [str(harness), "--corpus", str(output)],
        capture_output=True, text=True, env=environment,
    )
    match = RESULT.search(result.stdout)
    if not match:
        return False, f"{family}: missing corpus result (exit {result.returncode})"
    total, passed, skipped, malformed, status = match.groups()
    summary = (f"{family}: total={total} passed={passed} skipped={skipped} "
               f"malformed={malformed} {status}")
    return result.returncode == 0 and status == "PASS" and malformed == "0", summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-root", required=True, type=Path,
                        help="decoded SingleStepTests v1 directory")
    parser.add_argument("--harness", required=True, type=Path,
                        help="BasiliskIIUAECPUIntegrationHarness executable")
    parser.add_argument("--adapter", type=Path,
                        help="adapter script; defaults beside this runner")
    parser.add_argument("--output-dir", type=Path,
                        help="retain generated JSONL files in this directory")
    parser.add_argument("--limit", type=int, default=2500,
                        help="vectors per family; 0 means all")
    args = parser.parse_args()
    if args.limit < 0:
        parser.error("--limit cannot be negative")

    adapter = args.adapter or Path(__file__).with_name("m68000_json_adapter.py")
    if not adapter.is_file() or not args.harness.is_file():
        parser.error("adapter and harness paths must exist")
    missing = [family for family in FAMILIES
               if not (args.corpus_root / f"{family}.json").is_file()]
    if missing:
        parser.error("missing corpus families: " + ", ".join(missing))

    temporary = args.output_dir is None
    output_dir = args.output_dir or Path(tempfile.mkdtemp(prefix="m68000-regression-"))
    output_dir.mkdir(parents=True, exist_ok=True)
    failures = 0
    try:
        for family in FAMILIES:
            passed, summary = run_family(adapter, args.harness, args.corpus_root,
                                         output_dir, family, args.limit)
            print(summary)
            failures += not passed
    finally:
        if temporary:
            for path in output_dir.glob("*.jsonl"):
                path.unlink()
            output_dir.rmdir()
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
