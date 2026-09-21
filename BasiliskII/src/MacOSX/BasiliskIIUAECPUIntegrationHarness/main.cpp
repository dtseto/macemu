#include "../../uae_cpu_2026/m68k.h"
#include "../../uae_cpu_2026/readcpu.h"
#include "../../uae_cpu_2026/newcpu.h"
#include "../compiler/compemu.h"

#include <array>
#include <cstdio>

extern void init_m68k(void);
extern void exit_m68k(void);

extern bool UseJIT;
extern int quit_program;
extern uintptr MEMBaseDiff;
extern uae_u8 *fast_ram_base;
extern uae_u32 fast_ram_size;
extern uae_u32 RAMSize;

static constexpr uaecptr guest_code_offset = 0x1000;
static std::array<uae_u8, 65536> guest_memory;

struct CpuSnapshot {
    uae_u32 d0;
    uae_u32 pc;
    uae_u16 sr;
};

static CpuSnapshot run_moveq(bool jit)
{
    guest_memory.fill(0);
    guest_memory[guest_code_offset + 0] = 0x70;
    guest_memory[guest_code_offset + 1] = 0x05;
    guest_memory[guest_code_offset + 2] = 0x71;
    guest_memory[guest_code_offset + 3] = 0x00;

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(guest_memory.size());
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    m68k_setpc(guest_code_offset);
    MakeFromSR();
    regs.spcflags = jit ? 0 : SPCFLAG_BRK;

    if (jit)
        m68k_compile_execute();
    else
        m68k_do_execute();

    MakeSR();
    return { regs.regs[0], m68k_getpc(), regs.sr };
}

static bool run_jit_moveq(const CpuSnapshot &interpreter)
{
    const CpuSnapshot snapshot = run_moveq(true);
    const bool correct = snapshot.d0 == interpreter.d0 &&
        snapshot.pc == interpreter.pc &&
        snapshot.sr == interpreter.sr;

    std::printf("UAE_CPU_JIT_MOVEQ d0=%08x pc=%08x sr=%04x\n",
        static_cast<unsigned>(snapshot.d0),
        static_cast<unsigned>(snapshot.pc),
        static_cast<unsigned>(snapshot.sr));
    return correct;
}

int main()
{
    std::printf("UAE_CPU_INTEGRATION_BEGIN\n");
    std::printf("regstruct_size=%zu\n", sizeof(regs));

    init_m68k();
    std::printf("UAE_CPU_RUNTIME_FIXTURE_LINKED\n");

    const CpuSnapshot interpreter = run_moveq(false);
    const bool interpreter_pass = interpreter.d0 == 5 &&
        interpreter.pc == guest_code_offset + 2 &&
        (interpreter.sr & 0x001f) == 0;
    std::printf("UAE_CPU_INTERPRETER_MOVEQ d0=%08x pc=%08x sr=%04x\n",
        static_cast<unsigned>(interpreter.d0),
        static_cast<unsigned>(interpreter.pc),
        static_cast<unsigned>(interpreter.sr));
    std::printf("UAE_CPU_INTERPRETER_%s\n", interpreter_pass ? "PASS" : "FAIL");

    if (!interpreter_pass) {
        exit_m68k();
        return 1;
    }

    m68k_setpc(guest_code_offset);
    MakeFromSR();
    regs.spcflags = SPCFLAG_BRK;
    quit_program = 1;

    compiler_init();
    const bool jit_pass = run_jit_moveq(interpreter);
    std::printf("UAE_CPU_JIT_%s\n", jit_pass ? "PASS" : "FAIL");
    compiler_exit();

    exit_m68k();

    if (!jit_pass)
        return 1;

    std::printf("UAE_CPU_INTEGRATION_PASS\n");
    return 0;
}
