# BasiliskII 68K Semantics Harness

This target is the first independent semantics layer for the 68k JIT work. It compares two separate implementations of a focused 68000 semantic subset:

- a reference interpreter implementation;
- a lowered/JIT-contract implementation using explicit 32-bit arithmetic and flag formulas.

The corpus covers:

- MOVEQ-style sign extension;
- immediate ADD, SUB, AND, OR, and EOR;
- CMP and TST flag behavior;
- post-increment and displacement effective addresses;
- conditional branch decisions;
- boundary-value and randomized flag testing.

A successful run ends with:

```
SEMANTICS_68K_PASS
```

This is deliberately independent of the BasiliskII ROM and the host code cache. It validates the architectural contract first. The next semantics increment should replace the lowered executor with calls into the real ARM64 translator and compare the same CPU-state snapshots against the interpreter.
