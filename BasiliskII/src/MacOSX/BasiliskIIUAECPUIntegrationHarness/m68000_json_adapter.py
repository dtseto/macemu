#!/usr/bin/env python3
"""Normalize SingleStepTests/m68000 JSON vectors for the UAE harness.

The upstream corpus is intentionally kept outside this repository.  This
adapter converts decoded corpus files into a small, stable JSON-lines format
that can be consumed by a C++ or Python differential runner.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable


HEX_OPCODE = re.compile(r"\b([0-9a-fA-F]{4})\s*$")
REGISTERS = tuple(f"d{i}" for i in range(8)) + tuple(f"a{i}" for i in range(7))
REQUIRED_STATE = set(REGISTERS) | {"sr", "pc", "ram", "prefetch"}


class CorpusError(ValueError):
    pass


def integer(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CorpusError(f"{field} must be an integer")
    if value < 0 or value > 0xFFFFFFFF:
        raise CorpusError(f"{field} is outside the 32-bit range")
    return value


def parse_opcode(name: Any) -> int:
    if not isinstance(name, str):
        raise CorpusError("test name must be a string")
    match = HEX_OPCODE.search(name)
    if not match:
        raise CorpusError(f"cannot extract opcode from test name: {name!r}")
    return int(match.group(1), 16)


def normalize_state(state: Any, label: str) -> dict[str, Any]:
    if not isinstance(state, dict):
        raise CorpusError(f"{label} must be an object")
    missing = REQUIRED_STATE - set(state)
    if missing:
        raise CorpusError(f"{label} is missing: {', '.join(sorted(missing))}")

    registers = {register: integer(state[register], f"{label}.{register}")
                 for register in REGISTERS}
    registers["sr"] = integer(state["sr"], f"{label}.sr") & 0xFFFF
    registers["pc"] = integer(state["pc"], f"{label}.pc")
    registers["usp"] = integer(state.get("usp", 0), f"{label}.usp")
    registers["ssp"] = integer(state.get("ssp", 0), f"{label}.ssp")

    prefetch = state["prefetch"]
    if not isinstance(prefetch, list) or not all(isinstance(word, int) for word in prefetch):
        raise CorpusError(f"{label}.prefetch must be an integer array")
    if len(prefetch) < 2:
        raise CorpusError(f"{label}.prefetch must contain two words")

    ram = state["ram"]
    if not isinstance(ram, list):
        raise CorpusError(f"{label}.ram must be an array")
    normalized_ram = []
    for index, cell in enumerate(ram):
        if not isinstance(cell, list) or len(cell) != 2:
            raise CorpusError(f"{label}.ram[{index}] must be [address, byte]")
        address = integer(cell[0], f"{label}.ram[{index}].address")
        byte = integer(cell[1], f"{label}.ram[{index}].byte")
        if byte > 0xFF:
            raise CorpusError(f"{label}.ram[{index}].byte is not a byte")
        normalized_ram.append([address, byte])

    registers["prefetch"] = [word & 0xFFFF for word in prefetch[:2]]
    registers["ram"] = normalized_ram
    return registers


def normalize_test(test: Any, source: str, index: int) -> dict[str, Any]:
    if not isinstance(test, dict):
        raise CorpusError(f"{source}[{index}] must be an object")
    if "name" not in test or "initial" not in test or "final" not in test:
        raise CorpusError(f"{source}[{index}] must contain name, initial, and final")
    initial = normalize_state(test["initial"], f"{source}[{index}].initial")
    final = normalize_state(test["final"], f"{source}[{index}].final")
    opcode = parse_opcode(test["name"])
    return {
        "source": source,
        "index": index,
        "name": test["name"],
        "opcode": opcode,
        "instruction_words": [opcode, initial["prefetch"][1]],
        "initial": initial,
        "final": final,
        "length": integer(test.get("length", 0), f"{source}[{index}].length"),
    }


def read_tests(path: Path) -> list[Any]:
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise CorpusError(f"cannot read {path}: {error}") from error
    if not isinstance(value, list):
        raise CorpusError(f"{path} must contain a JSON array")
    return value


def iter_inputs(paths: Iterable[Path]) -> Iterable[tuple[Path, Any]]:
    for path in paths:
        for index, test in enumerate(read_tests(path)):
            yield path, (index, test)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path,
                        help="decoded SingleStepTests .json files")
    parser.add_argument("-o", "--output", type=Path,
                        help="JSON-lines output; defaults to stdout")
    parser.add_argument("--limit", type=int, default=0,
                        help="maximum vectors to emit (0 means all)")
    parser.add_argument("--opcode", type=lambda value: int(value, 0),
                        help="emit only one opcode, e.g. 0x4e71")
    args = parser.parse_args()
    if args.limit < 0:
        parser.error("--limit cannot be negative")

    output = args.output.open("w") if args.output else sys.stdout
    emitted = 0
    try:
        for path in args.inputs:
            for index, test in enumerate(read_tests(path)):
                vector = normalize_test(test, str(path), index)
                if args.opcode is not None and vector["opcode"] != args.opcode:
                    continue
                output.write(json.dumps(vector, separators=(",", ":")) + "\n")
                emitted += 1
                if args.limit and emitted >= args.limit:
                    break
            if args.limit and emitted >= args.limit:
                break
    except CorpusError as error:
        print(f"m68000_json_adapter: {error}", file=sys.stderr)
        return 2
    finally:
        if args.output:
            output.close()

    if emitted == 0:
        print("m68000_json_adapter: no vectors selected", file=sys.stderr)
        return 1
    print(f"m68000_json_adapter: emitted {emitted} vectors", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
