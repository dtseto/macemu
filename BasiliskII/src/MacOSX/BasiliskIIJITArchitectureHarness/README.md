# BasiliskII JIT Architecture Harness

This target validates the translation-block architecture independently of the 68k decoder, BasiliskII ROM state, and CPU semantics.

The toy guest supports:

- increment;
- decrement;
- jump;
- halt.

The harness checks:

- translation-block lookup and reuse;
- direct chaining for same-page targets;
- dispatcher fallback for cross-page targets;
- invalidation and recompilation;
- bounded cache eviction;
- compilation-failure handling without partially publishing a block.

A successful run ends with:

```
ARCHITECTURE_JIT_PASS
```

This harness intentionally models the translation and dispatch architecture with a toy guest. It does not claim that the ARM64 emitter or 68k semantics are correct; those are covered by the platform and future semantics harnesses.
