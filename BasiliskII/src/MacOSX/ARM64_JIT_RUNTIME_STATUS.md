# ARM64 JIT runtime checkpoint

The experimental ARM64 JIT now links into the BasiliskII application and has
been observed executing on Apple Silicon.  A run with `B2_JIT_DIAG=1` produced
native dispatch and compilation counters, so this is beyond a compile-only
checkpoint.

Observed status from a roughly two-minute run:

- `dispatch` and `exec_normal` reached tens of millions of calls.
- `compile` tracked almost every dispatch.
- `fresh` compilation was below one million while `recomp` reached tens of
  millions.
- `cache_hit` remained only in the thousands.
- `flush_hard` repeatedly increased at `compile_block:cache-full-post`.
- Average compiled blocks were only about 3 instructions.
- `opt>0` remained zero.
- `native_allowed=0`, so the run used the safe C dispatch path.

Interpretation: JIT execution is active, but the translation cache is
thrashing and blocks are being recompiled excessively.  This explains the
poor performance and is the next debugging target.  The repeated icache flush
messages are cache maintenance after invalidation, not link failures.

Diagnostics are enabled with:

```text
B2_JIT_DIAG=1
```

`B2_JIT_UNSAFE_NATIVE_DISPATCH=1` enables the experimental native dispatch
path for performance comparison only; it should not be used as the correctness
baseline.
