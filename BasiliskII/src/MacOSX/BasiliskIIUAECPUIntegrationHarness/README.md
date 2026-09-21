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

The current program installs `MOVEQ #5,D0` followed by `M68K_EXEC_RETURN` at guest address `0x1000`. It runs the fixture once through `m68k_do_execute` and once through `m68k_compile_execute`, then compares D0, PC, and SR. A successful run ends with:

```
UAE_CPU_INTERPRETER_PASS
UAE_CPU_JIT_PASS
UAE_CPU_INTEGRATION_PASS
```

## Fixture boundary

`runtime_fixture.cpp` supplies only deterministic services required to link and execute this controlled case. It does not emulate BasiliskII ROM services, devices, traps, or the full application lifecycle. The fixture is therefore suitable for instruction-level differential tests, not ROM boot.

The next increment will run the same memory/register fixture through `m68k_compile_execute` and compare the complete CPU-state snapshot against the interpreter result.
