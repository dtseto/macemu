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

```
UAE_CPU_INTERPRETER_PASS
UAE_CPU_JIT_PASS
UAE_CPU_INTEGRATION_PASS
```

## Fixture boundary

`runtime_fixture.cpp` supplies only deterministic services required to link and execute this controlled case. It does not emulate BasiliskII ROM services, devices, traps, or the full application lifecycle. The fixture is therefore suitable for instruction-level differential tests, not ROM boot.

The next increment will run the same memory/register fixture through `m68k_compile_execute` and compare the complete CPU-state snapshot against the interpreter result.
