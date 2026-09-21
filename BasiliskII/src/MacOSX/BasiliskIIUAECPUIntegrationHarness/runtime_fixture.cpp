#include <cstddef>
#include <cstdint>

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using int32 = std::int32_t;
using uint64 = std::uint64_t;

#include "../../src/include/main.h"
#include "../../src/include/emul_op.h"
#include "../../src/include/prefs.h"
#include "../../src/CrossPlatform/vm_alloc.h"
#include "../../uae_cpu_2026/m68k.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sys/mman.h>

int CPUType = 2;
int FPUType = 0;
uint16 ROMVersion = 0;
int32 emulated_ticks = 1000;
uint32 InterruptFlags = 0;
char *vde_sock = nullptr;
bool tick_inhibit = false;

struct B2_mutex {
    std::mutex value;
};

B2_mutex *B2_create_mutex(void) { return new B2_mutex; }
void B2_lock_mutex(B2_mutex *mutex) { mutex->value.lock(); }
void B2_unlock_mutex(B2_mutex *mutex) { mutex->value.unlock(); }
void B2_delete_mutex(B2_mutex *mutex) { delete mutex; }

void SetInterruptFlag(uint32 flag) { InterruptFlags |= flag; }
void ClearInterruptFlag(uint32 flag) { InterruptFlags &= ~flag; }

bool PrefsFindBool(const char *name)
{
    return std::strcmp(name, "jit") == 0;
}

int32 PrefsFindInt32(const char *name)
{
    if (std::strcmp(name, "jitcachesize") == 0)
        return 8192;
    return 0;
}

uint64 GetTicks_usec(void)
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void idle_resume(void) {}

void cpu_do_check_ticks(void)
{
    emulated_ticks = 1000;
}

void EmulOp(uint16, M68kRegisters *) {}

void *vm_acquire(size_t size, int)
{
#if defined(__APPLE__)
    void *result = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
#else
    void *result = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
    return result == MAP_FAILED ? VM_MAP_FAILED : result;
}

int vm_release(void *address, size_t size)
{
    return munmap(address, size);
}

void FlushCodeCache(void *, uint32) {}
void QuitEmulator(void) {}
void ErrorAlert(const char *) {}
void ErrorAlert(int) {}
void WarningAlert(const char *) {}
void WarningAlert(int) {}
bool ChoiceAlert(const char *, const char *, const char *) { return false; }
