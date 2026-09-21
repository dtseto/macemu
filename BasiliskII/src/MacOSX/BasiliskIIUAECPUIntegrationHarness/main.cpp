#include "../../uae_cpu_2026/m68k.h"
#include "../../uae_cpu_2026/readcpu.h"
#include "../../uae_cpu_2026/newcpu.h"
#include "../../src/include/emul_op.h"
#include "../compiler/compemu.h"

#include <array>
#include <cstdio>
#include <cstddef>

extern void init_m68k(void);
extern void exit_m68k(void);

extern bool UseJIT;
extern int quit_program;
extern uintptr MEMBaseDiff;
extern uae_u8 *fast_ram_base;
extern uae_u32 fast_ram_size;
extern uae_u32 RAMSize;

static constexpr uaecptr guest_code_offset = 0x1000;
static constexpr uaecptr guest_data_offset = 0x2000;
static constexpr uaecptr trap_code_offset = 0x1800;
static constexpr uaecptr trap_handler_offset = 0x3000;
static constexpr uaecptr trap_stack_offset = 0x4000;
static constexpr uaecptr subroutine_code_offset = 0x2600;
static constexpr uaecptr subroutine_handler_offset = 0x2800;
static constexpr uaecptr subroutine_stack_offset = 0x5000;
static constexpr uaecptr loop_code_offset = 0x2a00;
static std::array<uae_u8, 65536> guest_memory;

struct CpuSnapshot {
    std::array<uae_u32, 8> d;
    std::array<uae_u32, 8> a;
    uae_u32 pc;
    uae_u16 sr;
    uae_u32 data_value;
    uae_u16 stack_sr;
    uae_u32 stack_pc;
    uae_u16 stack_format;
};

struct TestCase {
    const char *name;
    uaecptr offset;
    uae_u32 initial_d0;
    uae_u32 expected_d0;
    uae_u32 initial_a0;
    uae_u32 expected_a0;
    uae_u32 data_value;
    uae_u32 expected_data_value;
    size_t retired_length;
    void (*emit)(uae_u8 *);
    uae_u16 expected_sr_mask = 0;
    uae_u16 expected_sr_bits = 0;
};

static void write_word(uae_u8 *code, size_t offset, uae_u16 value)
{
    code[offset] = static_cast<uae_u8>(value >> 8);
    code[offset + 1] = static_cast<uae_u8>(value);
}

static void write_long(uae_u8 *code, size_t offset, uae_u32 value)
{
    write_word(code, offset, static_cast<uae_u16>(value >> 16));
    write_word(code, offset + 2, static_cast<uae_u16>(value));
}

static void emit_moveq(uae_u8 *code)
{
    write_word(code, 0, 0x7005);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_addi(uae_u8 *code)
{
    write_word(code, 0, 0x0680);
    write_long(code, 2, 1);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_subi(uae_u8 *code)
{
    write_word(code, 0, 0x0480);
    write_long(code, 2, 1);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_andi(uae_u8 *code)
{
    write_word(code, 0, 0x0280);
    write_long(code, 2, 0x0f0f0f0f);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_ori(uae_u8 *code)
{
    write_word(code, 0, 0x0080);
    write_long(code, 2, 0x0f0f0f0f);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_store_long(uae_u8 *code)
{
    write_word(code, 0, 0x2080);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_load_long(uae_u8 *code)
{
    write_word(code, 0, 0x2010);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_load_long_postincrement(uae_u8 *code)
{
    write_word(code, 0, 0x2018);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_load_long_predecrement(uae_u8 *code)
{
    write_word(code, 0, 0x2020);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_alu_sequence(uae_u8 *code)
{
    write_word(code, 0, 0x7001);
    write_word(code, 2, 0x0680);
    write_long(code, 4, 2);
    write_word(code, 8, 0x0480);
    write_long(code, 10, 1);
    write_word(code, 14, M68K_EXEC_RETURN);
}

static void emit_addi_minus_one(uae_u8 *code)
{
    write_word(code, 0, 0x0680);
    write_long(code, 2, 0xffffffff);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_moveq_minus_one(uae_u8 *code)
{
    write_word(code, 0, 0x70ff);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_ext_long(uae_u8 *code)
{
    write_word(code, 0, 0x48c0);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_swap(uae_u8 *code)
{
    write_word(code, 0, 0x4840);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_decrement_loop(uae_u8 *code)
{
    write_word(code, 0, 0x7003);
    write_word(code, 2, 0x5380);
    write_word(code, 4, 0x66fc);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_beq_taken(uae_u8 *code)
{
    write_word(code, 0, 0x7000);
    write_word(code, 2, 0x6702);
    write_word(code, 4, 0x7001);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_beq_not_taken(uae_u8 *code)
{
    write_word(code, 0, 0x7001);
    write_word(code, 2, 0x6702);
    write_word(code, 4, 0x7002);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_bra_taken(uae_u8 *code)
{
    write_word(code, 0, 0x7001);
    write_word(code, 2, 0x6002);
    write_word(code, 4, 0x7002);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_disp_load_long(uae_u8 *code)
{
    write_word(code, 0, 0x2028);
    write_word(code, 2, 0x0000);
    write_word(code, 4, M68K_EXEC_RETURN);
}

static void emit_absolute_load_long(uae_u8 *code)
{
    write_word(code, 0, 0x2039);
    write_long(code, 2, guest_data_offset);
    write_word(code, 6, M68K_EXEC_RETURN);
}

static void emit_cmpi_equal_branch(uae_u8 *code)
{
    write_word(code, 0, 0x7005);
    write_word(code, 2, 0x0c80);
    write_long(code, 4, 5);
    write_word(code, 8, 0x6702);
    write_word(code, 10, 0x7001);
    write_word(code, 12, M68K_EXEC_RETURN);
}

static void emit_cmpi_not_equal_branch(uae_u8 *code)
{
    write_word(code, 0, 0x7004);
    write_word(code, 2, 0x0c80);
    write_long(code, 4, 5);
    write_word(code, 8, 0x6602);
    write_word(code, 10, 0x7001);
    write_word(code, 12, M68K_EXEC_RETURN);
}

static void emit_lsl_long(uae_u8 *code)
{
    write_word(code, 0, 0xe388);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_lsr_long(uae_u8 *code)
{
    write_word(code, 0, 0xe288);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static CpuSnapshot capture_snapshot()
{
    CpuSnapshot snapshot{};
    for (int i = 0; i < 8; i++) {
        snapshot.d[i] = m68k_dreg(regs, i);
        snapshot.a[i] = m68k_areg(regs, i);
    }
    snapshot.pc = m68k_getpc();
    snapshot.sr = regs.sr;
    snapshot.data_value = get_long(guest_data_offset);
    snapshot.stack_sr = get_word(m68k_areg(regs, 7));
    snapshot.stack_pc = get_long(m68k_areg(regs, 7) + 2);
    snapshot.stack_format = get_word(m68k_areg(regs, 7) + 6);
    return snapshot;
}

static void prepare_case(const TestCase &test_case, bool jit)
{
    guest_memory.fill(0);
    write_long(guest_memory.data(), guest_data_offset, test_case.data_value);
    test_case.emit(guest_memory.data() + test_case.offset);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(guest_memory.size());
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    m68k_dreg(regs, 0) = test_case.initial_d0;
    m68k_areg(regs, 0) = test_case.initial_a0;
    m68k_setpc(test_case.offset);
    MakeFromSR();
    regs.spcflags = 0;
}

static CpuSnapshot run_case(const TestCase &test_case, bool jit)
{
    prepare_case(test_case, jit);

    if (jit)
        m68k_compile_execute();
    else
        m68k_do_execute();

    MakeSR();
    return capture_snapshot();
}

static CpuSnapshot run_trap_case(bool jit)
{
    guest_memory.fill(0);
    write_long(guest_memory.data(), 4 * 32, trap_handler_offset);
    write_word(guest_memory.data() + trap_code_offset, 0, 0x4e40);
    write_word(guest_memory.data() + trap_code_offset, 2, M68K_EXEC_RETURN);
    write_word(guest_memory.data() + trap_handler_offset, 0, 0x7007);
    write_word(guest_memory.data() + trap_handler_offset, 2, M68K_EXEC_RETURN);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(guest_memory.size());
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    regs.vbr = 0;
    regs.usp = trap_stack_offset;
    regs.isp = trap_stack_offset;
    regs.msp = trap_stack_offset;
    m68k_areg(regs, 7) = trap_stack_offset;
    m68k_setpc(trap_code_offset);
    MakeFromSR();
    regs.spcflags = 0;

    if (jit)
        m68k_compile_execute();
    else
        m68k_do_execute();

    MakeSR();
    return capture_snapshot();
}

static CpuSnapshot run_subroutine_case(bool jit)
{
    guest_memory.fill(0);
    write_word(guest_memory.data() + subroutine_code_offset, 0, 0x4eb9);
    write_long(guest_memory.data() + subroutine_code_offset, 2, subroutine_handler_offset);
    write_word(guest_memory.data() + subroutine_code_offset, 6, M68K_EXEC_RETURN);
    write_word(guest_memory.data() + subroutine_handler_offset, 0, 0x7007);
    write_word(guest_memory.data() + subroutine_handler_offset, 2, 0x4e75);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(guest_memory.size());
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    regs.usp = subroutine_stack_offset;
    regs.isp = subroutine_stack_offset;
    regs.msp = subroutine_stack_offset;
    m68k_areg(regs, 7) = subroutine_stack_offset;
    m68k_setpc(subroutine_code_offset);
    MakeFromSR();
    regs.spcflags = 0;

    if (jit)
        m68k_compile_execute();
    else
        m68k_do_execute();

    MakeSR();
    return capture_snapshot();
}

static CpuSnapshot run_loop_case(bool jit)
{
    guest_memory.fill(0);
    emit_decrement_loop(guest_memory.data() + loop_code_offset);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(guest_memory.size());
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    m68k_setpc(loop_code_offset);
    MakeFromSR();
    regs.spcflags = 0;

    if (jit)
        m68k_compile_execute();
    else
        m68k_do_execute();

    MakeSR();
    return capture_snapshot();
}

static bool snapshots_match(const TestCase &test_case,
    const CpuSnapshot &interpreter, const CpuSnapshot &jit)
{
    return interpreter.d == jit.d &&
        interpreter.a == jit.a &&
        interpreter.pc == jit.pc &&
        interpreter.sr == jit.sr &&
        interpreter.d[0] == test_case.expected_d0 &&
        interpreter.a[0] == test_case.expected_a0 &&
        interpreter.data_value == test_case.expected_data_value &&
        (test_case.expected_sr_mask == 0 ||
            (interpreter.sr & test_case.expected_sr_mask) == test_case.expected_sr_bits) &&
        interpreter.pc == test_case.offset + test_case.retired_length;
}

int main()
{
    std::printf("UAE_CPU_INTEGRATION_BEGIN\n");
    std::printf("regstruct_size=%zu\n", sizeof(regs));

    init_m68k();
    std::printf("UAE_CPU_RUNTIME_FIXTURE_LINKED\n");

    const std::array<TestCase, 30> test_cases = {{
        {"MOVEQ", guest_code_offset, 0, 5, 0, 0, 0, 0, 2, emit_moveq},
        {"ADDI.L", guest_code_offset + 0x100, 5, 6, 0, 0, 0, 0, 6, emit_addi},
        {"SUBI.L", guest_code_offset + 0x200, 5, 4, 0, 0, 0, 0, 6, emit_subi},
        {"ANDI.L", guest_code_offset + 0x300, 0xf0f0f0f0, 0x00000000, 0, 0, 0, 0, 6, emit_andi},
        {"ORI.L", guest_code_offset + 0x400, 0xf0f0f0f0, 0xffffffff, 0, 0, 0, 0, 6, emit_ori},
        {"STORE.L", guest_code_offset + 0x500, 0x12345678, 0x12345678, guest_data_offset, guest_data_offset, 0, 0x12345678, 2, emit_store_long},
        {"LOAD.L", guest_code_offset + 0x600, 0, 0x12345678, guest_data_offset, guest_data_offset, 0x12345678, 0x12345678, 2, emit_load_long},
        {"LOAD.L_POSTINC", guest_code_offset + 0x700, 0, 0x12345678, guest_data_offset, guest_data_offset + 4, 0x12345678, 0x12345678, 2, emit_load_long_postincrement},
        {"LOAD.L_PREDEC", guest_code_offset + 0x780, 0, 0x12345678, guest_data_offset + 4, guest_data_offset, 0x12345678, 0x12345678, 2, emit_load_long_predecrement},
        {"ALU_SEQUENCE", guest_code_offset + 0x1500, 0, 2, 0, 0, 0, 0, 14, emit_alu_sequence},
        {"ADDI_MINUS_ONE", guest_code_offset + 0x1600, 0, 0xffffffff, 0, 0, 0, 0, 6, emit_addi_minus_one, 0x001f, 0x0008},
        {"SUBI_BORROW", guest_code_offset + 0x1700, 0, 0xffffffff, 0, 0, 0, 0, 6, emit_subi, 0x001f, 0x0019},
        {"MOVEQ_SIGN_EXTEND", guest_code_offset + 0x1780, 0, 0xffffffff, 0, 0, 0, 0, 2, emit_moveq_minus_one, 0x001f, 0x0008},
        {"ADDI_WRAP_TO_ZERO", guest_code_offset + 0x1800, 0xffffffff, 0, 0, 0, 0, 0, 6, emit_addi, 0x001f, 0x0015},
        {"ANDI_MIXED_BITS", guest_code_offset + 0x1880, 0x12345678, 0x02040608, 0, 0, 0, 0, 6, emit_andi, 0x001f, 0x0000},
        {"ORI_FROM_ZERO", guest_code_offset + 0x1900, 0, 0x0f0f0f0f, 0, 0, 0, 0, 6, emit_ori, 0x001f, 0x0000},
        {"SUBI_FROM_ZERO", guest_code_offset + 0x1980, 0, 0xffffffff, 0, 0, 0, 0, 6, emit_subi, 0x001f, 0x0019},
        {"EXT.L_SIGN_EXTEND", guest_code_offset + 0x1a00, 0x00008000, 0xffff8000, 0, 0, 0, 0, 2, emit_ext_long},
        {"SWAP", guest_code_offset + 0x1a80, 0x12345678, 0x56781234, 0, 0, 0, 0, 2, emit_swap},
        {"BEQ_TAKEN", guest_code_offset + 0x800, 0, 0, 0, 0, 0, 0, 6, emit_beq_taken},
        {"BEQ_NOT_TAKEN", guest_code_offset + 0x900, 0, 2, 0, 0, 0, 0, 6, emit_beq_not_taken},
        {"BRA_TAKEN", guest_code_offset + 0xa00, 0, 1, 0, 0, 0, 0, 6, emit_bra_taken},
        {"LOAD.L_DISP16", guest_code_offset + 0xb00, 0, 0x12345678, guest_data_offset, guest_data_offset, 0x12345678, 0x12345678, 4, emit_disp_load_long},
        {"LOAD.L_ABS32", guest_code_offset + 0xc00, 0, 0x12345678, 0, 0, 0x12345678, 0x12345678, 6, emit_absolute_load_long},
        {"CMPI_EQUAL_BEQ", guest_code_offset + 0xd00, 0, 5, 0, 0, 0, 0, 12, emit_cmpi_equal_branch},
        {"CMPI_NOTEQUAL_BNE", guest_code_offset + 0xe00, 0, 4, 0, 0, 0, 0, 12, emit_cmpi_not_equal_branch},
        {"ADDI_OVERFLOW", guest_code_offset + 0xf00, 0x7fffffff, 0x80000000, 0, 0, 0, 0, 6, emit_addi, 0x001f, 0x000a},
        {"SUBI_OVERFLOW", guest_code_offset + 0xf80, 0x80000000, 0x7fffffff, 0, 0, 0, 0, 6, emit_subi, 0x001f, 0x0002},
        {"LSL.L", guest_code_offset + 0x1100, 0x40000000, 0x80000000, 0, 0, 0, 0, 2, emit_lsl_long},
        {"LSR.L", guest_code_offset + 0x1200, 1, 0, 0, 0, 0, 0, 2, emit_lsr_long},
    }};

    prepare_case(test_cases[0], true);
    regs.spcflags = SPCFLAG_BRK;
    quit_program = 1;
    compiler_init();
    bool all_pass = true;
    for (const TestCase &test_case : test_cases) {
        const CpuSnapshot interpreter = run_case(test_case, false);
        const CpuSnapshot jit = run_case(test_case, true);
        const CpuSnapshot jit_repeat = run_case(test_case, true);
        const bool test_pass = snapshots_match(test_case, interpreter, jit) &&
            jit_repeat.d == jit.d &&
            jit_repeat.a == jit.a &&
            jit_repeat.pc == jit.pc &&
            jit_repeat.sr == jit.sr &&
            jit_repeat.data_value == jit.data_value;
        all_pass = all_pass && test_pass;
        std::printf("UAE_CPU_CASE_%s interp_d0=%08x jit_d0=%08x interp_a0=%08x jit_a0=%08x interp_mem=%08x jit_mem=%08x interp_pc=%08x jit_pc=%08x interp_sr=%04x jit_sr=%04x %s\n",
            test_case.name,
            static_cast<unsigned>(interpreter.d[0]),
            static_cast<unsigned>(jit.d[0]),
            static_cast<unsigned>(interpreter.a[0]),
            static_cast<unsigned>(jit.a[0]),
            static_cast<unsigned>(interpreter.data_value),
            static_cast<unsigned>(jit.data_value),
            static_cast<unsigned>(interpreter.pc),
            static_cast<unsigned>(jit.pc),
            static_cast<unsigned>(interpreter.sr),
            static_cast<unsigned>(jit.sr),
            test_pass ? "PASS" : "FAIL");
    }

    const CpuSnapshot trap_interpreter = run_trap_case(false);
    const CpuSnapshot trap_jit = run_trap_case(true);
    const bool trap_pass = trap_interpreter.d == trap_jit.d &&
        trap_interpreter.a == trap_jit.a &&
        trap_interpreter.pc == trap_jit.pc &&
        trap_interpreter.sr == trap_jit.sr &&
        trap_interpreter.stack_sr == trap_jit.stack_sr &&
        trap_interpreter.stack_pc == trap_jit.stack_pc &&
        trap_interpreter.stack_format == trap_jit.stack_format &&
        trap_interpreter.d[0] == 7 &&
        trap_interpreter.pc == trap_handler_offset + 2;
    all_pass = all_pass && trap_pass;
    std::printf("UAE_CPU_CASE_TRAP_VECTOR interp_d0=%08x jit_d0=%08x interp_a7=%08x jit_a7=%08x interp_frame=%04x:%08x:%04x jit_frame=%04x:%08x:%04x interp_pc=%08x jit_pc=%08x interp_sr=%04x jit_sr=%04x %s\n",
        static_cast<unsigned>(trap_interpreter.d[0]),
        static_cast<unsigned>(trap_jit.d[0]),
        static_cast<unsigned>(trap_interpreter.a[7]),
        static_cast<unsigned>(trap_jit.a[7]),
        static_cast<unsigned>(trap_interpreter.stack_sr),
        static_cast<unsigned>(trap_interpreter.stack_pc),
        static_cast<unsigned>(trap_interpreter.stack_format),
        static_cast<unsigned>(trap_jit.stack_sr),
        static_cast<unsigned>(trap_jit.stack_pc),
        static_cast<unsigned>(trap_jit.stack_format),
        static_cast<unsigned>(trap_interpreter.pc),
        static_cast<unsigned>(trap_jit.pc),
        static_cast<unsigned>(trap_interpreter.sr),
        static_cast<unsigned>(trap_jit.sr),
        trap_pass ? "PASS" : "FAIL");

    const CpuSnapshot subroutine_interpreter = run_subroutine_case(false);
    const CpuSnapshot subroutine_jit = run_subroutine_case(true);
    const bool subroutine_pass = subroutine_interpreter.d == subroutine_jit.d &&
        subroutine_interpreter.a == subroutine_jit.a &&
        subroutine_interpreter.pc == subroutine_jit.pc &&
        subroutine_interpreter.sr == subroutine_jit.sr &&
        subroutine_interpreter.d[0] == 7 &&
        subroutine_interpreter.a[7] == subroutine_stack_offset &&
        subroutine_interpreter.pc == subroutine_code_offset + 6;
    all_pass = all_pass && subroutine_pass;
    std::printf("UAE_CPU_CASE_JSR_RTS interp_d0=%08x jit_d0=%08x interp_a7=%08x jit_a7=%08x interp_pc=%08x jit_pc=%08x interp_sr=%04x jit_sr=%04x %s\n",
        static_cast<unsigned>(subroutine_interpreter.d[0]),
        static_cast<unsigned>(subroutine_jit.d[0]),
        static_cast<unsigned>(subroutine_interpreter.a[7]),
        static_cast<unsigned>(subroutine_jit.a[7]),
        static_cast<unsigned>(subroutine_interpreter.pc),
        static_cast<unsigned>(subroutine_jit.pc),
        static_cast<unsigned>(subroutine_interpreter.sr),
        static_cast<unsigned>(subroutine_jit.sr),
        subroutine_pass ? "PASS" : "FAIL");

    const CpuSnapshot loop_interpreter = run_loop_case(false);
    const CpuSnapshot loop_jit = run_loop_case(true);
    const CpuSnapshot loop_jit_repeat = run_loop_case(true);
    const bool loop_pass = loop_interpreter.d == loop_jit.d &&
        loop_interpreter.a == loop_jit.a &&
        loop_interpreter.pc == loop_jit.pc &&
        loop_interpreter.sr == loop_jit.sr &&
        loop_jit_repeat.d == loop_jit.d &&
        loop_jit_repeat.a == loop_jit.a &&
        loop_jit_repeat.pc == loop_jit.pc &&
        loop_jit_repeat.sr == loop_jit.sr &&
        loop_interpreter.d[0] == 0 &&
        loop_interpreter.pc == loop_code_offset + 6;
    all_pass = all_pass && loop_pass;
    std::printf("UAE_CPU_CASE_DECREMENT_LOOP interp_d0=%08x jit_d0=%08x interp_pc=%08x jit_pc=%08x interp_sr=%04x jit_sr=%04x %s\n",
        static_cast<unsigned>(loop_interpreter.d[0]),
        static_cast<unsigned>(loop_jit.d[0]),
        static_cast<unsigned>(loop_interpreter.pc),
        static_cast<unsigned>(loop_jit.pc),
        static_cast<unsigned>(loop_interpreter.sr),
        static_cast<unsigned>(loop_jit.sr),
        loop_pass ? "PASS" : "FAIL");

    const bool jit_pass = all_pass;
    std::printf("UAE_CPU_JIT_%s\n", jit_pass ? "PASS" : "FAIL");
    compiler_exit();

    exit_m68k();

    if (!jit_pass)
        return 1;

    std::printf("UAE_CPU_INTEGRATION_PASS\n");
    return 0;
}
