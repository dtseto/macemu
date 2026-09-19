# Basilisk II ARM64 JIT Baseline — Phase 0

Date: 2026-09-18

## Worktree

- Branch: `macosnewjit...origin/macosnewjit`
- HEAD: `2adb74d9 Restore universal x86 JIT build`
- Worktree was clean at the start of Phase 0.
- No production source changes were made in this phase.

## Schemes and targets

Schemes present:

- `BasiliskII`
- `uae_cpu_arm64`
- `uae_cpu_x86_64`
- `Generate ARM64 JIT Sources`
- `BasiliskIITests`

Targets:

- `BasiliskII` in `BasiliskII.xcodeproj`
- `uae_cpu_arm64` in `BasiliskII/uae_cpu_2026.xcodeproj`
- `uae_cpu_x86_64` in `BasiliskII/uae_cpu.xcodeproj`
- `Generate ARM64 JIT Sources` aggregate target

## Build results

All three requested baseline builds passed:

- `uae_cpu_arm64`: passed for `Any Mac (arm64)`.
- `uae_cpu_x86_64`: passed for `Any Mac (x86_64)`.
- `BasiliskII`: passed for `Any Mac (arm64, x86_64)`.

A later cache-hit x86 recheck hung in the Xcode build service and was terminated. The original x86 build completed successfully; the Xcode service still reported that later build as running when queried.

No runtime or regression tests were run in Phase 0; this phase is a build baseline only.

## Configuration snapshot

- `uae_cpu_arm64`: `CPU_aarch64=1 CPU_AARCH64=1 AARCH64_ASSEMBLY=1`; no `USE_JIT` or `JIT` definition; `LLVM_LTO=NO`; `ENABLE_HARDENED_RUNTIME=NO`; `ARCHS=arm64 x86_64` with `ONLY_ACTIVE_ARCH=YES`.
- `uae_cpu_x86_64`: `CPU_x86_64 JIT USE_JIT=1`; `LLVM_LTO=NO`; `ENABLE_HARDENED_RUNTIME=NO`; `ARCHS=x86_64`.
- `BasiliskII`: universal `arm64 x86_64`; `LLVM_LTO=NO`; `ENABLE_HARDENED_RUNTIME=NO`; `RUNTIME_EXCEPTION_ALLOW_JIT=NO`; `RUNTIME_EXCEPTION_ALLOW_UNSIGNED_EXECUTABLE_MEMORY=NO`.

## Current JIT/interpreter split

The ARM64 JIT implementation is preprocessor-disabled by the current target configuration. ARM64 JIT translation units are present in the build graph, but their `USE_JIT`-guarded contents produce no exported JIT symbols. The ARM64 archive contained empty-object warnings for `compemu_arm_build.o` and related JIT-only objects.

The x86 archive exported `compiler_init`, `compiler_exit`, `compiler_use_jit`, and `m68k_compile_execute`, confirming that the x86 JIT is compiled and linked.

ARM64 baseline compiler tasks included `compemu_arm_build.cpp`, `compemu_support_arm.cpp`, `compemu_support.cpp`, `compemu_fpp.cpp`, `gencomp_arm.c`, and `gencomp.c`; the interpreter tasks included `cpuemu.cpp`, `cpustbl.cpp`, and `cpufunctbl.cpp`.

The runtime path is guarded by `#if USE_JIT` in `basilisk_glue.cpp`; without that definition, `UseJIT` is a constant false and execution remains interpreter-only.

## Generated-source steps

- The workspace contains the `Generate ARM64 JIT Sources` aggregate scheme/target.
- The normal ARM64 build compiles `gencomp.c` and `gencomp_arm.c` and consumes generated interpreter sources such as `cpuemu.cpp`, `cpustbl.cpp`, and `cpufunctbl.cpp`.
- The x86 JIT build compiles generated `compemu.cpp`, `compstbl.cpp`, and `cpuemu.cpp` sources, together with `gencomp.c`.

## Existing warnings

Builds pass with pre-existing warnings. ARM64 warnings are concentrated in generated/interpreter code and legacy integer-width conversions, including `uintptr`/`uint32` conversions in `cpuemu.cpp`, `newcpu.cpp`, `jit_runtime_macosx.cpp`, and related files. There are also existing `DEBUG` redefinition, possibly-uninitialized, deprecated API, and empty-static-object warnings. These warnings were not changed in Phase 0.

## Phase 0 decision

Baseline gate: PASS for the three initial builds.

No production behavior changes are present. Phase 1 may begin, but the Xcode build-service hang should be cleared or independently reproduced before relying on a fresh x86 rebuild as a validation result.
