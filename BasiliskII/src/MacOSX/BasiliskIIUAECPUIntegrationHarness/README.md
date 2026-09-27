# BasiliskII UAE CPU Integration Harness

This target is the production-integration seam for the 68k semantics work. It links the real ARM64 UAE CPU archive against a deliberately small deterministic runtime fixture.

## Current phase

The passing probe validates:

- production CPU headers and register layout;
- the real `init_m68k` and `exit_m68k` entry points;
- static linking of the ARM64 UAE CPU archive;
- MPFR/GMP FPU dependencies;
- runtime-service resolution for mutexes, preferences, interrupts, timing, VM allocation, and emulation-op dispatch;
- execution of one real instruction through the interpreter;
- execution of the identical instruction stream through the production ARM64 JIT;
- differential comparison of D0, PC, and SR between both paths;
- bounded termination through the production `M68K_EXEC_RETURN` emulation opcode.

The current program installs five bounded programs at separate guest addresses:

- `MOVEQ #5,D0`;
- `ADDI.L #1,D0`;
- `SUBI.L #1,D0`;
- `ANDI.L #$0f0f0f0f,D0`;
- `ORI.L #$0f0f0f0f,D0`.
- `MOVE.L D0,(A0)`;
- `MOVE.L (A0),D0`;
- `MOVE.L (A0)+,D0`.
- `MOVE.L -(A0),D0`.
- a multi-instruction `MOVEQ`/`ADDI.L`/`SUBI.L` ALU sequence.
- absolute `JSR`/`RTS` with stack restoration.
- boundary-value `ADDI.L #-1` and `SUBI.L #1` borrow cases with explicit X/C expectations.
- `MOVEQ #-1,D0` sign extension with explicit N flag expectation.
- wraparound, mixed-bit, and zero-origin logical/arithmetic operand variants.
- a 127-iteration `SUBQ.L`/backward-`BNE.S` decrement loop.
- `EXT.L` sign extension from a negative word and `SWAP` byte-order transformation.
- unary `NEG.L`, `NOT.L`, and `TST.L` cases with explicit CCR expectations.
- byte- and word-width immediate moves that preserve upper D0 bits.
- byte-width memory store with explicit big-endian memory validation.
- word-width memory store and byte-width load with upper-bit preservation.
- PC-relative long load from a literal embedded after the instruction stream.
- indexed `d8(A0,D1)` long load.
- indexed load with a nonzero D1 displacement contribution.
- multi-instruction long load/add/store sequence with memory writeback.
- 32 generated four-instruction arithmetic programs at unique guest PCs.
- taken and not-taken `BEQ.S` cases;
- an unconditional `BRA.S` over a skipped instruction.
- `MOVE.L d16(A0),D0`;
- `MOVE.L abs.L,D0`.
- `CMPI.L` followed by taken `BEQ.S` and `BNE.S` cases.
- signed-overflow `ADDI.L` and `SUBI.L` cases with explicit CCR expectations.
- `LSL.L #1,D0` and `LSR.L #1,D0` with full SR comparison.
- `TRAP #0` vector dispatch to a handler, including the supervisor stack frame.

Each program is run through `m68k_do_execute` and `m68k_compile_execute`. The interpreter now runs to the explicit sentinel for multi-instruction cases. The harness compares all D registers, all A registers, PC, SR, and the guest data word, and also checks the expected D0 result, A0 result, memory result, and retired PC. A successful run ends with:

Each case is also executed twice through the JIT at the same guest PC to verify basic translated-block reuse produces the same state.

## Production performance probes

The harness now runs with a 68020 CPU model and a bounded 1 MB production translation cache. It adds three non-correctness probes:

- `UAE_CPU_CACHE_PRESSURE` compiles 12,000 distinct guest blocks and verifies every result after the real cache reaches its wrap/flush path;
- `JIT_TEST_DISPATCH` reports production direct-entry counters, dispatcher entries, fresh/recompiled blocks, cache misses, hard flushes, emitted instruction/code bytes, and peak cache usage;
- `UAE_CPU_BENCH` measures 1,000 warmed interpreter and JIT loop samples. The benchmark is informational and does not require a speedup.

The current Apple Silicon result is a correctness pass with one forced cache flush and roughly 755 KB peak code use. Direct-entry counters remain zero in this fixture, while `exec_nostats` is populated; this is useful evidence that the current production integration is still dispatcher-heavy and is not yet a direct-chaining performance baseline.

For broader semantics coverage, the external [SingleStepTests/m68000 corpus](https://github.com/SingleStepTests/m68000) is the selected source: it is MIT-licensed and contains per-instruction JSON tests generated from MAME. It is intentionally not vendored into this repository because the current checkout is about 182 MB of generated vectors. The next adapter should consume its `v1/*.json.bin` files, map initial RAM/register state into this fixture, and compare final state for both interpreter and JIT paths.

`m68000_json_adapter.py` is now the first adapter step. Run the upstream
`decode.py` once to convert a corpus `.json.bin` file, then normalize it:

```sh
python3 m68000_json_adapter.py /path/to/NOP.json -o /tmp/nop.jsonl --limit 100
```

For the 68000 indexed corpus, use the explicit 68020 adaptation mode:

```sh
python3 m68000_json_adapter.py /path/to/CLR.b.json \
    --adapt-indexed-for-68020 -o /tmp/CLR-68020.jsonl --limit 2500
```

Run the adapted corpus with the 24-bit bus fixture enabled:

```sh
B2_TEST_24BIT_ADDRESS=1 ./BasiliskIIUAECPUIntegrationHarness \
    --corpus /tmp/CLR-68020.jsonl
```

This clears the 68000 brief-index full-format and scale selector bits in the
emitted extension word. The original extension and the adaptation decision
remain in the JSON metadata, so the transformation is auditable and cannot be
mistaken for a native 68000 execution result.

Each output line preserves the initial/final register and RAM state, the
opcode extracted from the corpus test name, the first two instruction words,
and the source vector identity. Indexed vectors additionally carry the raw
extension word, index-register class, index size, and both the 68020 scale and
68000 scale interpretation. The selected SingleStepTests source is 68000-based,
while this harness currently uses a 68020 CPU model; keeping both values makes
that model mismatch explicit before a vector is enabled in the production C++
runner.

The integration executable now consumes that JSONL format directly for the
single-word-vector milestone:

```sh
./BasiliskIIUAECPUIntegrationHarness --corpus /tmp/m68000.jsonl
```

It restores the corpus registers and PC, runs the instruction once through
the interpreter and once through the production JIT, and compares all data
registers, address registers, PC, and condition-code/int-mask bits. Because
`M68K_EXEC_RETURN` is a privileged test sentinel, corpus vectors execute with
the S bit synthesized and the comparison masks only that synthetic bit. The
runner uses a 2 MB fixture for the normal integration/benchmark path and a
16 MB fixture for corpus mode. It currently skips odd or out-of-range PCs,
instructions longer than one prefetch extension word, and address-heavy
vectors whose effective-address setup cannot be proven safe without decoding
the addressing mode. Native 68000 indexed vectors remain skipped unless they
have been explicitly adapted to the 68020 brief format and the 24-bit fixture
is enabled. A7-based and high-address absolute-word (`xxx`) forms remain
skipped because they cross known stack-bank or fixture boundaries. Non-A7
predecrement (`-(An)`) and low-address absolute-word forms are exercised
normally; absolute-long forms remain outside this fixture because they require
more than one extension word. These are explicit coverage boundaries, not
semantic passes. Sparse initial and final RAM is replayed and checked for
supported vectors; direct `(An)`, displacement, postincrement, and
predecrement forms are exercised normally.

The complete adapted `CLR.b.json` file has also been validated: 1,973 of
2,500 vectors pass interpreter/JIT comparison, with 527 explicit skips for
odd PCs, unsupported A7/absolute boundaries, instruction-length limits, or
inconsistent corpus metadata. No adapted indexed vector produced a semantic
failure.

Example result from 400 mixed NOP/SWAP/EXT vectors:

```
UAE_CPU_CORPUS_RESULT total=400 passed=400 skipped=0 malformed=0 PASS
UAE_CPU_CORPUS_SKIPS pc=0 length=0 indexed=0 a7=0 absolute=0 memory=0 prepare=0
```

Corpus skips are also reported by reason. `indexed` identifies indexed `Xn`
forms reserved for the separate indexed-addressing milestone; `a7` identifies
stack-register forms; `absolute` identifies high absolute-word targets;
`memory` identifies vectors whose sparse state or accesses do not fit the
fixture; `metadata` identifies internally inconsistent corpus records; and
`pc`, `length`, and `prepare` identify structural fixture boundaries. This
keeps a passing supported subset from hiding changes in the unsupported
population.

The complete available files for these decoded SingleStepTests families have
now been validated: `NOP.json`, `SWAP.json`, `EXT.w.json`, `EXT.l.json`,
`AND.b.json`, `OR.b.json`, `SUB.b.json`, `ADD.b.json`, and `CLR.b.json`.
The 22,500-vector run passes all 17,797 supported vectors through interpreter/
JIT differential comparison, with 4,703 explicit fixture-boundary or corpus-
metadata skips and no malformed records. The full corpus run uses
`B2_TEST_24BIT_ADDRESS=1`; this is required because the 68000 vectors model a
24-bit bus and effective addresses may alias across the 16 MiB boundary.
The memory fixture now applies that bus mask consistently to direct memory
translation, matching the production JIT's 24-bit addressing behavior.

For reproducibility, concatenate the adapter outputs and run the combined
JSONL file with the 24-bit mode enabled. Corpus skip counts include
`metadata` records whose declared displacement and observed access address do
not agree, so they are not silently counted as semantic passes.

The adapter contract has a standalone Python test suite covering native versus
adapted indexed words, preserved source metadata, non-indexed vectors, and
malformed state/opcode rejection:

```sh
python3 -m unittest discover \
  -s BasiliskIIUAECPUIntegrationHarness -p 'test_*.py'
```

The complete family sweep can be reproduced with the regression runner. It
automatically enables 24-bit bus mode and applies the 68020 indexed adaptation
only to `CLR.b`:

```sh
python3 run_m68000_corpus_regression.py \
  --corpus-root /path/to/m68000/v1 \
  --harness /path/to/BasiliskIIUAECPUIntegrationHarness
```

The reproducible expansion command is:

```
python3 m68000_json_adapter.py --limit 100 -o /tmp/family.jsonl \
  /path/to/m68000/v1/NOP.json
./BasiliskIIUAECPUIntegrationHarness --corpus /tmp/family.jsonl
```

Repeat it for the approved family files above. Use the adapter without
`--adapt-indexed-for-68020` for these non-indexed families; use that option and
`B2_TEST_24BIT_ADDRESS=1` only for the separate indexed-addressing corpus.

```
UAE_CPU_INTERPRETER_PASS
UAE_CPU_JIT_PASS
UAE_CPU_INTEGRATION_PASS
```

## Fixture boundary

`runtime_fixture.cpp` supplies only deterministic services required to link and execute this controlled case. It does not emulate BasiliskII ROM services, devices, traps, or the full application lifecycle. The fixture is therefore suitable for instruction-level differential tests, not ROM boot.

The next increment will run the same memory/register fixture through `m68k_compile_execute` and compare the complete CPU-state snapshot against the interpreter result.
