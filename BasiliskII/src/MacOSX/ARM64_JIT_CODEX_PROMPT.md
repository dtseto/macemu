# Paste-ready Codex prompt

Continue the Basilisk II macOS ARM64 JIT debugging work in
`/Users/user2/Documents/macemu/BasiliskII/src/MacOSX`.

Read `ARM64_JIT_NEXT_GOAL.md` first. The current diagnostic commit records that
the ARM64 JIT populates its translation cache but often executes cached blocks
through `exec_nostats_limited()`, so the next goal is safe per-block native
eligibility and direct chaining.

Use PocketShaver/macemu-jit and the working Basilisk II x86 JIT as architectural
references. Port block lifecycle algorithms, not architecture-specific
assembly. Inspect block lookup, `blockinfo` state, checksum validation,
`handler_to_use`, direct edges, side exits, invalidation, cache flushing,
interrupt safepoints, and MAP_JIT write-protection transitions.

Implement the smallest safe step:

1. Add explicit per-block native-capability/state rather than relying only on
   the global `B2_JIT_UNSAFE_NATIVE_DISPATCH` gate.
2. Keep interpreter fallback for unsupported instructions, invalid blocks,
   checksum failures, interrupts, and unvalidated successor edges.
3. Allow direct native entry/chaining only for blocks proven to return with the
   required ARM64 register, flag, PC_P, countdown, and `spcflags` contract.
4. Preserve all pointer-width invariants and keep native execution explicitly
   opt-in.
5. Extend diagnostics for block eligibility, lookup hit/miss, native entry and
   return, side-exit reason, edge patching, invalidation, cache reset, and
   MAP_JIT transitions.

Do not edit the Xcode `.pbxproj` file. Build the ARM64 library and app with
Xcode, run a real boot or deterministic 68k workload, and compare counters
against the existing baseline. Keep the normal scheme in safe dispatch mode
unless a controlled experiment explicitly enables one allowlisted guest PC.
Do not claim success from compilation alone. Report changed files, tests,
before/after counters, remaining risks, and whether a commit is appropriate.
