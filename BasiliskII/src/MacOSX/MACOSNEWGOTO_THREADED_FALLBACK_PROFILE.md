# Threaded Interpreter Fallback Profile

Profile date: 2026-09-19
Branch: `macosnewgoto`
Mode: `B2_INTERP_THREADED_PROTO=1`, `B2_INTERP_THREADED_METRICS=1`
Validation: MOVE self-test enabled; no runtime crash or hang observed

The counts below are from the captured diagnostic run; subsequent commits add
diagnostics and a benchmark harness but do not provide a new timing result.

## Summary

The latest shutdown summary reported:

```text
threaded.dispatches=437643928
fallback=421264318
inline=16379610
fallback_percent=96.26
```

The fallback percentage is aggregate across the whole workload. NOP, MOVEQ,
EXT, and register-only MOVE remain inline when their exact opcode forms match;
unsupported forms continue to use `cpufunctbl[opcode](opcode)`.

The corresponding coarse family totals were:

| Family | Fallbacks | Share of fallbacks |
|---|---:|---:|
| Control flow | 137,503,952 | 32.64% |
| MOVE | 94,225,293 | 22.37% |
| Compare | 64,196,734 | 15.24% |
| Other | 74,126,392 | 17.60% |
| Arithmetic/logic | 31,198,196 | 7.41% |
| Test/clear/extend | 12,843,583 | 3.05% |
| Stack/multiple-register | 7,170,168 | 1.70% |

The console displayed the aggregate summary twice, but the counters above are
from one emitted metrics set and are not doubled.

## Current diagnostic controls

`B2_INTERP_THREADED_METRICS=1` now emits the top 100 exact fallback opcodes in
addition to the family totals. `B2_INTERP_THREADED_TABLE_SELFTEST=1` checks the
65,536-entry target table's fallback, NOP, and MOVEQ mappings at initialization
and reports the mapped/fallback counts. Keep both controls enabled only for
diagnostic runs; their counters and table scan are not suitable for timing
comparisons.

## Top fallback opcodes

The profiler emits the top 100 fallback opcodes at shutdown. It now also emits
coarse fallback-family totals using lines such as:

```text
B2_METRIC threaded.fallback_family_control_flow=...
B2_METRIC threaded.fallback_family_move=...
B2_METRIC threaded.fallback_family_compare=...
B2_METRIC threaded.fallback_family_arithmetic_logic=...
B2_METRIC threaded.fallback_family_test_clear_extend=...
B2_METRIC threaded.fallback_family_stack_multiple=...
B2_METRIC threaded.fallback_family_other=...
```

These are intentionally conservative profiling buckets, not proof that every
opcode in a bucket has identical semantics. The family decoder runs only when
`B2_INTERP_THREADED_METRICS=1` is enabled.

The table below records the first 16 from the latest captured run:

| Rank | Opcode | Count | Initial classification | Safe to inline next? |
|---:|---:|---:|---|---|
| 1 | `0xB0B8` | 44,009,584 | CMP memory form | No — memory operand |
| 2 | `0x64FA` | 36,742,283 | Conditional branch | No — PC behavior |
| 3 | `0x4E75` | 8,503,710 | RTS | No — stack and PC behavior |
| 4 | `0x6EFA` | 6,185,383 | Conditional branch | No — PC behavior |
| 5 | `0x6704` | 5,358,830 | Conditional branch | No — PC behavior |
| 6 | `0x4A11` | 4,406,513 | TST memory form | No — memory operand |
| 7 | `0xB3D6` | 4,358,139 | CMP memory form | No — memory operand |
| 8 | `0x2229` | 4,289,137 | MOVE memory form | No — memory/side effects |
| 9 | `0x60F0` | 4,279,887 | Branch | No — PC behavior |
| 10 | `0xD3C1` | 4,235,427 | ADD memory form | No — memory/side effects |
| 11 | `0x642C` | 4,233,425 | Conditional branch | No — PC behavior |
| 12 | `0xF620` | 3,803,687 | Coprocessor/line-F form | No — not classified safe |
| 13 | `0xF623` | 3,743,071 | Coprocessor/line-F form | No — not classified safe |
| 14 | `0x4E56` | 3,536,126 | LINK | No — stack-frame change |
| 15 | `0x4E5E` | 3,535,720 | UNLK | No — stack-frame change |
| 16 | `0x51CB` | 3,140,866 | DBcc | No — PC/control-flow behavior |

The console connector duplicated each shutdown line in its display; the counts
above are taken once from the emitted metrics summary, not added twice.

## Safe candidate TODO

These are candidates by semantics, not claims that they are frequent in this
profile. Add and validate one family at a time:

- `SWAP Dn`
- `TST Dn` only, excluding all memory forms
- `CLR Dn` only, excluding all memory forms
- Simple register-to-register arithmetic, after flag behavior is isolated

Do not inline the ranked fallback forms above without a separate state and
runtime validation plan.

## Deterministic interpreter benchmark

For repeatable dispatch/memory comparisons, use the separate no-JIT CPU
microbenchmark:

```sh
B2_BENCH_CPU=1 B2_BENCH_CPU_ITERATIONS=100000000 BasiliskII --config benchmark.conf
```

It runs a synthetic `MOVE.L`/`SUBQ.L`/`BNE.S` loop, verifies D0 reaches zero,
and reports elapsed nanoseconds and nanoseconds per guest instruction. The
iteration count defaults to 100,000,000. This profile contains no benchmark
timings yet, so comparisons should be recorded only after identical binaries,
iteration counts, and host conditions are used.
