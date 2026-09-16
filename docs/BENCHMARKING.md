# Basilisk II benchmarking and regression tests

The backend-neutral harness runs named processes, measures host wall/CPU/RSS use, parses `B2_METRIC name=value` records, checks milestones and optional framebuffer SHA-256 values, and writes versioned JSON.

## Run

In Xcode, select the shared **BasiliskIITests** scheme and choose **Product > Test** (`⌘U`). The Swift Testing suite runs the asset-free regression checks and verifies that the benchmark CLI produces valid versioned JSON. These tests require no ROM or disk image.

The equivalent command-line invocation is:

```sh
xcodebuild test -project BasiliskII/src/MacOSX/BasiliskII.xcodeproj -scheme BasiliskIITests -destination 'platform=macOS'

# Fixture-driven/manual benchmark commands:
cp tools/benchmark/example.json benchmark.local.json
./scripts/bench.sh run --manifest benchmark.local.json --output results/baseline.json
./scripts/bench.sh run --manifest benchmark.local.json --output results/candidate.json
./scripts/bench.sh compare results/baseline.json results/candidate.json --output results/comparison.json
```

ROMs and disk images are copyrighted, user-supplied fixtures and must not be committed. Use a disposable copy or snapshot. SDL dummy video/audio gives a headless smoke lane; presentation measurements require a real display with identical resolution, depth, scale, vsync, and refresh settings.

The runner always records `process.wall_seconds`, `process.cpu_seconds`, `process.cpu_percent`, and `process.max_rss_kib`. Emulator or guest automation should emit startup, boot, CPU-workload, presentation and upload metrics:

```text
B2_METRIC startup.ready_seconds=0.421
B2_METRIC guest.boot_seconds=18.92
B2_METRIC guest.work_units=50000
B2_METRIC video.present_ns=81234
B2_METRIC video.upload_bytes=16384
```

Use manifest `milestones` for ROM/boot/desktop markers. Use `framebuffer_path` and `expected_framebuffer_sha256` for deterministic checks.

For a bounded smoke run, set `B2_BINARY` and `B2_PREFS`, then run `./tests/rom-smoke.sh`. `B2_TIMEOUT`, `B2_ROM_MARKER`, `B2_DESKTOP_MARKER`, and `B2_TEST_RESULTS` are optional. A timeout is an accepted end to this bounded lane, but missing milestones remain visible as false metrics for CI policy to inspect.

## Correctness ladder

1. Asset-free Xcode tests (`BasiliskIITests` scheme, `⌘U`).
2. Bounded headless ROM smoke test with fatal-signal and progress markers.
3. Fixed boot-to-desktop fixture plus framebuffer checksum.
4. Video matrix: 1/2/4/8/16/32-bit modes, palette/gamma changes, clean frames, small dirty rectangles, and full updates.
5. Future JIT equivalence: identical vectors under interpreter and JIT; compare D0-D7, A0-A7, PC, SR/CCR, memory, exception frames, and ordered MMIO. Required groups cover condition codes, exceptions, self-modifying code, 24/32-bit addressing, FPU, and MMIO.

JIT must remain disabled by default until applicable equivalence and ROM/desktop lanes pass. Keep vectors and comparison backend-neutral.

## Baselines

Performance varies by ROM/disk, host, display, and build flags, so the repository does not claim synthetic numbers. Retain JSON artifacts, run one warm-up plus at least five samples, and compare medians on pinned hardware. CI should enforce correctness and archive performance results rather than set timing thresholds on shared runners.

This design follows the bounded, machine-readable approach in `rcarmo/macemu-jit`. PocketShaver informed separation of guest software rendering from final GPU composition; no PowerPC guest hooks are used.

On macOS, the active Xcode target uses `video_sdl2.cpp`, requests SDL's Metal renderer when available, and keeps QuickDraw in the guest framebuffer. Streaming textures are updated only for the accumulated dirty rectangle. Indexed modes precompute a packed 256-color host lookup table when the palette changes and expand only dirty pixels into the 32-bit staging surface. Set `B2_BENCHMARK_METRICS=1` to emit cumulative presentation time, presentation count, dirty pixels, and uploaded bytes at exit. The older `video_macosx.mm` Cocoa backend is not compiled by the current target and is intentionally unchanged.

The same metric flag reports skipped presentation attempts, accumulated and most-recent dirty rectangle sizes, and the VOSF startup decision. `vosf_threshold` optionally overrides the existing full-screen page-fault budget in microseconds; zero retains the established half-frame default. Do not raise it by platform without measurements showing that page-fault tracking costs less than framebuffer scanning. `SDL_RenderCopy` intentionally composites the complete persistent texture after each clear: restricting that copy to the dirty rectangle would lose unchanged pixels because SDL does not guarantee backbuffer preservation after presentation.
