# macosnewgoto Performance Audit

Audit date: 2026-09-18  
Branch: `macosnewgoto`  
Branch HEAD: `2adb74d9` (`Restore universal x86 JIT build`)  
Comparison source: `https://github.com/kanjitalk755/macemu` at `892eeb74` (`master`, 2026-09-18 checkout)  
Scope: Basilisk II sources and the macOS/Xcode path in this branch.

This file is the working inventory for the branch. Check it before starting another optimization pass.

## Executive summary

Already present in `macosnewgoto`:

- SDL3 video and audio source files are linked by the macOS Xcode target.
- ExtFS has both a directory-entry cache and a short-lived stat cache.
- The branch has NEON-backed framebuffer comparison, palette expansion, VOSF policy/diagnostics, and dirty-rectangle present skipping.
- The timing work includes monotonic-clock selection and separate precise-timer/60 Hz ownership logic.
- AArch64 flag assembly, atomic SPCFLAGS operations, STOP sleeping, and `pread/pwrite` are present.
- A generated computed-goto dispatcher and a small in-function threaded prototype exist, but neither is a complete production threaded interpreter.

Still missing or incomplete:

- The full interpreter still executes one function-pointer handler per opcode in the normal path.
- ARM/AArch64 byte swapping is now optimized in `BasiliskII/src/Unix/sysdeps.h`; target-generated code emits `rev`/`rev16`.
- `configure.ac` now adds conservative ARM baseline `-O3`/`-march` settings. CPU-specific `-mtune` and macOS-specific tuning remain intentionally unset; the macOS Xcode target has AArch64 defines but no LTO configuration.
- SDL2 now uses a shared non-blocking SPSC audio ring buffer; SDL3 continues to use its native SDL_AudioStream worker model. The default SDL2 device block remains preference-controlled rather than forced to 2048 frames.
- No VNC server/async VNC conversion implementation exists in this repository path.
- Network polling/batching and several timer/display architecture changes remain undone. A bounded regular-file disk read cache and the POSIX timer condition-variable conversion are now present in the working tree; they still need target runtime measurements before being treated as performance wins.

## Status legend

- **Present** — implemented in this branch and located below.
- **Partial** — a prototype, opt-in path, platform-specific path, or related optimization exists, but the audit item is not fully satisfied.
- **Missing** — no implementation found in the branch.
- **Not applicable** — the pasted item targets a subsystem/platform not built by this macOS path.
- **Verify** — source support exists, but runtime/build evidence is still needed before calling it complete.

## Main branch features requested

| Feature | Status | Evidence in `macosnewgoto` | Notes |
|---|---|---|---|
| SDL3 | **Present** | `BasiliskII/src/SDL/video_sdl3.cpp`, `audio_sdl3.cpp`; `BasiliskII/src/MacOSX/BasiliskII.xcodeproj/project.pbxproj:13-16,576-585,1214,1305` | SDL3 framework and both source files are in the Xcode target. |
| Goto/threaded interpreter | **Partial** | `BasiliskII/src/uae_cpu_2026/gencpu.c:2843-2946`; `newcpu.cpp:2204-2528`; `BasiliskII/src/MacOSX/Makefile.gencpu_2021:9` | Generator emits a 65,536-entry label table, but each generated label calls `cpufunctbl[n](opcode)` and returns. The in-function prototype directly handles only NOP/MOVEQ; all other opcodes fall back. Runtime activation is opt-in via `B2_INTERP_GENERATED_GOTO`/`B2_INTERP_THREADED_PROTO`. This is not yet a fully threaded opcode implementation or default fast path. |
| ExtFS directory cache/lookup | **Present** | `BasiliskII/src/extfs.cpp:115-203,259-260,403-429,1429,1540,1587` | Directory entries are cached and rebuilt when directory mtime changes. |
| ExtFS stat cache | **Present** | `BasiliskII/src/extfs.cpp:115-203,1365,1440,1493,1552,1634` | 128-entry TTL/LRU-like cache, failure TTL, invalidation, and optional metrics. |
| Dual timing | **Present, verify at runtime** | `BasiliskII/src/timer.cpp:268-346,597-605`; `BasiliskII/src/uae_cpu_2026/compiler/compemu_support.cpp:123-180` | Monotonic 60 Hz scheduling and precise Time Manager timing have explicit ownership/coordination logic. This is not the same as merging every video loop into one loop. |
| NEON checks | **Present** | `BasiliskII/src/SDL/video_neon.h:1-48`; `video_sdl2.cpp:2669-2852`; `video_sdl3.cpp:69` | AArch64 NEON comparison is used for dirty-region checks; non-AArch64 falls back to `memcmp`. |
| VOSF | **Present, policy-level** | `BasiliskII/src/CrossPlatform/video_vosf_policy.h`; `BasiliskII/src/SDL/video_sdl3.cpp:132-198` | Runtime policy/threshold and diagnostics exist. `config.h:45` shows `ENABLE_VOSF` is not enabled in the checked macOS configuration, so do not call VOSF “active” without a runtime/build test. |
| Palette expansion | **Present** | `BasiliskII/src/SDL/video_sdl3.cpp:980-1000,1010-1023`; `video_palette.h` | Indexed video uses a host palette expansion path instead of relying only on generic per-pixel SDL conversion. |
| Monotonic clock | **Present** | `BasiliskII/src/Unix/timer_unix.cpp:37-43,127-135`; `BasiliskII/src/timer.cpp:268-276` | macOS uses its monotonic Mach clock path; POSIX uses `CLOCK_MONOTONIC` when available. |

## Pasted audit checklist: current status

### Build/compiler

| ID | Status | Finding |
|---|---|---|
| B.1 | **Present/partial** | Xcode Release configurations use `GCC_OPTIMIZATION_LEVEL = 3` (`BasiliskII.xcodeproj/project.pbxproj:1049,1159,1302`), and `configure.ac` now adds `-O3` for ARM targets. Debug configurations remain unoptimized. |
| B.2 | **Partial** | `configure.ac` now adds portable ARM baseline flags: AArch64 `-march=armv8-a`; ARMv7 `-march=armv7-a -mfpu=neon-vfpv4`. No CPU-specific `-mtune` is applied, and macOS relies on Xcode's arm64 target. |
| B.3 | **Missing/intentional** | No LTO setting was found in the macOS Xcode target. Keep it disabled until the JIT gate/strict-marker behavior is tested; do not re-add blindly. |
| B.4 | **Not applicable** | Debian hardening is outside this macOS Xcode target; no macOS equivalent change was found. |
| B.5 | **Partial** | `-fno-exceptions` is only added for the old i386 autoconf branch (`configure.ac:1564-1570`); no global `-fno-rtti`/no-exceptions policy was found in the Xcode settings. |
| PGO | **Missing** | No PGO workflow or profile configuration was found. |

### CPU core

| ID | Status | Finding |
|---|---|---|
| C.1 | **Present** | `BasiliskII/src/Unix/sysdeps.h` now declares ARM/AArch64 unaligned scalar access and uses `__builtin_bswap32/16`; Clang verification emitted `ldr` + `rev` for ARM64 and ARMv7. |
| C.2 | **Partial** | `uae_cpu_2026/m68k.h:742-1705` contains the AArch64 optimized flag block, and `uae_cpu_2026.xcodeproj/project.pbxproj:605-607,647-649` defines AArch64 assembly flags. `configure.ac` now has ARM tuning but still lacks the generic ARM/AArch64 assembly define branch. |
| C.3 | **Present** | `uae_cpu_2026/spcflags.h:79-87` uses GCC atomic fetch-or/fetch-and with a fallback for other platforms. |
| C.4 | **Partial** | Computed-goto generation and runtime gates exist, but the generated wrapper still calls the ordinary handler at each label, and the small prototype only fast-paths NOP/MOVEQ. It is opt-in and not a complete threaded interpreter. |
| C.5 | **Missing** | `newcpu.cpp:2616` calls `cpu_check_ticks()` separately from the SPCFLAGS test. |
| C.6 | **Present** | `uae_cpu_2026/spcflags.h:105-119` and the STOP path in `newcpu.cpp:2090` call `SleepAndWait()`. |

### Video/display

| ID | Status | Finding |
|---|---|---|
| V.1 | **Missing/not applicable to current tree** | No `vnc_server.cpp` or VNC background conversion implementation is present in this repository. Do not duplicate this work unless VNC is reintroduced. |
| V.2 | **Not found in SDL3 path** | The pasted SDL2 double-buffer `memcmp` guard is not the macOS SDL3 implementation. SDL3 already accumulates dirty rectangles in `video_sdl3.cpp:157-158,1095`; inspect that path before adding another full-frame copy. |
| V.3 | **Partial/present** | Both SDL2 and SDL3 return before rendering when the dirty rect is empty (`video_sdl2.cpp:1021-1024`, `video_sdl3.cpp:976`). When dirty, both still render the full texture and call `SDL_RenderPresent`; further source/destination scaling optimization remains. |
| V.4 | **Present** | NEON dirty comparison is wired into SDL2 dirty paths and the SDL3 source includes `video_neon.h`. |
| V.5 | **Present, not enabled by default** | VOSF threshold/policy and diagnostics exist; the checked macOS config leaves `ENABLE_VOSF` undefined. |
| V.6 | **Present** | SDL3 has `video_expand_indexed_rect` and reports the palette expansion path in `video_sdl3.cpp:988-1000`. |

### Audio

| ID | Status | Finding |
|---|---|---|
| A.1 | **Present for SDL2; different SDL3 design** | `BasiliskII/src/SDL/audio_ring_buffer.h` provides a non-blocking SPSC byte queue; `audio_sdl.cpp` now reads it from the SDL2 callback and writes it from `AudioInterrupt`. SDL3 retains its native `SDL_AudioStream` worker model. |
| A.2 | **Partial** | SDL2 now buffers four device blocks, but the device block size remains preference-controlled rather than forcing the audit's 2048-frame default. |
| A.3 | **Present for SDL2** | Full-volume SDL2 callbacks read directly into the output stream; volume/mixing cases use a separate callback scratch buffer. |
| macOS native audio | **Separate path** | `audio_macosx.cpp` is distinct from SDL audio and should not be conflated with the SDL2 audit. |

### Disk/filesystem

| ID | Status | Finding |
|---|---|---|
| D.1 | **Present** | `BasiliskII/src/Unix/sys_unix.cpp:808,835` uses `pread/pwrite` for regular files, with a documented fallback for special/shared-offset files. |
| D.2 | **Present, verify** | `BasiliskII/src/Unix/sys_unix.cpp:114-240,938-968` now has a bounded 64 KiB, 16-entry read-through cache for regular disk-image files. `B2_DISK_CACHE=0` disables it; `B2_DISK_CACHE_METRICS=1` reports hit/miss/eviction counters at exit. Writes and handle close invalidate cached blocks. |
| D.3 | **Present** | ExtFS directory enumeration is cached in `extfs.cpp:403-429`; indexed lookups use the cache. |
| D.4 | **Present** | `stat_cached` is used across ExtFS metadata operations, with invalidation on mutations. |

### Networking

| ID | Status | Finding |
|---|---|---|
| N.1 | **Missing** | No poll/epoll plus pipe-wakeup replacement for the ethernet receive loop was found. |
| N.2 | **Missing** | No adaptive idle/active slirp timeout was found. |
| N.3 | **Missing** | No queued/batched replacement for per-packet interrupt acknowledgement was found. |

### Timing/threading

| ID | Status | Finding |
|---|---|---|
| T.1 | **Present** | Monotonic clock selection exists in `timer_unix.cpp` and precise timing in `timer.cpp`. |
| T.2 | **Present for POSIX, Mach unchanged** | `timer.cpp:161-205,537-569` now uses a condition variable and absolute timed waits instead of signal suspend/resume when `PRECISE_TIMING_POSIX` is selected. `configure.ac` checks `pthread_condattr_setclock` so the condition variable can use the same monotonic clock where supported. The macOS Mach path remains unchanged. |
| T.3 | **Partial/present for timing ownership** | The 60 Hz and precise timer ownership is coordinated in the JIT support path, but SDL3 still has a redraw thread plus VBL-triggered presentation (`video_sdl3.cpp:1496-1503,1892,1921,2943`). A fully unified display pipeline is not complete. |

## Work not to duplicate

Do not start another implementation of these without first checking the existing code:

1. ExtFS directory enumeration cache and stat cache — already implemented in `BasiliskII/src/extfs.cpp`.
2. SDL3 source migration and Xcode framework/source wiring — already implemented in the macOS project.
3. NEON framebuffer comparison — already implemented in `BasiliskII/src/SDL/video_neon.h` and used by video paths.
4. VOSF profitability policy and diagnostics — already implemented; only runtime validation/tuning remains.
5. Monotonic/dual timing ownership — already implemented in timer/JIT support; remaining work is timer cleanup and display-loop unification.
6. ARM/AArch64 builtin byte swapping and portable ARM baseline tuning — implemented in `BasiliskII/src/Unix/sysdeps.h` and `BasiliskII/src/Unix/configure.ac`; do not redo it without profiling or adding missing configure defines.
7. SDL2 non-blocking audio transport — implemented in `BasiliskII/src/SDL/audio_ring_buffer.h` and `audio_sdl.cpp`; do not reintroduce callback waits or share scratch buffers between producer and consumer.
8. AArch64 flag assembly, atomic SPCFLAGS, STOP sleeping, and regular-file `pread/pwrite` — already implemented.

## Recommended next tranche

Highest-value unfinished items, in order:

1. Make the threaded interpreter real: generate handlers that share a dispatch loop/state rather than labels that call `cpufunctbl` and return; keep the current differential validation and fallback gates.
2. Extend the ARM build work only if measurements justify it: add missing configure assembly defines or platform-specific tuning, then verify generated instructions on the target architecture.
3. Decide whether SDL3 should replace the SDL2 audio path for the target; if SDL2 remains supported, implement the ring buffer there separately.
4. Measure the new bounded disk read cache (`B2_DISK_CACHE_METRICS=1`) against boot/app-launch I/O before changing its size or policy; ExtFS directory/stat caching is already done.
5. Finish SDL3 present optimization: preserve dirty-rect texture updates and avoid full render/present work when there is no new frame.
6. Run timing/boot regression tests for the new POSIX condition-variable timer path on a POSIX target; macOS uses the unchanged Mach path.
7. Add network wakeup/batching only with packet-latency and idle-wakeup measurements.

## Validation still required

This is a source/build audit, not a performance claim. Before marking the remaining work complete, run:

- macOS Xcode Release build for the active `BasiliskII` scheme and `uae_cpu_arm64` target.
- Interpreter differential tests with generated-goto disabled, enabled, and validation enabled.
- VOSF active/fallback diagnostics on the intended display backend.
- ExtFS directory-cache metrics while opening large Finder directories.
- Disk cache hit/miss/eviction metrics during boot and application launch, with `B2_DISK_CACHE=0` as the control run.
- Audio underrun/latency measurements for both SDL2 and SDL3 paths.
- 60 Hz/precise-timer drift and CPU-load measurements.
- A strict JIT marker soak after every dispatch or timing change.

## Runtime measurement status

The available validation host is Apple Silicon macOS. Its Xcode build selects
the existing Mach precise-timing backend, so it cannot exercise the new
`PRECISE_TIMING_POSIX` condition-variable path. No Linux/ARM runtime, Docker
image, QEMU user emulator, ROM image, or disk-image fixture is available in
the workspace, so integrated disk-cache hit rates cannot be measured without
inventing workload results.

Completed on this host:

- Xcode build-for-testing completed successfully.
- Four repository regression tests passed.
- The built executable responded to `--help` without a crash.
- The modified Unix disk source passed standalone Clang syntax checking.

Required follow-up on a POSIX target with a real ROM and disk image:

```sh
B2_BENCHMARK_METRICS=1 B2_DISK_CACHE_METRICS=1 B2_DISK_CACHE=1 \
  BasiliskII --config benchmark.conf
B2_BENCHMARK_METRICS=1 B2_DISK_CACHE_METRICS=1 B2_DISK_CACHE=0 \
  BasiliskII --config benchmark.conf
```

Compare boot-to-ready time, timer drift, `disk.cache_hit_rate`, misses, and
evictions between the enabled and control runs. The POSIX timer result should
also be checked under repeated `PrimeTime`/`RmvTime` activity rather than by a
standalone condition-variable microbenchmark.
