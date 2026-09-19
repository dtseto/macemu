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
- ARM/AArch64 byte swapping is not optimized in `BasiliskII/src/Unix/sysdeps.h`; the generic shift/byte fallback remains.
- `configure.ac` does not add the requested ARM/AArch64 optimization defines or ARM `-march/-mtune` settings. The macOS Xcode target has AArch64 defines, but no equivalent architecture tuning or LTO configuration.
- The SDL2 audio implementation still blocks on a semaphore and uses the old mixing buffer. SDL3 uses an SDL audio stream, but it is not the documented lock-free 2048-frame ring-buffer implementation.
- No VNC server/async VNC conversion implementation exists in this repository path.
- Disk read-ahead/LRU caching, network polling/batching, and several timer/display architecture changes remain undone.

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
| B.1 | **Partial** | Xcode Release configurations use `GCC_OPTIMIZATION_LEVEL = 3` (`BasiliskII.xcodeproj/project.pbxproj:1049,1159,1302`), but the ARM-specific autoconf recommendation is not present and some Debug configurations remain unoptimized. |
| B.2 | **Missing** | No `-march`/`-mtune` settings were found in the macOS Xcode project or `BasiliskII/src/Unix/configure.ac`. |
| B.3 | **Missing/intentional** | No LTO setting was found in the macOS Xcode target. Keep it disabled until the JIT gate/strict-marker behavior is tested; do not re-add blindly. |
| B.4 | **Not applicable** | Debian hardening is outside this macOS Xcode target; no macOS equivalent change was found. |
| B.5 | **Partial** | `-fno-exceptions` is only added for the old i386 autoconf branch (`configure.ac:1564-1570`); no global `-fno-rtti`/no-exceptions policy was found in the Xcode settings. |
| PGO | **Missing** | No PGO workflow or profile configuration was found. |

### CPU core

| ID | Status | Finding |
|---|---|---|
| C.1 | **Missing** | `BasiliskII/src/Unix/sysdeps.h:439-455` still uses the generic shift/byte fallback for little-endian CPUs that are not declared unaligned-capable. No ARM `__builtin_bswap32/16` path was found there. |
| C.2 | **Present for macOS AArch64, missing in generic configure** | `uae_cpu_2026/m68k.h:742-1705` contains the AArch64 optimized flag block, and `uae_cpu_2026.xcodeproj/project.pbxproj:605-607,647-649` defines AArch64 assembly flags. `configure.ac` still has no ARM/AArch64 branch equivalent. |
| C.3 | **Present** | `uae_cpu_2026/spcflags.h:79-87` uses GCC atomic fetch-or/fetch-and with a fallback for other platforms. |
| C.4 | **Partial** | Computed-goto generation and runtime gates exist, but the generated wrapper still calls the ordinary handler at each label, and the small prototype only fast-paths NOP/MOVEQ. It is opt-in and not a complete threaded interpreter. |
| C.5 | **Missing** | `newcpu.cpp:2616` calls `cpu_check_ticks()` separately from the SPCFLAGS test. |
| C.6 | **Present** | `uae_cpu_2026/spcflags.h:105-119` and the STOP path in `newcpu.cpp:2090` call `SleepAndWait()`. |

### Video/display

| ID | Status | Finding |
|---|---|---|
| V.1 | **Missing/not applicable to current tree** | No `vnc_server.cpp` or VNC background conversion implementation is present in this repository. Do not duplicate this work unless VNC is reintroduced. |
| V.2 | **Not found in SDL3 path** | The pasted SDL2 double-buffer `memcmp` guard is not the macOS SDL3 implementation. SDL3 already accumulates dirty rectangles in `video_sdl3.cpp:157-158,1095`; inspect that path before adding another full-frame copy. |
| V.3 | **Partial** | SDL3 returns early when the dirty rect is empty (`video_sdl3.cpp:974-976`), but when dirty it still renders the full texture and calls `SDL_RenderPresent` (`1082-1084`). |
| V.4 | **Present** | NEON dirty comparison is wired into SDL2 dirty paths and the SDL3 source includes `video_neon.h`. |
| V.5 | **Present, not enabled by default** | VOSF threshold/policy and diagnostics exist; the checked macOS config leaves `ENABLE_VOSF` undefined. |
| V.6 | **Present** | SDL3 has `video_expand_indexed_rect` and reports the palette expansion path in `video_sdl3.cpp:988-1000`. |

### Audio

| ID | Status | Finding |
|---|---|---|
| A.1 | **Missing for SDL2; different SDL3 design** | `BasiliskII/src/SDL/audio_sdl.cpp:195-196,281,308` still creates/waits on a semaphore and uses `audio_mix_buf`. SDL3 uses `SDL_AudioStream` and a worker thread, but is not the documented lock-free SPSC ring buffer. |
| A.2 | **Missing** | No 2048-frame ring-buffer default was found in the SDL2/SDL3 implementations. |
| A.3 | **Missing for SDL2** | The SDL2 path still performs the old intermediate mixing-buffer operations; no direct ring read at full volume was found. |
| macOS native audio | **Separate path** | `audio_macosx.cpp` is distinct from SDL audio and should not be conflated with the SDL2 audit. |

### Disk/filesystem

| ID | Status | Finding |
|---|---|---|
| D.1 | **Present** | `BasiliskII/src/Unix/sys_unix.cpp:808,835` uses `pread/pwrite` for regular files, with a documented fallback for special/shared-offset files. |
| D.2 | **Missing** | No application-level disk read-ahead/LRU cache was found in the disk path. |
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
| T.2 | **Partial** | `timer_unix.cpp:301-346` has a condition-variable timed-wait implementation, but the POSIX implementation in `timer.cpp:161-253` still contains signal-based suspend/resume. macOS Mach timing follows a separate path. |
| T.3 | **Partial/present for timing ownership** | The 60 Hz and precise timer ownership is coordinated in the JIT support path, but SDL3 still has a redraw thread plus VBL-triggered presentation (`video_sdl3.cpp:1496-1503,1892,1921,2943`). A fully unified display pipeline is not complete. |

## Work not to duplicate

Do not start another implementation of these without first checking the existing code:

1. ExtFS directory enumeration cache and stat cache — already implemented in `BasiliskII/src/extfs.cpp`.
2. SDL3 source migration and Xcode framework/source wiring — already implemented in the macOS project.
3. NEON framebuffer comparison — already implemented in `BasiliskII/src/SDL/video_neon.h` and used by video paths.
4. VOSF profitability policy and diagnostics — already implemented; only runtime validation/tuning remains.
5. Monotonic/dual timing ownership — already implemented in timer/JIT support; remaining work is timer cleanup and display-loop unification.
6. AArch64 flag assembly, atomic SPCFLAGS, STOP sleeping, and regular-file `pread/pwrite` — already implemented.

## Recommended next tranche

Highest-value unfinished items, in order:

1. Make the threaded interpreter real: generate handlers that share a dispatch loop/state rather than labels that call `cpufunctbl` and return; keep the current differential validation and fallback gates.
2. Add the ARM/AArch64 byte-swap path and generic configure/build tuning, then verify the generated instructions on the target architecture.
3. Decide whether SDL3 should replace the SDL2 audio path for the target; if SDL2 remains supported, implement the ring buffer there separately.
4. Add a disk read cache only after measuring the existing ExtFS/stat-cache hit rates; ExtFS directory caching is already done.
5. Finish SDL3 present optimization: preserve dirty-rect texture updates and avoid full render/present work when there is no new frame.
6. Replace the remaining POSIX timer signal suspend/resume path with a condition-variable design, then run timing/boot regression tests.
7. Add network wakeup/batching only with packet-latency and idle-wakeup measurements.

## Validation still required

This is a source/build audit, not a performance claim. Before marking the remaining work complete, run:

- macOS Xcode Release build for the active `BasiliskII` scheme and `uae_cpu_arm64` target.
- Interpreter differential tests with generated-goto disabled, enabled, and validation enabled.
- VOSF active/fallback diagnostics on the intended display backend.
- ExtFS directory-cache metrics while opening large Finder directories.
- Audio underrun/latency measurements for both SDL2 and SDL3 paths.
- 60 Hz/precise-timer drift and CPU-load measurements.
- A strict JIT marker soak after every dispatch or timing change.
