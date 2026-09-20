#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint16_t kX = 0x0010;
constexpr uint16_t kN = 0x0008;
constexpr uint16_t kZ = 0x0004;
constexpr uint16_t kV = 0x0002;
constexpr uint16_t kC = 0x0001;

enum class Operation {
    MoveQ,
    AddImmediate,
    SubImmediate,
    AndImmediate,
    OrImmediate,
    EorImmediate,
    Compare,
    Test,
    MoveLongPostIncrement,
    MoveLongDisplacement,
    BranchEqual
};

struct Instruction {
    Operation operation;
    uint32_t value = 0;
    uint32_t address = 0;
    int32_t displacement = 0;
};

struct CPUState {
    std::array<uint32_t, 8> data{};
    std::array<uint32_t, 8> address{};
    uint32_t pc = 0;
    uint16_t sr = 0;
    std::array<uint8_t, 256> memory{};
};

bool equalState(const CPUState &first, const CPUState &second)
{
    return first.data == second.data &&
           first.address == second.address &&
           first.pc == second.pc &&
           first.sr == second.sr &&
           first.memory == second.memory;
}

void fail(const char *message)
{
    std::fprintf(stderr, "SEMANTICS_68K_FAIL: %s\n", message);
    std::fflush(stderr);
    std::exit(1);
}

void check(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

void setLogicFlags(CPUState &state, uint32_t result)
{
    state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
    if (result == 0)
        state.sr |= kZ;
    if ((result & 0x80000000u) != 0)
        state.sr |= kN;
}

void setAddFlags(CPUState &state, uint32_t left, uint32_t right, uint32_t result)
{
    const bool carry = static_cast<uint64_t>(left) +
                           static_cast<uint64_t>(right) >
                       0xffffffffull;
    const bool overflow =
        ((~(left ^ right) & (left ^ result)) & 0x80000000u) != 0;

    state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC | kX));
    if (result == 0)
        state.sr |= kZ;
    if ((result & 0x80000000u) != 0)
        state.sr |= kN;
    if (overflow)
        state.sr |= kV;
    if (carry)
        state.sr |= kC | kX;
}

void setSubFlags(CPUState &state, uint32_t left, uint32_t right, uint32_t result)
{
    const bool borrow = left < right;
    const bool overflow =
        (((left ^ right) & (left ^ result)) & 0x80000000u) != 0;

    state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC | kX));
    if (result == 0)
        state.sr |= kZ;
    if ((result & 0x80000000u) != 0)
        state.sr |= kN;
    if (overflow)
        state.sr |= kV;
    if (borrow)
        state.sr |= kC | kX;
}

uint32_t readLong(const CPUState &state, uint32_t address)
{
    check(address + 3 < state.memory.size(), "long read exceeded test memory");
    return (static_cast<uint32_t>(state.memory[address]) << 24) |
           (static_cast<uint32_t>(state.memory[address + 1]) << 16) |
           (static_cast<uint32_t>(state.memory[address + 2]) << 8) |
           static_cast<uint32_t>(state.memory[address + 3]);
}

void writeLong(CPUState &state, uint32_t address, uint32_t value)
{
    check(address + 3 < state.memory.size(), "long write exceeded test memory");
    state.memory[address] = static_cast<uint8_t>(value >> 24);
    state.memory[address + 1] = static_cast<uint8_t>(value >> 16);
    state.memory[address + 2] = static_cast<uint8_t>(value >> 8);
    state.memory[address + 3] = static_cast<uint8_t>(value);
}

void executeReference(const std::vector<Instruction> &program, CPUState &state)
{
    size_t index = 0;
    while (index < program.size()) {
        const Instruction &instruction = program[index++];

        switch (instruction.operation) {
            case Operation::MoveQ: {
                const uint32_t result =
                    static_cast<uint32_t>(static_cast<int32_t>(
                        static_cast<int8_t>(instruction.value)));
                state.data[0] = result;
                setLogicFlags(state, result);
                state.pc += 2;
                break;
            }

            case Operation::AddImmediate: {
                const uint32_t left = state.data[0];
                const uint32_t result = left + instruction.value;
                state.data[0] = result;
                setAddFlags(state, left, instruction.value, result);
                state.pc += 6;
                break;
            }

            case Operation::SubImmediate: {
                const uint32_t left = state.data[0];
                const uint32_t result = left - instruction.value;
                state.data[0] = result;
                setSubFlags(state, left, instruction.value, result);
                state.pc += 6;
                break;
            }

            case Operation::AndImmediate:
                state.data[0] &= instruction.value;
                setLogicFlags(state, state.data[0]);
                state.pc += 6;
                break;

            case Operation::OrImmediate:
                state.data[0] |= instruction.value;
                setLogicFlags(state, state.data[0]);
                state.pc += 6;
                break;

            case Operation::EorImmediate:
                state.data[0] ^= instruction.value;
                setLogicFlags(state, state.data[0]);
                state.pc += 6;
                break;

            case Operation::Compare: {
                const uint32_t left = state.data[0];
                const uint32_t right = instruction.value;
                const uint32_t result = left - right;
                const bool borrow = left < right;
                const bool overflow =
                    (((left ^ right) & (left ^ result)) & 0x80000000u) != 0;

                // CMP updates N/Z/V/C but preserves X on 68000-family CPUs.
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & 0x80000000u) != 0)
                    state.sr |= kN;
                if (overflow)
                    state.sr |= kV;
                if (borrow)
                    state.sr |= kC;
                state.pc += 2;
                break;
            }

            case Operation::Test:
                setLogicFlags(state, state.data[0]);
                state.pc += 2;
                break;

            case Operation::MoveLongPostIncrement: {
                const uint32_t address = state.address[0];
                state.data[0] = readLong(state, address);
                state.address[0] += 4;
                setLogicFlags(state, state.data[0]);
                state.pc += 2;
                break;
            }

            case Operation::MoveLongDisplacement: {
                const uint32_t address =
                    state.address[0] + static_cast<uint32_t>(
                                           instruction.displacement);
                state.data[0] = readLong(state, address);
                setLogicFlags(state, state.data[0]);
                state.pc += 4;
                break;
            }

            case Operation::BranchEqual:
                if ((state.sr & kZ) != 0)
                    state.pc = instruction.address;
                else
                    state.pc += 2;
                break;
        }
    }
}

void executeLowered(const std::vector<Instruction> &program, CPUState &state)
{
    size_t index = 0;
    while (index < program.size()) {
        const Instruction &instruction = program[index++];

        switch (instruction.operation) {
            case Operation::MoveQ: {
                const int32_t immediate =
                    static_cast<int32_t>(static_cast<int8_t>(instruction.value));
                const uint32_t result = static_cast<uint32_t>(immediate);
                state.data[0] = result;
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if (immediate < 0)
                    state.sr |= kN;
                state.pc += 2;
                break;
            }

            case Operation::AddImmediate: {
                const uint32_t left = state.data[0];
                const uint32_t right = instruction.value;
                const uint32_t result = left + right;
                state.data[0] = result;

                const uint32_t sign = 0x80000000u;
                const bool carry =
                    static_cast<uint64_t>(left) + right > 0xffffffffull;
                const bool overflow =
                    ((left ^ result) & (right ^ result) & sign) != 0;

                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC | kX));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & sign) != 0)
                    state.sr |= kN;
                if (overflow)
                    state.sr |= kV;
                if (carry)
                    state.sr |= kC | kX;
                state.pc += 6;
                break;
            }

            case Operation::SubImmediate: {
                const uint32_t left = state.data[0];
                const uint32_t right = instruction.value;
                const uint32_t result = left - right;
                state.data[0] = result;

                const uint32_t sign = 0x80000000u;
                const bool borrow = left < right;
                const bool overflow =
                    ((left ^ right) & (left ^ result) & sign) != 0;

                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC | kX));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & sign) != 0)
                    state.sr |= kN;
                if (overflow)
                    state.sr |= kV;
                if (borrow)
                    state.sr |= kC | kX;
                state.pc += 6;
                break;
            }

            case Operation::AndImmediate: {
                const uint32_t result = state.data[0] & instruction.value;
                state.data[0] = result;
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & 0x80000000u) != 0)
                    state.sr |= kN;
                state.pc += 6;
                break;
            }

            case Operation::OrImmediate: {
                const uint32_t result = state.data[0] | instruction.value;
                state.data[0] = result;
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & 0x80000000u) != 0)
                    state.sr |= kN;
                state.pc += 6;
                break;
            }

            case Operation::EorImmediate: {
                const uint32_t result = state.data[0] ^ instruction.value;
                state.data[0] = result;
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & 0x80000000u) != 0)
                    state.sr |= kN;
                state.pc += 6;
                break;
            }

            case Operation::Compare: {
                const uint32_t left = state.data[0];
                const uint32_t right = instruction.value;
                const uint32_t result = left - right;
                const uint32_t sign = 0x80000000u;
                const bool borrow = left < right;
                const bool overflow =
                    ((left ^ right) & (left ^ result) & sign) != 0;

                // CMP updates N/Z/V/C but preserves X on 68000-family CPUs.
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & sign) != 0)
                    state.sr |= kN;
                if (overflow)
                    state.sr |= kV;
                if (borrow)
                    state.sr |= kC;
                state.pc += 2;
                break;
            }

            case Operation::Test: {
                const uint32_t result = state.data[0];
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & 0x80000000u) != 0)
                    state.sr |= kN;
                state.pc += 2;
                break;
            }

            case Operation::MoveLongPostIncrement: {
                const uint32_t address = state.address[0];
                const uint32_t result = readLong(state, address);
                state.data[0] = result;
                state.address[0] = address + 4;
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & 0x80000000u) != 0)
                    state.sr |= kN;
                state.pc += 2;
                break;
            }

            case Operation::MoveLongDisplacement: {
                const uint32_t address =
                    state.address[0] + static_cast<uint32_t>(
                                           instruction.displacement);
                const uint32_t result = readLong(state, address);
                state.data[0] = result;
                state.sr &= static_cast<uint16_t>(~(kN | kZ | kV | kC));
                if (result == 0)
                    state.sr |= kZ;
                if ((result & 0x80000000u) != 0)
                    state.sr |= kN;
                state.pc += 4;
                break;
            }

            case Operation::BranchEqual:
                state.pc = (state.sr & kZ) != 0
                               ? instruction.address
                               : state.pc + 2;
                break;
        }
    }
}

void compareProgram(const std::vector<Instruction> &program,
                    const CPUState &initial,
                    const char *name)
{
    CPUState reference = initial;
    CPUState lowered = initial;

    executeReference(program, reference);
    executeLowered(program, lowered);

    if (!equalState(reference, lowered)) {
        std::fprintf(stderr,
                     "SEMANTICS_68K_MISMATCH case=%s ref_d0=%08x jit_d0=%08x "
                     "ref_a0=%08x jit_a0=%08x ref_pc=%08x jit_pc=%08x "
                     "ref_sr=%04x jit_sr=%04x\n",
                     name,
                     reference.data[0],
                     lowered.data[0],
                     reference.address[0],
                     lowered.address[0],
                     reference.pc,
                     lowered.pc,
                     reference.sr,
                     lowered.sr);
        fail("reference and lowered CPU states differ");
    }
}

void testInstructionFamilies()
{
    const std::vector<Instruction> program = {
        {Operation::MoveQ, 0x7f},
        {Operation::AddImmediate, 0x00000001},
        {Operation::SubImmediate, 0x00000002},
        {Operation::AndImmediate, 0x7fffffff},
        {Operation::OrImmediate, 0x80000000},
        {Operation::EorImmediate, 0xffffffff},
        {Operation::Compare, 0x12345678},
        {Operation::Test}
    };

    CPUState initial;
    initial.data[0] = 0x00000010;
    initial.sr = kX;
    compareProgram(program, initial, "instruction-families");
}

void testFlagBoundaries()
{
    const std::array<uint32_t, 8> values = {
        0x00000000,
        0x00000001,
        0x0000007f,
        0x00000080,
        0x7fffffff,
        0x80000000,
        0xffffffff,
        0xfffffffe
    };

    for (uint32_t left : values) {
        for (uint32_t right : values) {
            CPUState initial;
            initial.data[0] = left;
            initial.sr = kX;

            compareProgram({{Operation::AddImmediate, right}},
                           initial,
                           "add-flag-boundary");
            compareProgram({{Operation::SubImmediate, right}},
                           initial,
                           "sub-flag-boundary");
            compareProgram({{Operation::Compare, right}},
                           initial,
                           "cmp-flag-boundary");
        }
    }
}

void testEffectiveAddresses()
{
    CPUState initial;
    initial.address[0] = 32;
    writeLong(initial, 32, 0x80000001);
    writeLong(initial, 40, 0x12345678);

    compareProgram({{Operation::MoveLongPostIncrement}},
                   initial,
                   "post-increment-effective-address");

    initial.address[0] = 40;
    compareProgram({{Operation::MoveLongDisplacement, 0, 0, -4}},
                   initial,
                   "displacement-effective-address");
}

void testBranches()
{
    CPUState initial;
    initial.data[0] = 0;
    initial.sr = kZ;

    compareProgram({{Operation::BranchEqual, 0, 0x0040}},
                   initial,
                   "branch-equal-taken");

    initial.sr = 0;
    compareProgram({{Operation::BranchEqual, 0, 0x0040}},
                   initial,
                   "branch-equal-not-taken");
}

void testRandomizedSequences()
{
    std::mt19937 generator(0x68bA51u);
    const std::array<Operation, 7> operations = {
        Operation::MoveQ,
        Operation::AddImmediate,
        Operation::SubImmediate,
        Operation::AndImmediate,
        Operation::OrImmediate,
        Operation::EorImmediate,
        Operation::Compare
    };

    for (size_t sequence = 0; sequence < 10000; ++sequence) {
        CPUState initial;
        initial.data[0] = generator();
        initial.sr = static_cast<uint16_t>(generator() & 0x1f);

        std::vector<Instruction> program;
        for (size_t index = 0; index < 8; ++index) {
            Instruction instruction;
            instruction.operation = operations[generator() % operations.size()];
            instruction.value = generator();
            program.push_back(instruction);
        }

        compareProgram(program, initial, "randomized-sequence");
    }
}

} // namespace

int main()
{
    std::printf("SEMANTICS_68K_BEGIN\n");

    testInstructionFamilies();
    std::printf("SEMANTICS_68K_INSTRUCTION_FAMILIES_PASS\n");

    testFlagBoundaries();
    std::printf("SEMANTICS_68K_FLAGS_PASS\n");

    testEffectiveAddresses();
    std::printf("SEMANTICS_68K_EFFECTIVE_ADDRESS_PASS\n");

    testBranches();
    std::printf("SEMANTICS_68K_BRANCH_PASS\n");

    testRandomizedSequences();
    std::printf("SEMANTICS_68K_RANDOMIZED_PASS cases=10000\n");

    std::printf("SEMANTICS_68K_PASS\n");
    return 0;
}
