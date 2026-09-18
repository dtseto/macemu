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
        #expect(source.contains("cpuemu_threaded_dispatch == NULL"))
        #expect(source.contains("generated goto unavailable"))
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

    @Test("Audio shutdown releases callback before closing SDL")
    func audioShutdownOrdering() throws {
        let source = try String(contentsOf: repositoryRoot.appending(path: "BasiliskII/src/SDL/audio_sdl.cpp"), encoding: .utf8)
        let shutdown = try section(in: source, from: "static void close_audio(void)", through: "void AudioExit(void)")

        let flag = try #require(shutdown.range(of: "set_audio_shutting_down(true)"))
        let wake = try #require(shutdown.range(of: "SDL_SemPost(audio_irq_done_sem)"))
        let pause = try #require(shutdown.range(of: "SDL_PauseAudio(1)"))
        let close = try #require(shutdown.range(of: "SDL_CloseAudio()"))
        #expect(flag.lowerBound < wake.lowerBound)
        #expect(wake.lowerBound < pause.lowerBound)
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
    }
#endif

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
