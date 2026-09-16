import Foundation
import Testing

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
