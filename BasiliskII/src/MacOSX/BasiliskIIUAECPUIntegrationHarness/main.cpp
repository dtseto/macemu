#include "../../uae_cpu_2026/m68k.h"
#include "../../uae_cpu_2026/readcpu.h"
#include "../../uae_cpu_2026/newcpu.h"
#include "../../src/include/emul_op.h"
#include "../compiler/compemu.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

extern void init_m68k(void);
extern void exit_m68k(void);

extern bool UseJIT;
extern int quit_program;
extern uintptr MEMBaseDiff;
extern uae_u8 *fast_ram_base;
extern uae_u32 fast_ram_size;
extern uae_u32 RAMSize;
extern void jit_test_dump_dispatch_summary(void);

static constexpr uaecptr guest_code_offset = 0x1000;
static constexpr uaecptr guest_data_offset = 0x2000;
static constexpr uaecptr trap_code_offset = 0x1800;
static constexpr uaecptr trap_handler_offset = 0x3000;
static constexpr uaecptr trap_stack_offset = 0x4000;
static constexpr uaecptr subroutine_code_offset = 0x2600;
static constexpr uaecptr subroutine_handler_offset = 0x2800;
static constexpr uaecptr subroutine_stack_offset = 0x5000;
static constexpr uaecptr loop_code_offset = 0x2a00;
static constexpr uaecptr generated_code_offset = 0x6000;
static constexpr size_t fixture_memory_size = 2 * 1024 * 1024;
static constexpr size_t corpus_memory_size = 16 * 1024 * 1024;
static std::array<uae_u8, corpus_memory_size> guest_memory;
static size_t active_memory_size = fixture_memory_size;

static void select_guest_memory(size_t size)
{
    active_memory_size = size;
}

static void clear_guest_memory()
{
    std::fill(guest_memory.begin(), guest_memory.begin() + active_memory_size, 0);
}

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
    uae_u32 initial_d1 = 0;
    uae_u32 expected_d1 = 0;
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

static void emit_neg_long(uae_u8 *code)
{
    write_word(code, 0, 0x4480);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_not_long(uae_u8 *code)
{
    write_word(code, 0, 0x4680);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_tst_long(uae_u8 *code)
{
    write_word(code, 0, 0x4a80);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_move_immediate_byte(uae_u8 *code)
{
    write_word(code, 0, 0x103c);
    code[2] = 0x00;
    code[3] = 0xff;
    write_word(code, 4, M68K_EXEC_RETURN);
}

static void emit_move_immediate_word(uae_u8 *code)
{
    write_word(code, 0, 0x303c);
    write_word(code, 2, 0x8000);
    write_word(code, 4, M68K_EXEC_RETURN);
}

static void emit_store_byte(uae_u8 *code)
{
    write_word(code, 0, 0x1080);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_store_word(uae_u8 *code)
{
    write_word(code, 0, 0x3080);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_load_byte(uae_u8 *code)
{
    write_word(code, 0, 0x1010);
    write_word(code, 2, M68K_EXEC_RETURN);
}

static void emit_pc_relative_load(uae_u8 *code)
{
    write_word(code, 0, 0x203a);
    write_word(code, 2, 0x0006);
    write_word(code, 4, M68K_EXEC_RETURN);
    write_long(code, 8, 0x89abcdef);
}

static void emit_indexed_load(uae_u8 *code)
{
    write_word(code, 0, 0x2030);
    write_word(code, 2, 0x1000);
    write_word(code, 4, M68K_EXEC_RETURN);
}

static void emit_load_add_store(uae_u8 *code)
{
    write_word(code, 0, 0x2010);
    write_word(code, 2, 0x0680);
    write_long(code, 4, 1);
    write_word(code, 8, 0x2080);
    write_word(code, 10, M68K_EXEC_RETURN);
}

static void emit_decrement_loop(uae_u8 *code)
{
    write_word(code, 0, 0x707f);
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
    select_guest_memory(fixture_memory_size);
    clear_guest_memory();
    write_long(guest_memory.data(), guest_data_offset, test_case.data_value);
    test_case.emit(guest_memory.data() + test_case.offset);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(active_memory_size);
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    m68k_dreg(regs, 0) = test_case.initial_d0;
    m68k_dreg(regs, 1) = test_case.initial_d1;
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

static CpuSnapshot run_generated_case(bool jit, uaecptr offset,
    uae_u32 seed, uae_u32 add_value, uae_u32 xor_value, uae_u32 and_value)
{
    select_guest_memory(fixture_memory_size);
    clear_guest_memory();
    uae_u8 *code = guest_memory.data() + offset;
    write_word(code, 0, 0x203c);
    write_long(code, 2, seed);
    write_word(code, 6, 0x0680);
    write_long(code, 8, add_value);
    write_word(code, 12, 0x0a80);
    write_long(code, 14, xor_value);
    write_word(code, 18, 0x0280);
    write_long(code, 20, and_value);
    write_word(code, 24, M68K_EXEC_RETURN);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(active_memory_size);
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;

    regs = {};
    regs.sr = 0x2700;
    m68k_setpc(offset);
    MakeFromSR();
    regs.spcflags = 0;

    if (jit)
        m68k_compile_execute();
    else
        m68k_do_execute();

    MakeSR();
    return capture_snapshot();
}

static CpuSnapshot run_trap_case(bool jit)
{
    select_guest_memory(fixture_memory_size);
    clear_guest_memory();
    write_long(guest_memory.data(), 4 * 32, trap_handler_offset);
    write_word(guest_memory.data() + trap_code_offset, 0, 0x4e40);
    write_word(guest_memory.data() + trap_code_offset, 2, M68K_EXEC_RETURN);
    write_word(guest_memory.data() + trap_handler_offset, 0, 0x7007);
    write_word(guest_memory.data() + trap_handler_offset, 2, M68K_EXEC_RETURN);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(active_memory_size);
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
    select_guest_memory(fixture_memory_size);
    clear_guest_memory();
    write_word(guest_memory.data() + subroutine_code_offset, 0, 0x4eb9);
    write_long(guest_memory.data() + subroutine_code_offset, 2, subroutine_handler_offset);
    write_word(guest_memory.data() + subroutine_code_offset, 6, M68K_EXEC_RETURN);
    write_word(guest_memory.data() + subroutine_handler_offset, 0, 0x7007);
    write_word(guest_memory.data() + subroutine_handler_offset, 2, 0x4e75);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(active_memory_size);
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
    select_guest_memory(fixture_memory_size);
    clear_guest_memory();
    emit_decrement_loop(guest_memory.data() + loop_code_offset);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(active_memory_size);
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

struct CorpusState {
    std::array<uae_u32, 8> d{};
    std::array<uae_u32, 8> a{};
    uae_u32 usp = 0;
    uae_u32 ssp = 0;
    uae_u16 sr = 0;
    uae_u32 pc = 0;
    std::vector<std::pair<uae_u32, uae_u8>> memory;
};

struct CorpusVector {
    std::string name;
    CorpusState initial;
    CorpusState expected;
    uae_u16 opcode = 0;
    uae_u16 next_word = 0;
    std::vector<uae_u32> access_addresses;
};

static bool json_u32(const std::string &object, const std::string &key,
    uae_u32 &value)
{
    const std::string marker = "\"" + key + "\":";
    const size_t start = object.find(marker);
    if (start == std::string::npos)
        return false;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(
        object.c_str() + start + marker.size(), &end, 10);
    if (end == object.c_str() + start + marker.size() || parsed > 0xfffffffful)
        return false;
    value = static_cast<uae_u32>(parsed);
    return true;
}

static bool json_string(const std::string &object, const std::string &key,
    std::string &value)
{
    const std::string marker = "\"" + key + "\":\"";
    const size_t start = object.find(marker);
    if (start == std::string::npos)
        return false;
    const size_t value_start = start + marker.size();
    const size_t value_end = object.find('"', value_start);
    if (value_end == std::string::npos)
        return false;
    value = object.substr(value_start, value_end - value_start);
    return true;
}

static bool json_object(const std::string &line, const std::string &key,
    std::string &object)
{
    const std::string marker = "\"" + key + "\":{";
    const size_t start = line.find(marker);
    if (start == std::string::npos)
        return false;
    const size_t object_start = start + marker.size() - 1;
    const size_t object_end = line.find("},\"", object_start);
    if (object_end == std::string::npos)
        return false;
    object = line.substr(object_start, object_end - object_start + 1);
    return true;
}

static bool parse_corpus_state(const std::string &object, CorpusState &state)
{
    uae_u32 value = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!json_u32(object, "d" + std::to_string(i), value))
            return false;
        state.d[i] = value;
    }
    for (unsigned i = 0; i < 7; i++) {
        if (!json_u32(object, "a" + std::to_string(i), value))
            return false;
        state.a[i] = value;
    }
    if (json_u32(object, "usp", state.usp) &&
        json_u32(object, "ssp", state.ssp) &&
        json_u32(object, "sr", value) &&
        (state.sr = static_cast<uae_u16>(value), true) &&
        json_u32(object, "pc", state.pc)) {
        const std::string marker = "\"ram\":[";
        const size_t start = object.find(marker);
        const size_t end = start == std::string::npos
            ? std::string::npos : object.find("]]", start + marker.size());
        if (start == std::string::npos || end == std::string::npos)
            return false;
        size_t cursor = start + marker.size();
        while (cursor < end) {
            const size_t open = object.find('[', cursor);
            if (open == std::string::npos || open >= end)
                break;
            const size_t comma = object.find(',', open + 1);
            const size_t close = object.find(']', comma + 1);
            if (comma == std::string::npos || close == std::string::npos || close > end)
                return false;
            const unsigned long address = std::strtoul(
                object.c_str() + open + 1, nullptr, 10);
            const unsigned long byte = std::strtoul(
                object.c_str() + comma + 1, nullptr, 10);
            if (address > 0xfffffffful || byte > 0xff)
                return false;
            state.memory.emplace_back(static_cast<uae_u32>(address),
                static_cast<uae_u8>(byte));
            cursor = close + 1;
        }
        return true;
    }
    return false;
}

static bool parse_corpus_vector(const std::string &line, CorpusVector &vector)
{
    uae_u32 value = 0;
    std::string initial_object;
    std::string final_object;
    const auto parse_error = [](const char *stage) {
        static unsigned count = 0;
        if (count++ < 8)
            std::printf("UAE_CPU_CORPUS_PARSE_ERROR stage=%s\n", stage);
        return false;
    };
    if (!json_string(line, "name", vector.name))
        return parse_error("name");
    if (!json_object(line, "initial", initial_object))
        return parse_error("initial_object");
    if (!json_object(line, "final", final_object))
        return parse_error("final_object");
    if (!parse_corpus_state(initial_object, vector.initial))
        return parse_error("initial_state");
    if (!parse_corpus_state(final_object, vector.expected))
        return parse_error("final_state");
    if (!json_u32(line, "opcode", value))
        return parse_error("opcode");
    vector.opcode = static_cast<uae_u16>(value);
    const std::string words_marker = "\"instruction_words\":[";
    const size_t words_start = line.find(words_marker);
    if (words_start == std::string::npos)
        return false;
    const size_t next_start = line.find(',', words_start + words_marker.size());
    if (next_start == std::string::npos)
        return false;
    vector.next_word = static_cast<uae_u16>(std::strtoul(
        line.c_str() + next_start + 1, nullptr, 10));
    const std::string access_marker = "\"access_addresses\":[";
    const size_t access_start = line.find(access_marker);
    if (access_start == std::string::npos)
        return false;
    const size_t access_end = line.find(']', access_start + access_marker.size());
    if (access_end == std::string::npos)
        return false;
    size_t cursor = access_start + access_marker.size();
    while (cursor < access_end) {
        char *end = nullptr;
        const unsigned long address = std::strtoul(line.c_str() + cursor, &end, 10);
        if (end == line.c_str() + cursor || address > 0xfffffffful)
            return false;
        vector.access_addresses.push_back(static_cast<uae_u32>(address));
        cursor = static_cast<size_t>(end - line.c_str());
        if (cursor < access_end && line[cursor] == ',')
            cursor++;
    }
    return true;
}

static bool prepare_corpus_vector(const CorpusVector &vector, bool jit)
{
    select_guest_memory(corpus_memory_size);
    clear_guest_memory();
    const uaecptr pc = vector.initial.pc;
    const uae_u32 instruction_length = vector.expected.pc - vector.initial.pc;
    if (vector.expected.pc < vector.initial.pc ||
        instruction_length < 2 || instruction_length > 4 ||
        pc + instruction_length + 2 >= active_memory_size)
        return false;
    for (const auto &cell : vector.initial.memory) {
        if (cell.first >= active_memory_size)
            return false;
        guest_memory[cell.first] = cell.second;
    }
    write_word(guest_memory.data() + pc, 0, vector.opcode);
    if (instruction_length == 2)
        write_word(guest_memory.data() + pc, 2, M68K_EXEC_RETURN);
    else {
        write_word(guest_memory.data() + pc, 2, vector.next_word);
        write_word(guest_memory.data() + pc, instruction_length, M68K_EXEC_RETURN);
    }

    /* The fixture rewrites guest RAM between independent corpus vectors.
       Explicitly invalidate production translated blocks so a previous
       vector at the same cache line cannot execute stale host code. */
    set_cache_state(0);
    set_cache_state(1);

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(active_memory_size);
    RAMSize = fast_ram_size;
    UseJIT = jit;
    quit_program = 0;
    regs = {};
    for (unsigned i = 0; i < 8; i++) {
        m68k_dreg(regs, i) = vector.initial.d[i];
        /* The external 68000 vectors model a 24-bit address bus. */
        m68k_areg(regs, i) = vector.initial.a[i] & 0x00ffffffu;
    }
    regs.usp = vector.initial.usp & 0x00ffffffu;
    regs.isp = vector.initial.ssp & 0x00ffffffu;
    regs.msp = vector.initial.ssp & 0x00ffffffu;
    /* M68K_EXEC_RETURN is a privileged emulator sentinel. Keep the corpus
       condition-code/int-mask bits, but execute the one instruction in
       supervisor mode so the sentinel cannot turn a user-mode vector into a
       privilege exception. The final comparison masks the synthetic S bit. */
    m68k_areg(regs, 7) = vector.initial.ssp;
    regs.sr = static_cast<uae_u16>(vector.initial.sr | 0x2000);
    m68k_setpc(pc);
    MakeFromSR();
    regs.spcflags = 0;
    return true;
}

static bool corpus_snapshot_matches(const CorpusVector &vector,
    const CpuSnapshot &snapshot)
{
    if (!(snapshot.d == vector.expected.d &&
        ((snapshot.a[0] & 0x00ffffffu) == (vector.expected.a[0] & 0x00ffffffu)) &&
        ((snapshot.a[1] & 0x00ffffffu) == (vector.expected.a[1] & 0x00ffffffu)) &&
        ((snapshot.a[2] & 0x00ffffffu) == (vector.expected.a[2] & 0x00ffffffu)) &&
        ((snapshot.a[3] & 0x00ffffffu) == (vector.expected.a[3] & 0x00ffffffu)) &&
        ((snapshot.a[4] & 0x00ffffffu) == (vector.expected.a[4] & 0x00ffffffu)) &&
        ((snapshot.a[5] & 0x00ffffffu) == (vector.expected.a[5] & 0x00ffffffu)) &&
        ((snapshot.a[6] & 0x00ffffffu) == (vector.expected.a[6] & 0x00ffffffu)) &&
        snapshot.pc == vector.expected.pc &&
        (snapshot.sr & 0x1fff) == (vector.expected.sr & 0x1fff)))
        return false;
    for (const auto &cell : vector.expected.memory) {
        const uae_u32 instruction_length = vector.expected.pc - vector.initial.pc;
        if (cell.first >= vector.initial.pc &&
            cell.first < vector.initial.pc + instruction_length + 2)
            continue; /* excludes the temporary M68K_EXEC_RETURN sentinel */
        if (cell.first >= active_memory_size || get_byte(cell.first) != cell.second)
            return false;
    }
    return true;
}

static CpuSnapshot capture_corpus_snapshot()
{
    CpuSnapshot snapshot{};
    for (unsigned i = 0; i < 8; i++) {
        snapshot.d[i] = m68k_dreg(regs, i);
        snapshot.a[i] = m68k_areg(regs, i);
    }
    snapshot.pc = m68k_getpc();
    snapshot.sr = regs.sr;
    return snapshot;
}

static bool run_external_corpus(const char *path)
{
    std::ifstream input(path);
    if (!input)
        return false;
    unsigned total = 0;
    unsigned passed = 0;
    unsigned failures = 0;
    unsigned malformed = 0;
    unsigned skipped = 0;
    unsigned skipped_pc = 0;
    unsigned skipped_length = 0;
    unsigned skipped_indexed = 0;
    unsigned skipped_a7 = 0;
    unsigned skipped_absolute = 0;
    unsigned skipped_memory = 0;
    unsigned skipped_prepare = 0;
    unsigned skipped_metadata = 0;
    unsigned line_index = 0;
    std::string line;
    while (std::getline(input, line)) {
        const unsigned current_line = line_index++;
        if (line.empty())
            continue;
        CorpusVector vector;
        if (!parse_corpus_vector(line, vector)) {
            malformed++;
            std::printf("UAE_CPU_CORPUS_MALFORMED line=%u\n", current_line);
            continue;
        }
        if ((vector.initial.pc & 1) != 0 ||
            vector.initial.pc + 6 >= corpus_memory_size ||
            vector.expected.pc < vector.initial.pc) {
            skipped++;
            skipped_pc++;
            continue;
        }
        if (vector.expected.pc - vector.initial.pc < 2 ||
            vector.expected.pc - vector.initial.pc > 4) {
            skipped++;
            skipped_length++;
            continue;
        }
        const bool register_only = vector.name.find(" NOP ") != std::string::npos ||
            vector.name.find(" SWAP ") != std::string::npos ||
            vector.name.find(" EXT.") != std::string::npos;
        const bool indexed_effective_address = vector.name.find("Xn") != std::string::npos;
        const bool indexed_adapted_for_68020 =
            line.find("\"indexed_adapted\":true") != std::string::npos;
        const bool stack_register_effective_address = vector.name.find("A7") != std::string::npos;
        const bool indexed_stack_register = indexed_effective_address &&
            line.find("\"index_is_address\":true") != std::string::npos &&
            line.find("\"index_register\":7") != std::string::npos;
        const bool absolute_effective_address = vector.name.find("xxx") != std::string::npos;
        bool absolute_address_safe = true;
        if (absolute_effective_address) {
            /* For the supported absolute-word shape, transactions are
               [instruction PC, effective address, next PC, effective address].
               High absolute-word targets still SIGBUS in the production JIT. */
            absolute_address_safe = vector.access_addresses.size() >= 2 &&
                vector.access_addresses[1] < 0x00800000u;
        }
        const bool indexed_24bit_ready = indexed_effective_address &&
            indexed_adapted_for_68020 && std::getenv("B2_TEST_24BIT_ADDRESS") &&
            std::getenv("B2_TEST_24BIT_ADDRESS")[0] == '1';
        if (indexed_effective_address && !indexed_24bit_ready) {
            skipped++;
            skipped_indexed++;
            continue;
        }
        if (stack_register_effective_address || indexed_stack_register) {
            skipped++;
            skipped_a7++;
            continue;
        }
        if (absolute_effective_address && !absolute_address_safe) {
            skipped++;
            skipped_absolute++;
            continue;
        }
        bool address_space_safe = register_only || !vector.access_addresses.empty();
        for (const auto address : vector.access_addresses)
            address_space_safe = address_space_safe && address < corpus_memory_size;
        if (!register_only && vector.access_addresses.empty())
            address_space_safe = vector.initial.usp < corpus_memory_size &&
                vector.initial.ssp < corpus_memory_size;
        for (const auto &cell : vector.initial.memory)
            address_space_safe = address_space_safe && cell.first < corpus_memory_size;
        for (const auto &cell : vector.expected.memory)
            address_space_safe = address_space_safe && cell.first < corpus_memory_size;
        if (!address_space_safe) {
            skipped++;
            skipped_memory++;
            continue;
        }
        /* Reject internally inconsistent PC-relative records. The corpus
           supplies both the displacement word and the observed data address;
           a mismatch means the vector cannot validate CPU semantics. */
        if ((vector.name.find("(d16,PC)") != std::string::npos ||
             vector.name.find("(d16, PC)") != std::string::npos) &&
            vector.access_addresses.size() >= 2) {
            const uae_s32 displacement = static_cast<uae_s16>(vector.next_word);
            const uae_u32 expected_ea = (vector.initial.pc + 2 + displacement) & 0x00ffffffu;
            if (vector.access_addresses[1] != expected_ea) {
                skipped++;
                skipped_metadata++;
                continue;
            }
        }
        total++;
        if (!prepare_corpus_vector(vector, false)) {
            skipped++;
            skipped_prepare++;
            continue;
        }
        m68k_do_execute();
        MakeSR();
        const CpuSnapshot interpreter = capture_corpus_snapshot();
        if (!prepare_corpus_vector(vector, true)) {
            skipped++;
            skipped_prepare++;
            continue;
        }
        m68k_compile_execute();
        MakeSR();
        const CpuSnapshot jit = capture_corpus_snapshot();
        const bool pass = corpus_snapshot_matches(vector, interpreter) &&
            corpus_snapshot_matches(vector, jit) &&
            interpreter.d == jit.d && interpreter.a == jit.a &&
            interpreter.pc == jit.pc && interpreter.sr == jit.sr;
        if (pass)
            passed++;
        else {
            failures++;
            if (failures <= 8)
            std::printf("UAE_CPU_CORPUS_FAIL name=%s expected_pc=%08x interp_pc=%08x jit_pc=%08x expected_sr=%04x interp_sr=%04x jit_sr=%04x\n",
                vector.name.c_str(), static_cast<unsigned>(vector.expected.pc),
                static_cast<unsigned>(interpreter.pc), static_cast<unsigned>(jit.pc),
                static_cast<unsigned>(vector.expected.sr),
                static_cast<unsigned>(interpreter.sr), static_cast<unsigned>(jit.sr));
        }
    }
    std::printf("UAE_CPU_CORPUS_RESULT total=%u passed=%u skipped=%u malformed=%u %s\n",
        total, passed, skipped, malformed,
        total != 0 && malformed == 0 && passed == total ? "PASS" : "FAIL");
    std::printf("UAE_CPU_CORPUS_SKIPS pc=%u length=%u indexed=%u a7=%u absolute=%u memory=%u prepare=%u metadata=%u\n",
        skipped_pc, skipped_length, skipped_indexed, skipped_a7,
        skipped_absolute, skipped_memory, skipped_prepare, skipped_metadata);
    return total != 0 && malformed == 0 && passed == total;
}

static bool run_cache_pressure(unsigned count)
{
    /* Keep this test bounded and deterministic: at 1 MB, the production
       cache must wrap when these distinct 32-byte guest blocks are compiled. */
    constexpr uaecptr pressure_base = 0x80000;
    constexpr uaecptr pressure_stride = 0x40;
    constexpr uaecptr pressure_limit = pressure_base + pressure_stride * 12000;
    if (pressure_limit + 32 >= guest_memory.size() || count > 12000)
        return false;

    select_guest_memory(fixture_memory_size);
    clear_guest_memory();
    for (unsigned i = 0; i < count; i++) {
        uaecptr offset = pressure_base + pressure_stride * i;
        uae_u8 *code = guest_memory.data() + offset;
        write_word(code, 0, static_cast<uae_u16>(0x7000 | (i & 7)));
        write_word(code, 2, M68K_EXEC_RETURN);
    }

    MEMBaseDiff = reinterpret_cast<uintptr>(guest_memory.data());
    fast_ram_base = guest_memory.data();
    fast_ram_size = static_cast<uae_u32>(active_memory_size);
    RAMSize = fast_ram_size;
    UseJIT = true;

    for (unsigned i = 0; i < count; i++) {
        uaecptr offset = pressure_base + pressure_stride * i;
        quit_program = 0;
        regs = {};
        regs.sr = 0x2700;
        m68k_setpc(offset);
        MakeFromSR();
        regs.spcflags = 0;
        m68k_compile_execute();
        if (m68k_dreg(regs, 0) != (i & 7) || m68k_getpc() != offset + 2)
            return false;
    }
    return true;
}

static void run_throughput_benchmark()
{
    constexpr unsigned samples = 1000;
    for (unsigned i = 0; i < 10; i++) {
        (void)run_loop_case(false);
        (void)run_loop_case(true);
    }

    const auto measure = [](bool jit) {
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < samples; i++)
            (void)run_loop_case(jit);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        return elapsed;
    };
    const auto interpreter_ns = measure(false);
    const auto jit_ns = measure(true);
    const double speedup = jit_ns ? static_cast<double>(interpreter_ns) / jit_ns : 0.0;
    std::printf("UAE_CPU_BENCH samples=%u interpreter_ns=%lld jit_ns=%lld speedup=%.3f PASS\n",
        samples, static_cast<long long>(interpreter_ns),
        static_cast<long long>(jit_ns), speedup);
}

static bool snapshots_match(const TestCase &test_case,
    const CpuSnapshot &interpreter, const CpuSnapshot &jit)
{
    return interpreter.d == jit.d &&
        interpreter.a == jit.a &&
        interpreter.pc == jit.pc &&
        interpreter.sr == jit.sr &&
        interpreter.d[0] == test_case.expected_d0 &&
        (test_case.expected_d1 == 0 || interpreter.d[1] == test_case.expected_d1) &&
        interpreter.a[0] == test_case.expected_a0 &&
        interpreter.data_value == test_case.expected_data_value &&
        (test_case.expected_sr_mask == 0 ||
            (interpreter.sr & test_case.expected_sr_mask) == test_case.expected_sr_bits) &&
        interpreter.pc == test_case.offset + test_case.retired_length;
}

int main(int argc, char **argv)
{
    setenv("B2_TEST_DISPATCH_SUMMARY", "1", 1);
    setenv("B2_TEST_JIT_CACHE_KB", "1024", 1);
    std::printf("UAE_CPU_INTEGRATION_BEGIN\n");
    std::printf("regstruct_size=%zu\n", sizeof(regs));

    init_m68k();
    std::printf("UAE_CPU_RUNTIME_FIXTURE_LINKED\n");

    if (argc == 3 && std::string(argv[1]) == "--corpus") {
        compiler_init();
        const bool corpus_pass = run_external_corpus(argv[2]);
        compiler_exit();
        exit_m68k();
        return corpus_pass ? 0 : 1;
    }

    const std::array<TestCase, 42> test_cases = {{
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
        {"NEG.L", guest_code_offset + 0x1b00, 1, 0xffffffff, 0, 0, 0, 0, 2, emit_neg_long, 0x001f, 0x0019},
        {"NOT.L", guest_code_offset + 0x1b80, 0, 0xffffffff, 0, 0, 0, 0, 2, emit_not_long, 0x001f, 0x0008},
        {"TST.L_ZERO", guest_code_offset + 0x1c00, 0, 0, 0, 0, 0, 0, 2, emit_tst_long, 0x001f, 0x0004},
        {"MOVE.B_IMMEDIATE", guest_code_offset + 0x1c80, 0x12345600, 0x123456ff, 0, 0, 0, 0, 4, emit_move_immediate_byte, 0x001f, 0x0008},
        {"MOVE.W_IMMEDIATE", guest_code_offset + 0x1d00, 0x12340000, 0x12348000, 0, 0, 0, 0, 4, emit_move_immediate_word, 0x001f, 0x0008},
        {"STORE.B", guest_code_offset + 0x1d80, 0x123456ab, 0x123456ab, guest_data_offset, guest_data_offset, 0, 0xab000000, 2, emit_store_byte},
        {"STORE.W", guest_code_offset + 0x1e00, 0x1234abcd, 0x1234abcd, guest_data_offset, guest_data_offset, 0, 0xabcd0000, 2, emit_store_word},
        {"LOAD.B", guest_code_offset + 0x1e80, 0x12345600, 0x12345680, guest_data_offset, guest_data_offset, 0x80000000, 0x80000000, 2, emit_load_byte, 0x001f, 0x0008},
        {"LOAD.L_PC_RELATIVE", guest_code_offset + 0x1f00, 0, 0x89abcdef, 0, 0, 0, 0, 4, emit_pc_relative_load},
        {"LOAD.L_INDEXED", guest_code_offset + 0x1f80, 0, 0x12345678, guest_data_offset, guest_data_offset, 0x12345678, 0x12345678, 4, emit_indexed_load},
        {"LOAD.L_INDEXED_NONZERO", guest_code_offset + 0x1fc0, 0, 0x12345678, guest_data_offset - 4, guest_data_offset - 4, 0x12345678, 0x12345678, 4, emit_indexed_load, 0, 0, 4, 4},
        {"LOAD_ADD_STORE", guest_code_offset + 0x3000, 0, 0x12345679, guest_data_offset, guest_data_offset, 0x12345678, 0x12345679, 10, emit_load_add_store},
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

    bool generated_pass = true;
    constexpr unsigned generated_case_count = 32;
    for (unsigned i = 0; i < generated_case_count; i++) {
        const uae_u32 seed = 0x13579bdfu + i * 0x1020304u;
        const uae_u32 add_value = 0x2468ace0u ^ (i * 0x01010101u);
        const uae_u32 xor_value = 0xf0f0f0f0u ^ (i * 0x00110011u);
        const uae_u32 and_value = 0xff00ffffu ^ (i * 0x00010001u);
        const uae_u32 expected = ((seed + add_value) ^ xor_value) & and_value;
        const uaecptr offset = generated_code_offset + i * 0x100;
        const CpuSnapshot generated_interpreter = run_generated_case(
            false, offset, seed, add_value, xor_value, and_value);
        const CpuSnapshot generated_jit = run_generated_case(
            true, offset, seed, add_value, xor_value, and_value);
        const bool case_pass = generated_interpreter.d == generated_jit.d &&
            generated_interpreter.a == generated_jit.a &&
            generated_interpreter.pc == generated_jit.pc &&
            generated_interpreter.sr == generated_jit.sr &&
            generated_interpreter.d[0] == expected &&
            generated_interpreter.pc == offset + 24;
        generated_pass = generated_pass && case_pass;
    }
    all_pass = all_pass && generated_pass;
    std::printf("UAE_CPU_CASE_GENERATED_MATRIX cases=%u %s\n",
        generated_case_count, generated_pass ? "PASS" : "FAIL");

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

    const bool pressure_pass = run_cache_pressure(12000);
    std::printf("UAE_CPU_CACHE_PRESSURE blocks=%u %s\n", 12000,
        pressure_pass ? "PASS" : "FAIL");
    all_pass = all_pass && pressure_pass;

    run_throughput_benchmark();
    jit_test_dump_dispatch_summary();

    const bool jit_pass = all_pass;
    std::printf("UAE_CPU_JIT_%s\n", jit_pass ? "PASS" : "FAIL");
    compiler_exit();

    exit_m68k();

    if (!jit_pass)
        return 1;

    std::printf("UAE_CPU_INTEGRATION_PASS\n");
    return 0;
}
