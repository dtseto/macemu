# Production JIT Integration Findings

The production integration harness currently proves that one instruction can run
through both the interpreter and the ARM64 JIT with identical architectural state.
The confirmed findings so far are:

- Guest memory must outlive the compiled code. A stack-local buffer allowed the
  JIT to retain invalid pointers after the test function returned.
- Starting at guest address zero exposed bootstrap and PC-state assumptions. The
  harness now places code at `0x1000` and initializes it with production
  `m68k_setpc()`.
- `SPCFLAG_BRK` stops the interpreter but is not a sufficient bounded stop for
  the JIT dispatcher. The JIT fixture uses the production `M68K_EXEC_RETURN`
  sentinel and an `EmulOp` termination hook.
- `compiler_init()` performs an initial `execute_normal()` bootstrap, so valid
  guest memory and PC state must exist before compiler initialization.
- The fixture must provide the runtime `EmulOp` behavior needed by the bounded
  test; this was a fixture issue, not a confirmed CPU semantic defect.
- Xcode must build the dedicated integration scheme. Building the default
  `BasiliskII` scheme produced a stale executable and a misleading result.

The current test passes with identical D0, PC, and SR for interpreter and JIT.
No 68k semantic defect has been confirmed yet. Arithmetic flags, effective
addresses, memory operations, branches, exceptions, pointer-width contracts,
translation-block chaining, and cache eviction remain to be tested.
