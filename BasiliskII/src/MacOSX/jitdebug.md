# Basilisk II ARM64 JIT Debugging Handoff

## Scope and source layout

- Repository root: `/Users/user2/Documents/macemu/BasiliskII`
- Active macOS sources: `src/MacOSX/compiler/`
- ARM64 CPU library project: `src/MacOSX/uae_cpu_2026.xcodeproj`
- Main application project: `src/MacOSX/BasiliskII.xcodeproj`
- Staging/generator tree `src/uae_cpu_2026/compiler/` is not the source compiled by the active Xcode target.
- Relevant files:
  - `compiler/compemu_support.cpp`: outer compiled-execution loop and current safe-dispatch containment.
  - `compiler/compemu_support_arm.cpp`: block compiler, cache metadata, dispatcher-stub generation, and diagnostics.
  - `compiler/compemu_midfunc_arm64.cpp`: ARM64 branch patching.
  - `compiler/codegen_arm64.cpp`: generated ARM64 operations and trampoline helpers.
  - `compiler/arm64_branch_patch.h`: checked branch-instruction encoding.

## Original symptom

With the saved `jit=true` preference, Basilisk II enters the ARM64 JIT and then hangs or raises an illegal-instruction exception. Some hangs consume enough resources to destabilize the host computer.

The first reproducible failure occurred immediately after the initial interpreter/bootstrap operation:

- Last guest PC before native entry: `0200008c`.
- Cache tag handler was the generated `popall_execute_normal` stub.
- The generated dispatch subsequently transferred into zero/unwritten or unrelated executable memory.
- Zero-filled cache memory decodes as ARM64 `udf #0`.

## Verified preference and initialization behavior

- `jit=true` is read correctly.
- `UseJIT` is set.
- Execution enters `m68k_do_compile_execute()`.
- JIT initialization and executable cache allocation succeed.
- The first C-side block compilation succeeds.
- Generated handler tables are populated (previous count: 2,827 ARM handlers).
- This is not a preference-plumbing failure.

## Causes ruled out

### Entitlements and executable memory

- `MAP_JIT` allocation succeeds.
- The combined popall/cache allocation is executable.
- Apple write/execute switching completes.
- This is not an entitlement or executable-memory allocation failure.

### Instruction-cache flushing

- I-cache flush calls occur.
- Flush-site diagnostics were already present.
- Repeated runs fail after successfully executing freshly generated stubs.
- No evidence currently points to a missing cache flush as the primary cause.

### Empty or stale generated opcode tables

- Generated tables contain thousands of handlers.
- The generator pipeline works.
- The Makefile `verify` dependency issue was fixed so a fresh verify builds all prerequisites.

### Source file being overwritten

- Xcode compiles `src/MacOSX/compiler/compemu_support_arm.cpp`.
- Generator targets produce `compemu_arm.cpp`, `compstbl_arm.cpp`, and `comptbl.h`.
- No build phase was found that overwrites `compemu_support_arm.cpp`.

### Optimization level 2 alone

- Forcing maximum optimization level 1 reproduced the invalid dispatch.
- Default optimization level was restored to 2.
- The defect is not specific to level-2 chaining.

### Static ARM64 branch encoding

- `write_jmp_target()` validates alignment, supported instruction class, and architectural range.
- Completed-block dumps decode unconditional `B` targets.
- Observed branches in the first blocks pointed to valid popall stubs or emitted code.
- A diagnostic initially trapped a target equal to the live emitter frontier. That was a valid forward patch, so patch-time validation now permits `target == written_end`.
- No invalid static branch has yet been observed after correcting that boundary rule.

## Runtime observations

### Safe-dispatch run exposed and fixed a condition-code abort

After native entry was contained, a longer safe-dispatch run reached a deterministic
compiler abort while compiling guest opcode `0x5dc7` (`Scc`, condition LT):

```text
jit_abort("unsupported legacy x86 condition code %d")
legacy_x86_cc_to_native(cc=10)
setcc(d=16, cc=10)
op_5dc0_0_comp_nf(opcode=24007)
```

This was not another hang or bad branch target. The old generator uses x86 parity
condition 10 for M68K `LT` and no-parity condition 11 for M68K `GE`, because its x86
flag representation stored `N xor V` in PF. AArch64 has no parity condition, but native
`LT`/`GE` test the equivalent architectural N/V relationship. The ARM64 legacy
compatibility map omitted these two translations.

The compatibility layer now maps:

- legacy condition 10 to `NATIVE_CC_LT`;
- legacy condition 11 to `NATIVE_CC_GE`.

The existing 12/13 mappings remain the ordinary x86 signed LT/GE mappings. The ARM64
FPU condition path is separate and uses `fp_fscc_ri`, so this integer compatibility fix
does not reinterpret floating unordered/parity predicates.

Both the `uae_cpu_arm64` library and `BasiliskII` app rebuilt successfully after this
change. A subsequent run passed the former abort point and remained alive for 54 seconds,
reaching approximately 7.50 million safe dispatches without `BAD_TARGET`, `JIT_FAILSAFE`,
or another abort. The run was then stopped manually to avoid needless host load.

### First failing native transition

A representative run used:

- Popall base: approximately `0x12312c000`.
- Translation cache: approximately `0x12312d000-0x12332d000`.
- Valid selected handler: `popall+0x74`.
- Invalid PC: approximately `0x123418000`, beyond the allocated cache.

The exact addresses vary with ASLR, but the shape is repeatable.

Registers at one failure included:

- `x8`: valid popall base.
- `lr`: valid return address in `m68k_do_compile_execute()`.
- `pc`: invalid address outside the JIT arena.

This argues against a corrupted C return address and points to a runtime branch target constructed or consumed after entering generated code.

### Popall trampoline disassembly

The generated entry:

1. Saves `x27` and `x28`.
2. Calls optional FPU-shadow synchronization.
3. Initializes JIT base registers.
4. Calls `jit_lookup_dispatch_handler()`.
5. Branches through the returned handler.

The `popall_execute_normal` exit:

1. Optionally synchronizes FPU state.
2. Restores preserved registers.
3. Loads an absolute C target from a literal.
4. Tail-branches to `execute_normal`.

The inspected literal target and cache-selected handler were valid. The invalid PC appears only after runtime entry.

### C-boundary cache-miss experiment

Routing cache misses directly through `execute_normal()` from the existing C frame eliminated the immediate `0200008c` failure and advanced through many guest PCs while compiling blocks.

When the first active compiled handler was finally selected, native entry reproduced the same out-of-arena transfer.

Conclusion: the shared `pushall_call_handler` native-entry path is unsafe for both cache-miss stubs and real compiled blocks. The cache lookup itself is returning valid pointers.

## Current diagnostics

Set `B2_JIT_DIAG=1`.

### Bad-target trap

`jit_diag_bad_target_breakpoint()` is a no-inline C symbol suitable for an Xcode symbolic breakpoint. It logs:

- diagnostic site;
- patch address;
- target address;
- guest PC;
- cache bounds;
- written-code frontier;
- source instruction.

It traps when diagnostics detect:

- a target inside allocated but unwritten cache;
- a handler/native-block target outside the valid popall and written-cache regions.

Relevant log:

```text
JIT_DIAG BAD_TARGET site=...
```

### Dispatch logs

The outer dispatcher logs the guest PC, host PC pointer, cacheline, handler, and block metadata. High-frequency logs are limited to the first 200 dispatches and then powers of two.

```text
JIT_DIAG pre_native_dispatch count=...
JIT_DIAG safe_c_dispatch count=...
```

### Ten-second native-entry failsafe

Immediately before entering generated native code, a watchdog records the guest PC and sets a ten-second monotonic deadline. It disarms when native code returns.

If native code fails to return, a detached watchdog thread logs:

```text
JIT_FAILSAFE native dispatch exceeded 10 seconds guest_pc=XXXXXXXX; pausing process PID
```

It then sends `SIGSTOP` to the Basilisk II process. This pauses rather than kills the process, preserves registers/stacks for LLDB, and prevents a runaway JIT loop from monopolizing the host. Continuing in LLDB grants a fresh window.

The watchdog is armed only around actual native JIT execution and does not affect the non-JIT interpreter.

## Current safety containment

On ARM64, native dispatch is disabled by default because the common entry trampoline is not yet trustworthy. JIT blocks can still be compiled and inspected, but execution uses the safe C `execute_normal()` boundary.

Set this only for controlled debugger experiments:

```text
B2_JIT_UNSAFE_NATIVE_DISPATCH=1
```

That re-enables native trampoline entry. The ten-second failsafe remains armed around it.

This containment prevents the illegal-instruction hang, but it is not the final performance fix because compiled blocks are not executed natively by default.

## Build-system discovery

Building only the active `BasiliskII` scheme did not always rebuild the ARM64 static library after CPU-source edits. LLDB line mappings and missing new log strings revealed that the app was sometimes linked against the previous library.

Reliable rebuild sequence:

1. Switch to the `uae_cpu_arm64` scheme.
2. Build.
3. Switch back to `BasiliskII`.
4. Build and run.

Always use this sequence while debugging CPU-library changes.

## Remaining leading theories

### 1. Generated entry-trampoline ABI defect

Most likely because valid C-side handlers become invalid control flow only after `pushall_call_handler` entry.

Inspect:

- stack alignment at every C call;
- balance of manual `x30` saves/restores;
- preservation of `x27` and `x28`;
- assumptions made by compiled handlers about initialized registers;
- whether a helper or Apple runtime uses a register treated as scratch by the emitter.

### 2. Use of platform-reserved register x18

The ARM64 emitter uses `R18_INDEX`/x18 as temporary storage in several places. On Apple platforms x18 is platform-reserved. Calls into C or system code may depend on or mutate it.

This has not been proven as the transfer cause, but it deserves a targeted audit:

- enumerate all x18 uses in entry/exit stubs;
- replace x18 with an ordinary caller-saved register for an A/B build;
- ensure no live value is expected across a C call.

### 3. Compiled-block entry contract mismatch

The block handler may assume register or flag state different from what the shared entry provides. Add a small generated guard immediately before branching to the handler that stores key registers and the selected target into a C-visible diagnostic record.

Capture:

- selected handler;
- SP before and after each save/call;
- x27/x28 base values;
- x30;
- NZCV;
- first 8 instructions at the handler.

### 4. Runtime indirect branch not covered by static target checks

Static `B` patches validate correctly, but runtime `BR`/literal-loaded targets can still escape. Extend completed-stub diagnostics to decode:

- `LDR literal` plus following `BR`;
- `BLR` helper targets;
- every runtime-loaded target immediately before use.

### 5. FPU-shadow synchronization interaction

The entry and exits may call FPU synchronization helpers. Test:

```text
B2_JIT_DISABLE_ENTRY_FPU_SYNC=1
```

Compare the first invalid transition and stack/register snapshots. This is a diagnostic experiment, not a proposed permanent fix.

### 6. Pointer authentication or indirect-branch policy interaction

Less likely, because unsigned JIT entry executes several instructions and the failure target varies as an ordinary address. Still consider arm64e/PAC and BTI behavior if the trampoline works on non-Apple ARM64 or under a different architecture slice.

### 7. Safe-dispatch recompilation/cache churn (separate performance issue)

The validation run showed nearly one compilation per C dispatch, mostly recompiles,
one-instruction blocks, and thousands of `compile_block:cache-full-post` flushes. At 54
seconds the counters were approximately:

```text
dispatch=7497554 compile=7497509 fresh=1469399 recomp=6028110
flush_hard=4937 avg_block=1.0 insn peak_cache=1186451.3KB
```

This does not reproduce the native control-flow escape, and the process continued to
run, but it makes the C-dispatch containment expensive. The likely reason is that
`execute_normal()` is being used as a one-instruction safety boundary while compiled
handlers cannot be consumed natively, so blocks are repeatedly reconsidered/recompiled.
Treat this as a containment-mode performance issue, not evidence that cache flushing
caused the original invalid branch.

The displayed `peak_cache` value is misleading: it is derived from
`current_cache_size`, which is incremented for every emitted block but reset only at JIT
initialization/teardown, not when the translation cache is flushed. It is cumulative
emitted code across cache generations, not live resident cache usage. This is a
diagnostics-accounting issue and does not mean the process held a 1.1 GB executable cache.

## Recommended next experiments

1. Keep the safe default enabled for ordinary runs.
2. Under LLDB, set `B2_JIT_UNSAFE_NATIVE_DISPATCH=1`.
3. Add symbolic breakpoints on:
   - `jit_diag_bad_target_breakpoint`;
   - `jit_lookup_dispatch_handler`;
   - the ten-second `JIT_FAILSAFE` stop, observed through SIGSTOP.
4. Single-step the shared entry from its final `BR x0` into the first compiled handler.
5. Record SP, LR, x18, x27, x28, and the branch register before every indirect transfer.
6. A/B test removal of x18 from entry/exit code.
7. A/B test with entry FPU synchronization disabled.
8. If the first handler is entered correctly, instrument its first indirect exit rather than adding more cache-lookup diagnostics.

## Current status

- ARM64 CPU library builds successfully.
- Basilisk II app builds successfully.
- Safe C-dispatch containment prevents the immediate native-entry crash.
- Native JIT execution remains experimental and disabled by default.
- Ten-second SIGSTOP failsafe protects controlled native-dispatch experiments.
- The legacy condition-code 10/11 compiler abort is fixed and the corrected run passed
  7.50 million safe dispatches over 54 seconds.
- Safe mode currently exhibits heavy one-instruction recompilation and cache flush churn;
  it is suitable for diagnosis/containment, not normal-performance JIT use.
- The remaining bug is localized to generated native entry/execution control flow, most likely the common trampoline ABI or its first compiled-handler contract.
