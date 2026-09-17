#include "sysdeps.h"
#include "main.h"
#include "rom_patches.h"
#include "macos_util_macosx.h"

#if defined(CPU_aarch64) || defined(CPU_AARCH64)

// macOS equivalent of the reference Unix one_tick() contract. The AppKit
// timer normally owns this work; synchronous/retirement JIT modes call this
// hook only when explicitly enabled.
void jit_one_tick(void)
{
    if (ROMVersion != ROM_VERSION_CLASSIC || HasMacStarted()) {
        SetInterruptFlag(INTFLAG_60HZ);
        TriggerInterrupt();
    }
}

// These are optional diagnostic observers emitted by the reference backend.
// The local debug implementation depends on private generator state; retaining
// callable observers keeps generated code linkable without enabling tracing.
extern "C" void jit_trace_add(uae_u32, uae_u32)
{
}

extern "C" void jit_trace_pc_hit(uae_u32, uae_u32)
{
}

#endif
