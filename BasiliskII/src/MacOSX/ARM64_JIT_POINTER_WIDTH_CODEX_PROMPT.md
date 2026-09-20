# Codex Prompt: Fix ARM64 JIT Pointer-Width Truncation on macOS

You are working on the Basilisk II macOS emulator in:

`/Users/user2/Documents/macemu/BasiliskII/src/MacOSX`

The goal is to make the experimental AArch64 JIT safe on Apple Silicon macOS, especially when executable/JIT memory is allocated above the 4 GB boundary.

## Current diagnosis

The current backend distinguishes `PC_P` conceptually as a 64-bit host pointer, but several APIs still describe operations as `W4`/32-bit operations. On AArch64, writing a `W` register clears the upper 32 bits of the corresponding `X` register.

The highest-risk path is:

```cpp
MIDFUNC(2, mov_l_ri, (W4 d, IMPTR s))
{
    set_const(d, s);
}
```

followed by the raw emitter:

```cpp
LOWFUNC(..., compemu_raw_mov_l_ri, (W4 d, IM32 s))
{
    LOAD_U32(d, s);
}
```

The generator emits cases such as:

```cpp
mov_l_ri(PC_P, (uintptr)comp_pc_p);
```

If that constant is materialized through `LOAD_U32`, a high host pointer is truncated. A similar risk exists in `lea_l_brr`, where constant folding calls `mov_l_ri()` before reaching the later `PC_P` 64-bit special case.

This is not an automatic truncation by the CPU. It is a backend contract violation: a pointer-valued virtual register is reaching an emitter that intentionally uses a 32-bit `W` operation.

## Relevant files

Inspect these first:

- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/compemu.h`
  - `jit_reg_value_t`, `reg_status`, virtual register metadata, and declarations.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/compemu_arm.h`
  - `PC_P`, virtual register numbering, allocator structures, and spill declarations.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/codegen_arm64.cpp`
  - Raw AArch64 emitters, especially `compemu_raw_mov_l_ri`, `compemu_raw_mov_l_rr`, loads, stores, spills, and reloads.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/codegen_arm64.h`
  - `W` versus `X` instruction macros and direct-memory addressing helpers.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/compemu_midfunc_arm64.cpp`
  - `mov_l_ri`, `mov_l_rr`, `lea_l_brr`, indexed LEA, add/subtract operations, and all `PC_P` special cases.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/compemu_midfunc_arm.h`
  - Midfunc signatures and declarations for pointer-width operations.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/compemu_support_arm.cpp`
  - `get_n_addr`, code allocation, block entry/exit, chaining, spills, and `MAP_JIT` write protection.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/gencomp.c`
  - Generated call sites for `PC_P`, `get_n_addr`, branches, JSR/JMP, LEA, MOVE, and ROM access.
- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/gencomp_arm.c`
  - ARM-specific generator output and compatibility with the checked-in generated sources.
- `BasiliskII/uae_cpu_2026.xcodeproj/generated/compemu.cpp`
  - Confirm that generated output uses the intended pointer-aware APIs.
- `BasiliskII/src/MacOSX/config.h`
  - Confirm ARM64 JIT is explicitly opt-in; currently `USE_JIT` is enabled only for x86_64.
- `BasiliskII/BasiliskIITests/BasiliskIITests.swift`
  - Existing source-contract and MAP_JIT tests.

## Required invariants

Enforce these explicitly:

1. `PC_P` is the only virtual register permitted to hold a persistent native host pointer.
2. `PC_P` must use 64-bit `X` operations for moves, arithmetic, spills, reloads, and memory writes.
3. Ordinary M68K integer/EA virtual registers are modulo-2^32 values and must use 32-bit `W` operations.
4. Pointer width must never be inferred from numeric magnitude, such as `value > 0xffffffff`.
5. `get_n_addr()` and `get_n_addr_jmp()` must write pointer values only to pointer-aware destinations, normally `PC_P`.
6. `regs.pc_p`, `regs.pc_oldp`, and `regs.pc` must be updated consistently at dispatcher and block boundaries.
7. Every generated code path that writes `PC_P` must use a pointer-width API.

## Implementation plan

### 1. Audit all unsafe `PC_P` paths

Use `XcodeGrep` to find every use of:

- `PC_P`
- `mov_l_ri`
- `mov_l_rr`
- `lea_l_brr`
- `lea_l_rr_indexed`
- `add_l`, `add_l_ri`, `sub_l_ri`
- `get_n_addr`
- `STR_w`, `LDR_w`, `STR_x`, `LDR_x`
- spill/reload helpers

For each call, classify the value as either guest 32-bit or host pointer 64-bit. Do not infer the classification from the value itself.

### 2. Add explicit pointer-width midfunc contracts

Add or complete APIs with names such as:

- `mov_ptr_ri(W4 d, IMPTR value)`
- `mov_ptr_rr(W4 d, RR4 s)`
- `add_ptr_ri(W4 d, IM32 offset)`
- `add_l_ri_hostptr(W4 d, IMPTR base)`

Use the project’s existing naming and `MIDFUNC` conventions. The destination type may remain the project’s virtual-register type, but the implementation must select `X` instructions when the contract is pointer-width.

Do not change ordinary `mov_l_ri` or `arm_ADD_l_ri` into pointer operations. Their guest arithmetic must remain modulo 2^32.

### 3. Fix constant materialization

Ensure `PC_P` constants cannot reach `LOAD_U32`.

At minimum:

- Make `mov_l_ri()` dispatch to a pointer-width emitter when `d == PC_P`, or replace those call sites with `mov_ptr_ri()`.
- Ensure `compemu_raw_mov_l_ri()` has a genuinely 64-bit counterpart using `LOAD_U64`.
- Move the `PC_P` branch in `lea_l_brr` before constant folding, or make constant folding call the pointer-aware operation.
- Audit indexed LEA and all constant propagation involving `PC_P`.

### 4. Fix spill/reload width

Verify that a `PC_P` spill or reload uses 64-bit stores and loads. If the allocator currently has no explicit width metadata, add the smallest safe mechanism possible—prefer a pointer-register predicate based on `PC_P` if the invariant truly allows no other pointer virtual registers.

Do not broadly convert all virtual-register spills to 64-bit operations, because that would weaken the guest-value contract and may alter allocator behavior.

### 5. Audit `get_n_addr()` and generated handlers

Confirm that all pointer-producing paths write to `PC_P` or use an explicit pointer destination. Pay special attention to:

- JSR/JMP and return-address construction
- exception/trap dispatch
- LEA
- MOVE/MOVEM native-address helpers
- ROM and RAM access helpers
- block chaining and branch target construction

If a temporary receives a host pointer, either route it through `PC_P` or introduce explicit pointer metadata. Do not cache a pointer in an ordinary guest `W4` virtual register.

### 6. Fix ARM64 JIT enablement separately

The current macOS configuration enables `USE_JIT` only for x86_64:

```cpp
#if defined(CPU_x86_64) && defined(JIT) && !defined(USE_JIT)
#define USE_JIT 1
#endif
```

Do not silently enable ARM64 for all builds. Add an explicit opt-in build definition, for example `JIT` or a dedicated `AARCH64_JIT_EXPERIMENTAL`, and update the configuration so ARM64 can be tested intentionally.

### 7. Preserve macOS W^X correctness

Keep the existing Apple Silicon strategy:

- `MAP_JIT` allocation
- `pthread_jit_write_protect_np(false)` while emitting or patching
- instruction-cache invalidation after writes
- `pthread_jit_write_protect_np(true)` before execution
- `com.apple.security.cs.allow-jit` in the signed application entitlements

Do not fall back to non-`MAP_JIT` executable mappings on macOS ARM64.

## Tests to add or strengthen

Add focused tests for:

1. Loading a pointer above `0xffffffff` into `PC_P` and verifying the emitted code uses `X` operations.
2. Moving a high pointer from one pointer-aware register to another.
3. Spilling and reloading `PC_P` without truncation.
4. Constant-folded `lea_l_brr(PC_P, ...)` preserving the high half.
5. Guest `ADD.L` with bit 31 set remaining modulo 2^32.
6. `get_n_addr()` producing a high host pointer and storing it in `PC_P`.
7. MAP_JIT allocation, patching, cache invalidation, and execution.
8. Interpreter/JIT differential execution for short programs containing ROM reads, LEA, MOVE, JSR/JMP, and returns.

Prefer Swift Testing for existing Swift tests and small C++/runtime self-tests where emitted instruction inspection is required.

## Validation sequence

1. Run source-level contract tests.
2. Build the ARM64 CPU target.
3. Enable the experimental ARM64 JIT explicitly.
4. Run the MAP_JIT self-test.
5. Run a minimal JIT program that forces a code or ROM pointer above 4 GB.
6. Run interpreter versus JIT differential tests.
7. Run System 7 and System 8 boot workloads.
8. Only after correctness is stable, investigate direct chaining and zero-RAM compile skipping.

## Constraints

- Keep changes limited to ARM64 JIT pointer-width correctness and the minimum configuration needed to test it.
- Do not redesign the entire allocator unless the audit proves that `PC_P`-only metadata is insufficient.
- Do not use numeric pointer magnitude as a type test.
- Do not claim success from a normal x86_64 build; ARM64 JIT must be compiled and exercised.
- Report every remaining unverified assumption, especially entitlements, generated-source freshness, and physical Apple Silicon runtime results.

Begin by auditing the files above and producing a table of every path that can write, move, spill, reload, or arithmetic-update `PC_P`. Then implement the smallest safe fix and build/test it.
