# ARM64 JIT Next Goal

## Objective

Make the ARM64 JIT execute validated native blocks directly, while retaining
correct interpreter fallback for unsupported instructions, interrupts,
self-modifying code, and invalidated blocks.

## Current diagnosis

The cache is populated, but the normal ARM64 path frequently executes cached
blocks through `exec_nostats_limited()`. This gives the system compiled blocks
without receiving native execution speed. The global native-dispatch gate is a
safety mechanism, not a complete block lifecycle.

The first controlled native test entered and returned from guest PC
`0x0400d418` with 64-bit host pointers intact. `countdown`, `spcflags`, and
the interrupt state survived the return, and no `BAD_TARGET` was observed.

## Architecture to implement

Use per-block eligibility instead of a global native/interpreter choice:

1. Look up the block by guest PC.
2. Validate its checksum and lifecycle state.
3. Enter the compiled block directly only when it is marked native-safe.
4. Route unsupported operations and interrupt safepoints through controlled
   C/interpreter side exits.
5. Chain directly only between validated native-safe successor blocks.
6. Return to the dispatcher for unknown successors, then patch the edge after
   the successor is compiled and validated.
7. Invalidate both the block and inbound direct edges after guest writes.

## Safety invariants

- `PC_P` remains the persistent 64-bit native host-pointer virtual register.
- Pointer moves, arithmetic, spills, reloads, and memory accesses remain
  64-bit.
- Guest M68K values remain modulo 2^32.
- Pointer type is never inferred from numeric magnitude.
- `MAP_JIT` and `pthread_jit_write_protect_np()` remain intact.
- ARM64 native execution remains explicitly opt-in.
- Unsafe native dispatch must not be enabled globally as the correctness fix.

## Acceptance tests

- Stable-block repeated execution increases cache hits and does not recompile
  on every dispatch.
- A loop remains cached and returns through a valid safepoint.
- Self-modifying RAM invalidates the block and direct inbound edges.
- ROM writes and normal RAM writes preserve invalidation behavior.
- High host pointers above 4 GB remain intact.
- Interpreter/JIT differential tests remain equivalent.
- Native and safe dispatch can be compared independently.
- Real boot and deterministic MacBench workloads complete without hangs,
  aborts, pointer-target failures, or excessive cache resets.

