#!/usr/bin/env python3
"""Run the complete available SingleStepTests family regression sweep."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


FAMILIES = (
    "NOP", "SWAP", "EXT.w", "EXT.l", "AND.b", "AND.w", "OR.b",
    "EOR.b", "SUB.b", "ADD.b", "CLR.b",
)
RESULT = re.compile(
    r"UAE_CPU_CORPUS_RESULT total=(\d+) passed=(\d+) skipped=(\d+) "
    r"malformed=(\d+) (PASS|FAIL)"
)


def apply_baseline(details: list[dict], baseline: dict) -> list[str]:
    """Annotate family results and return pass-count regressions."""
    previous = {
        item["family"]: item["passed"]
        for item in baseline.get("families", [])
        if "family" in item and "passed" in item
    }
    regressions = []
    for item in details:
        old_passed = previous.get(item["family"])
        if old_passed is None:
            continue
        item["baseline_passed"] = old_passed
        if item.get("passed", 0) < old_passed:
            regressions.append(
                f"{item['family']}: passed {item.get('passed', 0)} "
                f"below baseline {old_passed}"
            )
    return regressions


def run_family(adapter: Path, harness: Path, corpus_root: Path,
               output_dir: Path, family: str, limit: int) -> tuple[bool, str, dict]:
    source = corpus_root / f"{family}.json"
    output = output_dir / f"{family}-68020.jsonl"
    adapter_args = [sys.executable, str(adapter)]
    if family == "CLR.b":
        adapter_args.append("--adapt-indexed-for-68020")
    adapter_args += ["--limit", str(limit), "-o", str(output), str(source)]
    adapted = subprocess.run(adapter_args, capture_output=True, text=True)
    if adapted.returncode != 0:
        return False, f"{family}: adapter failed: {adapted.stderr.strip()}", {
            "family": family, "status": "ADAPTER_FAIL",
        }

    environment = os.environ.copy()
    environment["B2_TEST_24BIT_ADDRESS"] = "1"
    result = subprocess.run(
        [str(harness), "--corpus", str(output)],
        capture_output=True, text=True, env=environment,
    )
    match = RESULT.search(result.stdout)
    if not match:
        return False, f"{family}: missing corpus result (exit {result.returncode})", {
            "family": family, "status": "NO_RESULT",
        }
    total, passed, skipped, malformed, status = match.groups()
    summary = (f"{family}: total={total} passed={passed} skipped={skipped} "
               f"malformed={malformed} {status}")
    details = {
        "family": family,
        "total": int(total),
        "passed": int(passed),
        "skipped": int(skipped),
        "malformed": int(malformed),
        "status": status,
    }
    return result.returncode == 0 and status == "PASS" and malformed == "0", summary, details


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
    parser.add_argument("--jobs", type=int, default=4,
                        help="families to execute concurrently (default: 4)")
    parser.add_argument("--summary-json", type=Path,
                        help="write aggregate and per-family results as JSON")
    parser.add_argument("--min-passed", type=int, default=0,
                        help="minimum aggregate supported-pass count")
    parser.add_argument("--baseline-json", type=Path,
                        help="fail if any family drops below this prior summary")
    args = parser.parse_args()
    if args.limit < 0:
        parser.error("--limit cannot be negative")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.min_passed < 0:
        parser.error("--min-passed cannot be negative")
    baseline = None
    if args.baseline_json:
        try:
            baseline = json.loads(args.baseline_json.read_text())
        except (OSError, json.JSONDecodeError) as error:
            parser.error(f"cannot read baseline JSON: {error}")
        if not isinstance(baseline, dict):
            parser.error("baseline JSON must contain an object")

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
        worker_count = min(args.jobs, len(FAMILIES))
        with ThreadPoolExecutor(max_workers=worker_count) as executor:
            results = list(executor.map(
                lambda family: run_family(
                    adapter, args.harness, args.corpus_root, output_dir,
                    family, args.limit),
                FAMILIES))
        details = [result[2] for result in results]
        for passed, summary, _ in results:
            print(summary)
            failures += not passed
        baseline_regressions = apply_baseline(details, baseline or {})
        for regression in baseline_regressions:
            print("BASELINE_REGRESSION " + regression)
        failures += len(baseline_regressions)
        aggregate = {
            "total": sum(item.get("total", 0) for item in details),
            "passed": sum(item.get("passed", 0) for item in details),
            "skipped": sum(item.get("skipped", 0) for item in details),
            "malformed": sum(item.get("malformed", 0) for item in details),
            "families": details,
            "baseline_regressions": baseline_regressions,
        }
        if aggregate["passed"] < args.min_passed:
            failures += 1
        if args.summary_json:
            args.summary_json.write_text(json.dumps(aggregate, indent=2) + "\n")
    finally:
        if temporary:
            for path in output_dir.glob("*.jsonl"):
                path.unlink()
            output_dir.rmdir()
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
