#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

enum class Opcode {
    Increment,
    Decrement,
    Jump,
    Halt
};

struct Instruction {
    Opcode opcode;
    uint32_t target = 0;
};

struct Machine {
    int32_t accumulator = 0;
    uint32_t pc = 0;
    bool halted = false;
    uint64_t executedInstructions = 0;
};

struct ExecutionStats {
    uint64_t dispatcherEntries = 0;
    uint64_t directChains = 0;
    uint64_t samePageChains = 0;
    uint64_t crossPageFallbacks = 0;
    uint64_t compiledBlocks = 0;
};

struct TranslationBlock {
    uint32_t start = 0;
    uint32_t end = 0;
    uint64_t generation = 0;
    bool valid = true;
    std::vector<Instruction> instructions;
};

class TranslationCache {
public:
    explicit TranslationCache(size_t capacity)
        : capacity_(capacity)
    {
    }

    void setCompilationFailures(size_t count)
    {
        compilationFailuresRemaining_ = count;
    }

    TranslationBlock *lookup(uint32_t guestPC)
    {
        auto iterator = blocks_.find(guestPC);
        if (iterator == blocks_.end() || !iterator->second->valid)
            return nullptr;
        return iterator->second.get();
    }

    TranslationBlock *compile(const std::vector<Instruction> &program,
                              uint32_t guestPC,
                              ExecutionStats &stats)
    {
        if (compilationFailuresRemaining_ != 0) {
            --compilationFailuresRemaining_;
            return nullptr;
        }

        if (guestPC >= program.size())
            return nullptr;

        auto block = std::make_unique<TranslationBlock>();
        block->start = guestPC;
        block->generation = ++generation_;
        block->instructions.reserve(program.size() - guestPC);

        for (uint32_t index = guestPC; index < program.size(); ++index) {
            block->instructions.push_back(program[index]);
            block->end = index + 1;

            if (program[index].opcode == Opcode::Jump ||
                program[index].opcode == Opcode::Halt) {
                break;
            }
        }

        if (blocks_.size() >= capacity_)
            evictOldest();

        TranslationBlock *result = block.get();
        blocks_[guestPC] = std::move(block);
        insertionOrder_.push_back(guestPC);
        ++stats.compiledBlocks;
        return result;
    }

    void invalidate(uint32_t guestPC)
    {
        auto iterator = blocks_.find(guestPC);
        if (iterator != blocks_.end())
            iterator->second->valid = false;
    }

    size_t liveBlockCount() const
    {
        size_t count = 0;
        for (const auto &entry : blocks_) {
            if (entry.second->valid)
                ++count;
        }
        return count;
    }

private:
    void evictOldest()
    {
        while (!insertionOrder_.empty()) {
            const uint32_t guestPC = insertionOrder_.front();
            insertionOrder_.pop_front();

            auto iterator = blocks_.find(guestPC);
            if (iterator == blocks_.end())
                continue;

            blocks_.erase(iterator);
            return;
        }
    }

    size_t capacity_;
    size_t compilationFailuresRemaining_ = 0;
    uint64_t generation_ = 0;
    std::unordered_map<uint32_t, std::unique_ptr<TranslationBlock>> blocks_;
    std::deque<uint32_t> insertionOrder_;
};

class ToyJIT {
public:
    explicit ToyJIT(size_t cacheCapacity)
        : cache_(cacheCapacity)
    {
    }

    TranslationBlock *compile(const std::vector<Instruction> &program,
                              uint32_t guestPC)
    {
        if (auto *existing = cache_.lookup(guestPC))
            return existing;

        return cache_.compile(program, guestPC, stats_);
    }

    void setCompilationFailures(size_t count)
    {
        cache_.setCompilationFailures(count);
    }

    void invalidate(uint32_t guestPC)
    {
        cache_.invalidate(guestPC);
    }

    const ExecutionStats &stats() const
    {
        return stats_;
    }

    size_t liveBlockCount() const
    {
        return cache_.liveBlockCount();
    }

    bool execute(const std::vector<Instruction> &program,
                 Machine &machine,
                 uint64_t instructionLimit,
                 uint32_t pageSize = 4)
    {
        bool enterDispatcher = true;

        while (!machine.halted &&
               machine.executedInstructions < instructionLimit) {
            if (enterDispatcher)
                ++stats_.dispatcherEntries;

            TranslationBlock *block = cache_.lookup(machine.pc);
            if (block == nullptr) {
                block = compile(program, machine.pc);
                if (block == nullptr)
                    return false;
            }

            enterDispatcher = true;

            for (const Instruction &instruction : block->instructions) {
                ++machine.executedInstructions;

                switch (instruction.opcode) {
                    case Opcode::Increment:
                        ++machine.accumulator;
                        ++machine.pc;
                        break;

                    case Opcode::Decrement:
                        --machine.accumulator;
                        ++machine.pc;
                        break;

                    case Opcode::Jump: {
                        const uint32_t target = instruction.target;
                        const bool samePage =
                            (block->start / pageSize) == (target / pageSize);
                        TranslationBlock *targetBlock = cache_.lookup(target);

                        machine.pc = target;

                        if (targetBlock != nullptr && samePage) {
                            ++stats_.directChains;
                            ++stats_.samePageChains;
                            enterDispatcher = false;
                        } else if (targetBlock != nullptr) {
                            ++stats_.crossPageFallbacks;
                        }

                        goto blockComplete;
                    }

                    case Opcode::Halt:
                        machine.halted = true;
                        machine.pc = block->end;
                        goto blockComplete;
                }
            }

        blockComplete:
            if (!enterDispatcher)
                continue;
        }

        return machine.halted ||
               machine.executedInstructions >= instructionLimit;
    }

private:
    TranslationCache cache_;
    ExecutionStats stats_;
};

void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "ARCHITECTURE_JIT_FAIL: %s\n", message);
        std::fflush(stderr);
        std::exit(1);
    }
}

std::vector<Instruction> makeLoopProgram()
{
    return {
        {Opcode::Increment},
        {Opcode::Decrement},
        {Opcode::Jump, 0},
        {Opcode::Halt}
    };
}

void testBlockReuse()
{
    ToyJIT jit(8);
    const auto program = makeLoopProgram();
    ExecutionStats unusedStats;

    check(jit.compile(program, 0) != nullptr,
          "initial block compilation failed");
    check(jit.compile(program, 0) != nullptr,
          "second block lookup failed");
    check(jit.stats().compiledBlocks == 1,
          "same guest PC compiled more than once");
    check(jit.liveBlockCount() == 1,
          "block lookup produced the wrong live-block count");
    (void)unusedStats;
}

void testDirectChaining()
{
    ToyJIT jit(8);
    const auto program = makeLoopProgram();
    Machine machine;

    check(jit.compile(program, 0) != nullptr,
          "loop block precompilation failed");
    check(jit.execute(program, machine, 1000),
          "direct-chain loop did not execute");
    check(machine.executedInstructions >= 1000 &&
              machine.executedInstructions <= 1002,
          "direct-chain loop exceeded its block-boundary limit");
    check(jit.stats().dispatcherEntries == 1,
          "direct chaining still re-entered the dispatcher");
    check(jit.stats().directChains > 1,
          "direct chaining was never exercised");
    check(jit.stats().samePageChains == jit.stats().directChains,
          "same-page chain accounting is inconsistent");
}

void testCrossPageFallback()
{
    ToyJIT jit(8);
    const std::vector<Instruction> program = {
        {Opcode::Increment},
        {Opcode::Jump, 4},
        {Opcode::Halt},
        {Opcode::Halt},
        {Opcode::Increment},
        {Opcode::Jump, 0}
    };
    Machine machine;

    check(jit.compile(program, 0) != nullptr,
          "cross-page source block compilation failed");
    check(jit.compile(program, 4) != nullptr,
          "cross-page target block compilation failed");
    check(jit.execute(program, machine, 100),
          "cross-page fallback loop did not execute");
    check(jit.stats().crossPageFallbacks > 0,
          "cross-page jump did not use the fallback path");
    check(jit.stats().dispatcherEntries > 1,
          "cross-page fallback did not re-enter the dispatcher");
}

void testInvalidation()
{
    ToyJIT jit(8);
    auto program = std::vector<Instruction>{
        {Opcode::Increment},
        {Opcode::Halt}
    };

    check(jit.compile(program, 0) != nullptr,
          "invalidation setup compilation failed");
    jit.invalidate(0);

    check(jit.compile(program, 0) != nullptr,
          "invalidated block was not recompiled");
    check(jit.stats().compiledBlocks == 2,
          "invalidated block reused stale translation");

    Machine machine;
    check(jit.execute(program, machine, 2),
          "recompiled block did not execute");
    check(machine.accumulator == 1,
          "recompiled block produced the wrong result");
}

void testEviction()
{
    ToyJIT jit(2);
    const std::vector<Instruction> program = {
        {Opcode::Increment},
        {Opcode::Halt},
        {Opcode::Decrement},
        {Opcode::Halt},
        {Opcode::Increment},
        {Opcode::Halt}
    };

    check(jit.compile(program, 0) != nullptr,
          "first eviction block compilation failed");
    check(jit.compile(program, 2) != nullptr,
          "second eviction block compilation failed");
    check(jit.compile(program, 4) != nullptr,
          "third eviction block compilation failed");
    check(jit.liveBlockCount() == 2,
          "cache exceeded its configured capacity");

    check(jit.compile(program, 0) != nullptr,
          "evicted block was not safely recompiled");
    check(jit.stats().compiledBlocks == 4,
          "cache lookup incorrectly returned an evicted block");
}

void testCompilationFailure()
{
    ToyJIT jit(4);
    const auto program = makeLoopProgram();
    Machine machine;

    jit.setCompilationFailures(1);
    check(!jit.execute(program, machine, 10),
          "compilation failure was not reported");
    check(machine.executedInstructions == 0,
          "failed compilation partially executed guest code");
    check(jit.liveBlockCount() == 0,
          "failed compilation left a partially published block");
}

} // namespace

int main()
{
    std::printf("ARCHITECTURE_JIT_BEGIN\n");

    testBlockReuse();
    std::printf("ARCHITECTURE_JIT_BLOCK_REUSE_PASS\n");

    testDirectChaining();
    std::printf("ARCHITECTURE_JIT_CHAIN_PASS\n");

    testCrossPageFallback();
    std::printf("ARCHITECTURE_JIT_CROSS_PAGE_PASS\n");

    testInvalidation();
    std::printf("ARCHITECTURE_JIT_INVALIDATION_PASS\n");

    testEviction();
    std::printf("ARCHITECTURE_JIT_EVICTION_PASS\n");

    testCompilationFailure();
    std::printf("ARCHITECTURE_JIT_FAILURE_HANDLING_PASS\n");

    std::printf("ARCHITECTURE_JIT_PASS\n");
    return 0;
}
