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
- taken and not-taken `BEQ.S` cases;
- an unconditional `BRA.S` over a skipped instruction.
- `MOVE.L d16(A0),D0`;
- `MOVE.L abs.L,D0`.
- `CMPI.L` followed by taken `BEQ.S` and `BNE.S` cases.

Each program is run through `m68k_do_execute` and `m68k_compile_execute`. The interpreter now runs to the explicit sentinel for multi-instruction cases. The harness compares all D registers, all A registers, PC, SR, and the guest data word, and also checks the expected D0 result, A0 result, memory result, and retired PC. A successful run ends with:

```
UAE_CPU_INTERPRETER_PASS
UAE_CPU_JIT_PASS
UAE_CPU_INTEGRATION_PASS
```

## Fixture boundary

`runtime_fixture.cpp` supplies only deterministic services required to link and execute this controlled case. It does not emulate BasiliskII ROM services, devices, traps, or the full application lifecycle. The fixture is therefore suitable for instruction-level differential tests, not ROM boot.

The next increment will run the same memory/register fixture through `m68k_compile_execute` and compare the complete CPU-state snapshot against the interpreter result.
