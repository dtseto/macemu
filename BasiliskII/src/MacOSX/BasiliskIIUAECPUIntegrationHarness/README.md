# BasiliskII UAE CPU Integration Harness

This target is the production-integration seam for the 68k semantics work. It links the real ARM64 UAE CPU archive against a deliberately small deterministic runtime fixture.

## Current phase

The passing probe validates:

- production CPU headers and register layout;
- the real `init_m68k` and `exit_m68k` entry points;
- static linking of the ARM64 UAE CPU archive;
- MPFR/GMP FPU dependencies;
- runtime-service resolution for mutexes, preferences, interrupts, timing, VM allocation, and emulation-op dispatch;
- execution of one real interpreter instruction from a bounded guest-memory buffer.

The current program installs `MOVEQ #5,D0` at guest address zero, executes it through `m68k_do_execute`, and checks D0, PC, and CCR. A successful run ends with:

```
UAE_CPU_INTERPRETER_PASS
UAE_CPU_INTEGRATION_PASS
```

## Fixture boundary

`runtime_fixture.cpp` supplies only deterministic services required to link and execute this controlled case. It does not emulate BasiliskII ROM services, devices, traps, or the full application lifecycle. The fixture is therefore suitable for instruction-level differential tests, not ROM boot.

The next increment will run the same memory/register fixture through `m68k_compile_execute` and compare the complete CPU-state snapshot against the interpreter result.
