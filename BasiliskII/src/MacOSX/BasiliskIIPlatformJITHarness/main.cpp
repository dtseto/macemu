#include <sys/mman.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#if !defined(__APPLE__) || !defined(__aarch64__)
#error "BasiliskIIPlatformJITHarness requires an Apple Silicon macOS target"
#endif

namespace {

constexpr size_t kPageCount = 1;
constexpr size_t kCodeSize = kPageCount * 4096;
constexpr uint32_t kRet = 0xD65F03C0;
constexpr size_t kStressIterations = 2000;

using JITFunction = uint32_t (*)();

void fail(const char *message)
{
    std::fprintf(stderr, "PLATFORM_JIT_FAIL: %s\n", message);
    std::fflush(stderr);
    std::exit(1);
}

void check(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

void synchronizeInstructions(void *start, size_t size)
{
    __builtin___clear_cache(static_cast<char *>(start),
                            static_cast<char *>(start) + size);
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

void enterWriteMode()
{
    pthread_jit_write_protect_np(0);
}

void enterExecuteMode()
{
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
    pthread_jit_write_protect_np(1);
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy" ::: "memory");
}

void *allocateCode()
{
    void *code = mmap(nullptr,
                      kCodeSize,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANON | MAP_JIT,
                      -1,
                      0);
    check(code != MAP_FAILED, "mmap(MAP_JIT) failed");
    return code;
}

void emitConstantFunction(void *code, uint32_t value)
{
    enterWriteMode();

    auto *instructions = static_cast<uint32_t *>(code);
    instructions[0] = 0x52800000u | ((value & 0xffffu) << 5);
    instructions[1] = kRet;

    synchronizeInstructions(code, 2 * sizeof(uint32_t));
    enterExecuteMode();
}

void runSingleFunctionTest()
{
    void *code = allocateCode();
    emitConstantFunction(code, 42);

    auto function = reinterpret_cast<JITFunction>(code);
    check(function() == 42, "generated function returned the wrong value");

    check(munmap(code, kCodeSize) == 0, "munmap failed");
}

void runRewriteTest()
{
    void *code = allocateCode();
    auto function = reinterpret_cast<JITFunction>(code);

    for (uint32_t value = 0; value < 256; ++value) {
        emitConstantFunction(code, value);
        check(function() == value, "rewritten function returned the wrong value");
    }

    check(munmap(code, kCodeSize) == 0, "munmap failed after rewrite test");
}

void runThreadTest()
{
    constexpr size_t kThreadCount = 4;
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (size_t index = 0; index < kThreadCount; ++index) {
        threads.emplace_back([&failed, index]() {
            void *code = allocateCode();
            auto function = reinterpret_cast<JITFunction>(code);

            for (size_t iteration = 0;
                 iteration < kStressIterations && !failed.load();
                 ++iteration) {
                const uint32_t expected =
                    static_cast<uint32_t>((index * 17 + iteration) & 0xffff);
                emitConstantFunction(code, expected);
                if (function() != expected) {
                    failed.store(true);
                    break;
                }
            }

            if (munmap(code, kCodeSize) != 0)
                failed.store(true);
        });
    }

    for (auto &thread : threads)
        thread.join();

    check(!failed.load(), "threaded write/execute stress test failed");
}

void runAllocationStressTest()
{
    for (size_t iteration = 0; iteration < kStressIterations; ++iteration) {
        void *code = allocateCode();
        emitConstantFunction(code, static_cast<uint32_t>(iteration & 0xffff));

        auto function = reinterpret_cast<JITFunction>(code);
        check(function() == (iteration & 0xffff),
              "allocation stress function returned the wrong value");

        check(munmap(code, kCodeSize) == 0,
              "munmap failed during allocation stress test");
    }
}

} // namespace

int main()
{
    std::printf("PLATFORM_JIT_BEGIN\n");

    runSingleFunctionTest();
    std::printf("PLATFORM_JIT_SINGLE_PASS\n");

    runRewriteTest();
    std::printf("PLATFORM_JIT_REWRITE_PASS\n");

    runThreadTest();
    std::printf("PLATFORM_JIT_THREAD_PASS\n");

    runAllocationStressTest();
    std::printf("PLATFORM_JIT_STRESS_PASS iterations=%zu\n",
                kStressIterations);

    std::printf("PLATFORM_JIT_PASS\n");
    return 0;
}
