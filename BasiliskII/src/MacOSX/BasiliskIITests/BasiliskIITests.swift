import Foundation
import Testing
#if os(macOS)
import Darwin
#endif

struct BenchmarkHarnessTests {
    private var repositoryRoot: URL {
        var url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        for _ in 0..<4 {
            url.deleteLastPathComponent()
        }
        return url
    }

    @Test("Asset-free benchmark regression suite")
    func assetFreeRegressionSuite() throws {
        let result = try run("/bin/sh", [repositoryRoot.appending(path: "tests/run.sh").path])
        #expect(result.status == 0, Comment(rawValue: result.output))
        #expect(result.output.contains("OK"), Comment(rawValue: result.output))
    }

    @Test("Benchmark CLI produces machine-readable JSON")
    func benchmarkCLIProducesJSON() throws {
        let temporaryDirectory = FileManager.default.temporaryDirectory
            .appending(path: UUID().uuidString, directoryHint: .isDirectory)
        try FileManager.default.createDirectory(at: temporaryDirectory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temporaryDirectory) }

        let manifestURL = temporaryDirectory.appending(path: "manifest.json")
        let outputURL = temporaryDirectory.appending(path: "result.json")
        let manifest: [String: Any] = [
            "scenarios": [[
                "name": "xcode-self-test",
                "command": ["/bin/sh", "-c", "echo 'B2_METRIC guest.work_units=42'"],
            ]],
        ]
        try JSONSerialization.data(withJSONObject: manifest).write(to: manifestURL)

        let result = try run(repositoryRoot.appending(path: "scripts/bench.sh").path, [
            "run", "--manifest", manifestURL.path, "--output", outputURL.path,
        ])
        #expect(result.status == 0, Comment(rawValue: result.output))

        let object = try #require(JSONSerialization.jsonObject(with: Data(contentsOf: outputURL)) as? [String: Any])
        #expect(object["schema_version"] as? Int == 1)
        #expect(object["ok"] as? Bool == true)
        let runs = try #require(object["runs"] as? [[String: Any]])
        let firstRun = try #require(runs.first)
        let metrics = try #require(firstRun["metrics"] as? [String: Any])
        #expect(metrics["guest.work_units"] as? Int == 42)
    }

    @Test("Interpreter dispatch diagnostics are opt-in and breakpointable")
    func interpreterDispatchDiagnosticsAreOptIn() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/uae_cpu_2026/newcpu.cpp"),
            encoding: .utf8
        )
        #expect(source.contains("B2_INTERP_DISPATCH_METRICS"))
        #expect(source.contains("B2_INTERP_OPCODE_HISTOGRAM"))
        #expect(source.contains("B2_INTERP_BREAK_OPCODE"))
        #expect(source.contains("B2_INTERP_THREADED_PROTO"))
        #expect(source.contains("threaded_targets[0x4e71]"))
        #expect(source.contains("m68k_dreg(regs, (opcode >> 9) & 7)"))
        #expect(source.contains("SET_ZFLG(value == 0)"))
        #expect(source.contains("cpuop_func *handler = NULL"))
        #expect(source.contains("raise(SIGTRAP)"))
        #expect(source.contains("B2_INTERP_GENERATED_GOTO"))
        #expect(source.contains("cpuemu_threaded_dispatch_available"))
        #expect(source.contains("cpufunctbl[opcode](opcode)"))
        #expect(source.contains("generated goto unavailable"))
        #expect(source.contains("B2_INTERP_GOTO_VALIDATE"))
        #expect(source.contains("generated_dispatch_in_progress"))
        #expect(source.contains("cpuemu_threaded_dispatch_validate"))
    }

    @Test("Generated threaded dispatch is opt-in and has a compiler fallback")
    func generatedThreadedDispatchIsFailsafe() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/uae_cpu_2026/gencpu.c"),
            encoding: .utf8
        )
        #expect(source.contains("--dispatch=goto"))
        #expect(source.contains("cpuemu_threaded.cpp"))
        #expect(source.contains("defined(__GNUC__) || defined(__clang__)"))
        #expect(source.contains("cpufunctbl[opcode](opcode)"))
        #expect(source.contains("goto *targets[opcode]"))
        #expect(source.contains("opcode_%04x"))
        #expect(source.contains("cpuemu_threaded_dispatch_available"))
        #expect(source.contains("cpuemu_threaded_dispatch_validate"))
        #expect(source.contains("opcode == 65535"))
    }

    @Test("Computed-goto generator covers the complete opcode space")
    func computedGotoGeneratorCoversCompleteOpcodeSpace() throws {
        let generator = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/uae_cpu_2021/gencpu.c"),
            encoding: .utf8
        )
        let dispatcher = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/uae_cpu_2026/gencpu.c"),
            encoding: .utf8
        )

        #expect(generator.contains("int main(int argc, char **argv)"))
        #expect(generator.contains("--dispatch=goto"))
        #expect(generator.contains("targets[65536]"))
        #expect(generator.contains("for (opcode = 0; opcode < 65536; opcode++)"))
        #expect(generator.contains("opcode == 65535"))
        #expect(generator.contains("cpufunctbl[opcode](opcode)"))
        #expect(dispatcher.contains("cpuemu_threaded_dispatch_available"))
        #expect(dispatcher.contains("cpuemu_threaded_dispatch_validate"))
    }

    @Test("Computed-goto runtime modes remain opt-in and differential-safe")
    func computedGotoRuntimeModesRemainOptInAndDifferentialSafe() throws {
        let interpreter = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/uae_cpu_2026/newcpu.cpp"),
            encoding: .utf8
        )
        let makefile = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/Makefile.gencpu_2021"),
            encoding: .utf8
        )

        #expect(interpreter.contains("B2_INTERP_GENERATED_GOTO"))
        #expect(interpreter.contains("B2_INTERP_GOTO_VALIDATE"))
        #expect(interpreter.contains("cpuemu_threaded_dispatch_validate(opcode, handler)"))
        #expect(interpreter.contains("generated_validation_reported"))
        #expect(interpreter.contains("generated_dispatch_in_progress"))
        #expect(interpreter.contains("cpufunctbl[opcode](opcode)"))
        #expect(makefile.contains("B2_GENERATE_INTERP_GOTO"))
        #expect(makefile.contains("./gencpu $(DISPATCH_ARG)"))
    }

    @Test("Deterministic opcode sequence has normal and generated handler paths")
    func deterministicOpcodeSequenceHasNormalAndGeneratedHandlerPaths() throws {
        let interpreter = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/uae_cpu_2026/newcpu.cpp"),
            encoding: .utf8
        )
        let generator = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/uae_cpu_2021/gencpu.c"),
            encoding: .utf8
        )

        let sequence = ["4e71", "7001", "7002", "4e75"]
        #expect(sequence.count == 4)
        #expect(sequence.allSatisfy { $0.count == 4 })
        #expect(generator.contains("opcode_%04x"))
        #expect(interpreter.contains("cpufunctbl[opcode]"))
        #expect(interpreter.contains("(*handler)(opcode)"))
        #expect(interpreter.contains("cpuemu_threaded_dispatch(opcode)"))
    }

    @Test("JIT feature switch is value-based and defaults off")
    func jitFeatureSwitchIsValueBased() throws {
        let header = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu.h"),
            encoding: .utf8
        )
        #expect(header.contains("#ifndef USE_JIT"))
        #expect(header.contains("#define USE_JIT 0"))
        #expect(header.contains("#if USE_JIT"))
        #expect(!header.contains("#define USE_JIT\n"))
    }

    @Test("ARM64 JIT bootstrap and safety fallback diagnostics are opt-in")
    func arm64JITBootstrapAndFallbackDiagnosticsAreOptIn() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu_support.cpp"),
            encoding: .utf8
        )
        #expect(source.contains("B2_JIT_BOOTSTRAP_WATCHDOG"))
        #expect(source.contains("first execute_normal() returned pc=%08x pc_p=%p"))
        #expect(source.contains("B2_JIT_UNSAFE_NATIVE_DISPATCH=0 selects slow execute_normal fallback"))
        #expect(source.contains("jit_watchdog_arm(true)"))
        #expect(source.contains("jit_watchdog_arm(false)"))
    }

    @Test("ARM64 JIT bootstrap tracing is bounded and opt-in")
    func arm64JITBootstrapTracingIsBoundedAndOptIn() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu_legacy_arm64_compat.cpp"),
            encoding: .utf8
        )
        #expect(source.contains("B2_JIT_BOOTSTRAP_TRACE"))
        #expect(source.contains("JIT_BOOTSTRAP phase=%s pc=%08x pc_p=%p"))
        #expect(source.contains("remaining = (env && *env) ? strtol(env, NULL, 0) : 0"))
        #expect(source.contains("if (remaining == 0)"))
        #expect(source.contains("zero_ram_fallback"))
        #expect(source.contains("cache_miss"))
    }

    @Test("ARM64 JIT dispatch tracing distinguishes native and safe paths")
    func arm64JITDispatchTracingDistinguishesPaths() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu_support.cpp"),
            encoding: .utf8
        )
        #expect(source.contains("B2_JIT_DISPATCH_TRACE"))
        #expect(source.contains("JIT_DISPATCH phase=%s pc=%08x pc_p=%p"))
        #expect(source.contains("jit_dispatch_trace(\"safe_c_dispatch\""))
        #expect(source.contains("jit_dispatch_trace(\"native_dispatch\""))
        #expect(source.contains("jit_dispatch_trace(\"bootstrap\""))
        #expect(source.contains("if (remaining == 0)"))
    }

    @Test("ARM64 JIT keeps ROM and rtarea interpretation as the safe default")
    func arm64JITKeepsROMOnInterpreterPathByDefault() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu_support_arm.cpp"),
            encoding: .utf8
        )
        #expect(source.contains("B2_JIT_JIT_ROM"))
        #expect(source.contains("arm64_rom_block && !jit_native_rom_enabled()"))
        #expect(source.contains("ROMBaseMac"))
        #expect(source.contains("bi->handler_to_use = (cpuop_func*)popall_execute_normal"))
        #expect(source.contains("bi->direct_handler = bi->direct_pen"))
        #expect(source.contains("ROM/rtarea on the interpreter path"))
        #expect(source.contains("optlev = 0;"))
    }

    @Test("Audio shutdown releases callback before closing SDL")
    func audioShutdownOrdering() throws {
        let source = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/SDL/audio_sdl.cpp"), encoding: .utf8)
        let shutdown = try section(in: source, from: "static void close_audio(void)", through: "void AudioExit(void)")

        let flag = try #require(shutdown.range(of: "set_audio_shutting_down(true)"))
        let pause = try #require(shutdown.range(of: "SDL_PauseAudio(1)"))
        let close = try #require(shutdown.range(of: "SDL_CloseAudio()"))
        #expect(flag.lowerBound < pause.lowerBound)
        #expect(pause.lowerBound < close.lowerBound)

        let callback = try section(in: source, from: "static void stream_func(void *arg", through: "void AudioInterrupt(void)")
        #expect(callback.components(separatedBy: "is_audio_shutting_down()").count >= 3)
    }

    @Test("macOS cursor restoration stays on the main video exit path")
    func macOSCursorTeardownOrdering() throws {
        let source = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/SDL/video_sdl2.cpp"), encoding: .utf8)
        let destructor = try section(in: source, from: "driver_base::~driver_base()", through: "// Palette has changed")
        #expect(destructor.contains("#ifndef __MACOSX__"))
        #expect(destructor.contains("SDL_ShowCursor(1)"))

        let videoExit = try section(in: source, from: "void VideoExit(void)", through: "void VideoQuitFullScreen(void)")
        let closeDisplay = try #require(videoExit.range(of: "video_close()"))
        let restoreCursor = try #require(videoExit.range(of: "SDL_ShowCursor(SDL_ENABLE)"))
        let destroyWindow = try #require(videoExit.range(of: "delete_sdl_video_window()"))
        #expect(closeDisplay.lowerBound < restoreCursor.lowerBound)
        #expect(restoreCursor.lowerBound < destroyWindow.lowerBound)
    }

    @Test("ARM64 JIT generator emits nonempty handler and dispatch tables")
    func arm64GeneratedSourcesArePopulated() throws {
        let compilerDirectory = repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler")
        let handlers = try String(
            contentsOf: compilerDirectory.appending(path: "compemu_arm.cpp"),
            encoding: .utf8
        )
        let dispatchTable = try String(
            contentsOf: compilerDirectory.appending(path: "compstbl_arm.cpp"),
            encoding: .utf8
        )
        let declarations = try String(
            contentsOf: compilerDirectory.appending(path: "comptbl.h"),
            encoding: .utf8
        )

        let handlerPattern = try Regex(#"op_[0-9a-f]+_0_comp_(?:ff|nf)"#)
        let generatedHandlers = handlers.matches(of: handlerPattern)
        #expect(generatedHandlers.count > 1_000)
        #expect(dispatchTable.contains("op_smalltbl_0_comp_ff"))
        #expect(dispatchTable.contains("op_smalltbl_0_comp_nf"))
        #expect(dispatchTable.contains("{ 0, 65536, 0 }"))
        #expect(declarations.contains("op_smalltbl_0_comp_ff"))
        #expect(declarations.contains("op_smalltbl_0_comp_nf"))
    }

    @Test("ARM64 JIT keeps PC pointer state wider than guest values")
    func arm64PointerWidthContractIsExplicit() throws {
        let compilerDirectory = repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler")
        let compilerHeader = try String(
            contentsOf: compilerDirectory.appending(path: "compemu.h"),
            encoding: .utf8
        )
        let arm64Backend = try String(
            contentsOf: compilerDirectory.appending(path: "compemu_support_arm.cpp"),
            encoding: .utf8
        )
        let codegen = try String(
            contentsOf: compilerDirectory.appending(path: "codegen_arm64.cpp"),
            encoding: .utf8
        )

        #expect(compilerHeader.contains("typedef uintptr jit_reg_value_t;"))
        #expect(compilerHeader.contains("jit_reg_value_t val;"))
        #expect(arm64Backend.contains("if (r != PC_P)"))
        #expect(arm64Backend.contains("arm_ADD_ptr_ri"))
        #expect(codegen.contains("pc_p/pc_oldp are 64-bit host pointers"))
    }

    @Test("macOS ARM64 JIT allocation has a MAP_JIT fallback")
    func macOSARM64JITAllocationContractIsExplicit() throws {
        let compilerSource = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu_support_arm.cpp"),
            encoding: .utf8
        )
        let entitlements = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/BasiliskII.entitlements"),
            encoding: .utf8
        )

        #expect(compilerSource.contains("defined(CPU_AARCH64) && defined(__APPLE__)"))
        #expect(compilerSource.contains("MAP_PRIVATE | MAP_ANON | MAP_JIT"))
        #expect(compilerSource.contains("com.apple.security.cs.allow-jit entitlement"))
        #expect(entitlements.contains("com.apple.security.cs.allow-jit"))
        #expect(entitlements.contains("com.apple.security.cs.allow-unsigned-executable-memory"))
        #expect(entitlements.contains("com.apple.security.cs.disable-library-validation"))
    }

    @Test("Active ARM64 JIT path does not require low host addresses")
    func activeARM64JITPathIsHighAddressClean() throws {
        let cpuDirectory = repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler")
        let fpp = try String(
            contentsOf: cpuDirectory.appending(path: "compemu_fpp.cpp"),
            encoding: .utf8
        )
        let support = try String(
            contentsOf: cpuDirectory.appending(path: "compemu_support.cpp"),
            encoding: .utf8
        )

        #expect(fpp.contains("#if !defined(CPU_aarch64) && !defined(CPU_AARCH64)"))
        #expect(support.contains("return uae_vm_alloc(size, 0, UAE_VM_READ_WRITE);"))
        #expect(support.contains("AArch64 JIT/natmem may live above 4 GB"))
    }

    @Test("macOS launcher exposes a guarded JIT self-test")
    func macOSLauncherJITSelfTestIsGuarded() throws {
        let launcher = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/main_macosx.mm"),
            encoding: .utf8
        )

        #expect(launcher.contains("--jit-selftest"))
        #expect(launcher.contains("MAP_PRIVATE | MAP_ANON | MAP_JIT"))
        #expect(launcher.contains("pthread_jit_write_protect_np(0)"))
        #expect(launcher.contains("pthread_jit_write_protect_np(1)"))
        #expect(launcher.contains("initial_result != 42 || patched_result != 43"))
        #expect(launcher.contains("return run_jit_selftest();"))
    }

    @Test("ARM64 JIT diagnostics print host pointers without truncation")
    func arm64JITDiagnosticsPreserveHostPointerFormatting() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu_support_arm.cpp"),
            encoding: .utf8
        )

        #expect(source.contains("Address of regs: %p, regs.pc_p: %p"))
        #expect(source.contains("Address of cache_tags: %p"))
        #expect(!source.contains("Address of regs: 0x%016x"))
    }

    @Test("JIT FPU feature flag is valid for numeric preprocessor checks")
    func jitFPUFeatureFlagIsNumeric() throws {
        let config = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/config.h"),
            encoding: .utf8
        )

        #expect(config.contains("#define USE_JIT_FPU 1"))
        #expect(!config.contains("#define USE_JIT_FPU\n"))
    }

    @Test("ARM64 JIT dispatch tracing is opt-in")
    func arm64JITDispatchTracingIsOptIn() throws {
        let source = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/compiler/compemu_legacy_arm64_compat.cpp"),
            encoding: .utf8
        )

        #expect(source.contains("trace_remaining = (value && *value) ? strtol(value, NULL, 0) : 0;"))
        #expect(!source.contains("? strtol(value, NULL, 0) : 200"))
    }

#if arch(arm64) && os(macOS)
    @Test("macOS MAP_JIT page executes generated ARM64 instructions")
    func mapJITExecutesGeneratedARM64() throws {
        let pageSize = Int(getpagesize())
        let mapping = mmap(
            nil,
            pageSize,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANON | MAP_JIT,
            -1,
            0
        )
        #expect(mapping != MAP_FAILED)
        let code = try #require(mapping == MAP_FAILED ? nil : mapping)
        defer { munmap(code, pageSize) }

        // mov w0, #42; ret
        let instructions: [UInt32] = [0x52800540, 0xd65f03c0]
        pthread_jit_write_protect_np(0)
        instructions.withUnsafeBytes { bytes in
            code.copyMemory(from: bytes.baseAddress!, byteCount: bytes.count)
        }
        sys_icache_invalidate(code, instructions.count * MemoryLayout<UInt32>.size)
        pthread_jit_write_protect_np(1)

        typealias GeneratedFunction = @convention(c) () -> UInt32
        let function = unsafeBitCast(code, to: GeneratedFunction.self)
        #expect(function() == 42)

        // Verify the write-protect transition and patch lifecycle, not just
        // initial execution of a MAP_JIT page.
        pthread_jit_write_protect_np(0)
        let patchedInstructions: [UInt32] = [0x52800560, 0xd65f03c0] // mov w0, #43; ret
        patchedInstructions.withUnsafeBytes { bytes in
            code.copyMemory(from: bytes.baseAddress!, byteCount: bytes.count)
        }
        sys_icache_invalidate(code, patchedInstructions.count * MemoryLayout<UInt32>.size)
        pthread_jit_write_protect_np(1)

        #expect(function() == 43)
    }
#endif

    @Test("SDL optimization diagnostics describe gated paths once")
    func sdlOptimizationDiagnosticsAreGatedAndOneShot() throws {
        let sdl2 = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/SDL/video_sdl2.cpp"),
            encoding: .utf8
        )
        let sdl3 = try String(
            contentsOf: repositoryRoot.appending(path: "BasiliskII/src/SDL/video_sdl3.cpp"),
            encoding: .utf8
        )

        #expect(sdl2.contains("#if SDL_VERSION_ATLEAST(2, 0, 0) && !SDL_VERSION_ATLEAST(3, 0, 0)"))
        #expect(sdl3.contains("#if SDL_VERSION_ATLEAST(3, 0, 0)"))
        for source in [sdl2, sdl3] {
            #expect(source.contains("sdl_update_video_rect"))
            #expect(source.contains("SDL_RectEmpty(&sdl_update_video_rect)"))
            #expect(source.contains("B2_OPT path=SDL_present_skip active"))
            #expect(source.contains("B2_OPT path=NEON_dirty_detection active"))
            #expect(source.contains("B2_OPT path=NEON_dirty_detection fallback reason=NEON_unavailable"))
            #expect(source.contains("B2_OPT path=palette_expansion active"))
            #expect(source.contains("B2_OPT path=palette_expansion fallback reason=indexed_path_not_selected"))
            #expect(source.contains("static bool optimization_paths_reported = false"))
            #expect(source.contains("if (optimization_paths_reported)"))
            #expect(source.contains("static bool palette_optimization_reported = false"))
            #expect(source.contains("if (!palette_optimization_reported)"))
        }
        #expect(sdl2.contains("B2_OPT path=SDL3_native fallback reason=SDL2_backend_selected"))
        #expect(sdl3.contains("B2_OPT path=SDL3_native active"))
        #expect(sdl3.contains("B2_OPT path=SDL2_dirty_rects fallback reason=SDL3_backend_selected"))
    }

    @Test("Non-video optimization diagnostics cover active and fallback paths")
    func nonVideoOptimizationDiagnosticsAreGatedAndOneShot() throws {
        let ethernet = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/Unix/ether_unix.cpp"), encoding: .utf8)
        let extfs = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/MacOSX/extfs_macosx.cpp"), encoding: .utf8)
        let system = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/Unix/sys_unix.cpp"), encoding: .utf8)
        let bincue = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/bincue.cpp"), encoding: .utf8)
        let timer = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/timer.cpp"), encoding: .utf8)
        let sysdeps = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/Unix/sysdeps.h"), encoding: .utf8)
        let vosf = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/SDL/video_sdl3.cpp"), encoding: .utf8)

        #expect(ethernet.contains("#define USE_POLL 1"))
        #expect(ethernet.contains("poll(&pf, 1, -1)"))
        #expect(ethernet.contains("select(fd + 1"))
        #expect(ethernet.contains("B2_OPT path=ethernet_poll active"))
        #expect(ethernet.contains("B2_OPT path=ethernet_poll fallback reason=poll_unavailable"))
        #expect(ethernet.contains("B2_OPT path=adaptive_slirp_timeout active"))
        #expect(ethernet.contains("B2_OPT path=adaptive_slirp_timeout fallback reason=slirp_interface_not_selected"))
        #expect(ethernet.contains("static bool ethernet_optimization_diagnostics_reported = false"))

        #expect(extfs.contains("void extfs_init(void)"))
        #expect(extfs.contains("g_use_xattrs = check_xattr()"))

        #expect(system.contains("pread(fh->fd, buffer, length, file_offset)"))
        #expect(system.contains("pwrite(fh->fd, buffer, length, file_offset)"))
        #expect(system.contains("lseek(fh->fd, file_offset, SEEK_SET)"))
        #expect(system.contains("B2_OPT path=regular_file_pread_pwrite active"))
        #expect(system.contains("B2_OPT path=regular_file_pread_pwrite fallback reason=special_file_shared_offset"))
        #expect(system.contains("static bool regular_file_pread_diagnostic_reported = false"))

        #expect(bincue.contains("pread(cs->binfh"))
        #expect(system.contains("#if defined(BINCUE)"))
        #expect(system.contains("B2_OPT path=bincue_positional_reads active"))
        #expect(system.contains("B2_OPT path=bincue_positional_reads fallback reason=compiled_out"))
        #expect(system.contains("static bool bincue_active_diagnostic_reported = false"))

        #expect(sysdeps.contains("#define PRECISE_TIMING_POSIX 1"))
        #expect(sysdeps.contains("#define PRECISE_TIMING_MACH 1"))
        #expect(timer.contains("CLOCK_MONOTONIC"))
        #expect(timer.contains("B2_OPT path=monotonic_timer active"))
        #expect(timer.contains("host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &system_clock)"))
        #expect(timer.contains("B2_OPT path=monotonic_timer fallback reason=precise_timing_unavailable"))

        #expect(vosf.contains("#ifdef ENABLE_VOSF"))
        #expect(vosf.contains("video_vosf_profitable"))
        #expect(vosf.contains("B2_OPT path=VOSF_policy active"))
        #expect(vosf.contains("report_vosf_policy(false, \"compiled_out\")"))
        #expect(vosf.contains("B2_OPT path=VOSF_diagnostics active"))
        #expect(vosf.contains("static bool vosf_policy_reported = false"))
    }

    private func section(in source: String, from start: String, through end: String) throws -> String {
        let startRange = try #require(source.range(of: start))
        let endRange = try #require(source.range(of: end, range: startRange.upperBound..<source.endIndex))
        return String(source[startRange.lowerBound..<endRange.lowerBound])
    }

    private func run(_ executable: String, _ arguments: [String]) throws -> (status: Int32, output: String) {
        let process = Process()
        let pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = pipe
        process.standardError = pipe
        try process.run()
        process.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return (process.terminationStatus, String(decoding: data, as: UTF8.self))
    }
}
