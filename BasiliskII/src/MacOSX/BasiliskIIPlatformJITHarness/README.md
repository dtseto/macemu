# BasiliskII Platform JIT Harness

This target validates the macOS Apple Silicon executable-memory layer independently of the 68k translator and translation-block manager.

The harness verifies:

- `MAP_JIT` allocation;
- per-thread `pthread_jit_write_protect_np` transitions;
- ARM64 instruction-cache synchronization;
- execution of generated ARM64 code;
- rewriting generated code in place;
- concurrent write/execute cycles on separate JIT regions;
- repeated allocation and release of JIT regions.

Build the `BasiliskIIPlatformJITHarness` scheme in Xcode, then run the resulting executable. A successful run ends with:

```
PLATFORM_JIT_PASS
```

The current stress configuration performs 2,000 cycles inside one process. The harness was also validated across 50 independent process launches.

This harness intentionally does not include BasiliskII CPU state, 68k decoding, generated UAE tables, translation blocks, or ROM initialization. A failure here indicates the macOS JIT platform layer must be fixed before debugging higher layers.
