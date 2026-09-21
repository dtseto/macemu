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

Each output line preserves the initial/final register and RAM state, the
opcode extracted from the corpus test name, the first two instruction words,
and the source vector identity. This gives the eventual C++ differential
runner a stable input contract without copying the upstream corpus into git.

```
UAE_CPU_INTERPRETER_PASS
UAE_CPU_JIT_PASS
UAE_CPU_INTEGRATION_PASS
```

## Fixture boundary

`runtime_fixture.cpp` supplies only deterministic services required to link and execute this controlled case. It does not emulate BasiliskII ROM services, devices, traps, or the full application lifecycle. The fixture is therefore suitable for instruction-level differential tests, not ROM boot.

The next increment will run the same memory/register fixture through `m68k_compile_execute` and compare the complete CPU-state snapshot against the interpreter result.
