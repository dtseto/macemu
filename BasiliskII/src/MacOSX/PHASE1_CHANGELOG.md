# ARM64 JIT Bring-up — Phase 1

Date: 2026-09-18

## Files changed

- `BasiliskII/uae_cpu_2026.xcodeproj/compiler/compemu.h`
- `BasiliskII/uae_cpu_2026.xcodeproj/project.pbxproj` (through Xcode’s target-setting API)

## Symbols/configuration changed

- Added ARM64-only `B2_ARM64_JIT`, defaulting to `0` when absent.
- ARM64 `USE_JIT` now derives from `B2_ARM64_JIT` when `USE_JIT` is not otherwise defined.
- ARM64 target definitions retain `USE_XCODE=1 CPU_aarch64=1 CPU_AARCH64=1 AARCH64_ASSEMBLY=1` and add `B2_ARM64_JIT=0`.
- x86 definitions and source files were not changed.

## Validation

- ARM64 interpreter/default (`B2_ARM64_JIT=0`): passed.
- ARM64 opt-in JIT (`B2_ARM64_JIT=1`, temporary validation setting): passed.
- x86 interpreter (temporary removal of `JIT USE_JIT=1`): passed.
- x86 JIT (original `JIT USE_JIT=1` restored): passed.
- Universal `BasiliskII` application after final settings were restored: passed.
- Final ARM64 archive has no `compiler_*`/`m68k_compile_execute` JIT exports.
- x86 JIT archive exports `compiler_init`, `compiler_exit`, `compiler_use_jit`, and `m68k_compile_execute`.

## Notes and remaining risk

The ARM64 JIT source files remain in the target’s source list, but their implementation is guarded by `USE_JIT`; the default ARM64 archive remains interpreter-only. This phase isolates the compile-time switch and proves both values build. A future phase may refine source membership if needed, but no ARM64 JIT is enabled by default.

Phase 2 is safe to begin.
