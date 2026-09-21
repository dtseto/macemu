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

static bool run_interpreter_moveq()
{
    std::array<uae_u8, 4096> memory{};
    memory[0] = 0x70;
    memory[1] = 0x05;

    MEMBaseDiff = reinterpret_cast<uintptr>(memory.data());
    fast_ram_base = memory.data();
    fast_ram_size = static_cast<uae_u32>(memory.size());
    RAMSize = fast_ram_size;
    UseJIT = false;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    regs.pc = 0;
    regs.pc_p = memory.data();
    regs.pc_oldp = memory.data();
    regs.fault_pc = 0;
    MakeFromSR();
    regs.spcflags = SPCFLAG_BRK;

    m68k_do_execute();

    MakeSR();
    const uae_u32 pc = m68k_getpc();
    const bool correct = regs.regs[0] == 5 &&
        pc == 2 &&
        (regs.sr & 0x0004) == 0 &&
        (regs.sr & 0x0002) == 0;

    std::printf("UAE_CPU_INTERPRETER_MOVEQ d0=%08x pc=%08x sr=%04x\n",
        static_cast<unsigned>(regs.regs[0]),
        static_cast<unsigned>(pc),
        static_cast<unsigned>(regs.sr));
    return correct;
}

int main()
{
    std::printf("UAE_CPU_INTEGRATION_BEGIN\n");
    std::printf("regstruct_size=%zu\n", sizeof(regs));

    init_m68k();
    std::printf("UAE_CPU_RUNTIME_FIXTURE_LINKED\n");

    const bool interpreter_pass = run_interpreter_moveq();
    std::printf("UAE_CPU_INTERPRETER_%s\n", interpreter_pass ? "PASS" : "FAIL");

    exit_m68k();

    if (!interpreter_pass)
        return 1;

    std::printf("UAE_CPU_INTEGRATION_PASS\n");
    return 0;
}
