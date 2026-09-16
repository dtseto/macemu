#!/usr/bin/env python3
"""Basilisk II benchmark and ROM regression runner (standard library only)."""
from __future__ import annotations
import argparse, hashlib, json, os, platform, re, resource, shlex, signal, subprocess, sys, time
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1
METRIC_RE = re.compile(r"(?:B2_)?METRIC[ :]+([A-Za-z0-9_.-]+)=([^\s]+)")

def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def host_metadata() -> dict[str, Any]:
    return {"system": platform.system(), "release": platform.release(), "machine": platform.machine(),
            "python": platform.python_version(), "logical_cpus": os.cpu_count()}

def parse_metrics(text: str) -> dict[str, Any]:
    metrics: dict[str, Any] = {}
    for name, raw in METRIC_RE.findall(text):
        try:
            value: Any = float(raw) if any(c in raw for c in ".eE") else int(raw, 0)
        except ValueError:
            value = raw
        metrics[name] = value
    return metrics

def terminate(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=5)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try: os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError: pass

def run_scenario(config: dict[str, Any]) -> dict[str, Any]:
    argv = shlex.split(config["command"]) if isinstance(config["command"], str) else config["command"]
    environment = os.environ.copy()
    environment.update({str(k): str(v) for k, v in config.get("environment", {}).items()})
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    process = subprocess.Popen(argv, cwd=config.get("working_directory"), env=environment,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
    timed_out = False
    try:
        output, _ = process.communicate(timeout=float(config.get("timeout_seconds", 120)))
    except subprocess.TimeoutExpired:
        timed_out = True
        terminate(process)
        output, _ = process.communicate()
    wall = time.monotonic() - started
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    metrics = parse_metrics(output)
    metrics.update({"process.wall_seconds": wall, "process.cpu_seconds": cpu,
                    "process.cpu_percent": cpu / wall * 100.0 if wall else 0.0,
                    "process.max_rss_kib": after.ru_maxrss})
    for label, pattern in config.get("milestones", {}).items():
        metrics[f"milestone.{label}.observed"] = bool(re.search(pattern, output, re.MULTILINE))
    framebuffer = config.get("framebuffer_path")
    if framebuffer and Path(framebuffer).is_file():
        metrics["framebuffer.sha256"] = sha256(Path(framebuffer))
        metrics["framebuffer.bytes"] = Path(framebuffer).stat().st_size
    expected = config.get("expected_framebuffer_sha256")
    checksum_ok = expected is None or metrics.get("framebuffer.sha256") == expected
    allowed = set(config.get("allowed_exit_codes", [0]))
    ok = (process.returncode in allowed or (timed_out and config.get("timeout_is_success", False))) and checksum_ok
    return {"name": config.get("name", Path(argv[0]).name), "ok": ok, "command": argv,
            "exit_code": process.returncode, "timed_out": timed_out, "metrics": metrics,
            "output_tail": output[-8192:]}

def compare(base: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    old_metrics, new_metrics = base["runs"][0]["metrics"], candidate["runs"][0]["metrics"]
    changes = {}
    for key in sorted(old_metrics.keys() & new_metrics.keys()):
        old, new = old_metrics[key], new_metrics[key]
        if isinstance(old, (int, float)) and not isinstance(old, bool) and isinstance(new, (int, float)) and not isinstance(new, bool):
            changes[key] = {"baseline": old, "candidate": new, "delta": new - old,
                            "percent": (new - old) / old * 100.0 if old else None}
    return {"schema_version": SCHEMA_VERSION, "comparison": changes}

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    run = commands.add_parser("run")
    run.add_argument("--manifest", required=True, type=Path); run.add_argument("--output", type=Path)
    run.add_argument("--scenario", action="append", default=[])
    diff = commands.add_parser("compare")
    diff.add_argument("baseline", type=Path); diff.add_argument("candidate", type=Path); diff.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.action == "compare":
        result = compare(json.loads(args.baseline.read_text()), json.loads(args.candidate.read_text()))
    else:
        scenarios = json.loads(args.manifest.read_text()).get("scenarios", [])
        if args.scenario: scenarios = [s for s in scenarios if s.get("name") in args.scenario]
        result = {"schema_version": SCHEMA_VERSION, "generated_at_unix": time.time(),
                  "host": host_metadata(), "runs": [run_scenario(s) for s in scenarios]}
        result["ok"] = bool(result["runs"]) and all(run["ok"] for run in result["runs"])
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(rendered)
    else: sys.stdout.write(rendered)
    return 0 if result.get("ok", True) else 1

if __name__ == "__main__": raise SystemExit(main())
