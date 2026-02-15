#include "psxrecomp/ir/control_flow.h"
#include "psxrecomp/ir/optimizations.h"
#include "psxrecomp/ir/ssa.h"
#include "psxrecomp/ir/verify.h"

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

int main()
{
    using psxrecomp::Address;
    using psxrecomp::Register;
    using psxrecomp::ir::BasicBlock;
    using psxrecomp::ir::ControlFlowBuildResult;
    using psxrecomp::ir::Instruction;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Value;

    auto makeInstruction =
        [](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs, Address address)
    { return Instruction{opcode, std::move(inputs), std::move(outputs), address}; };

    Register r1 = static_cast<Register>(1);
    Register r2 = static_cast<Register>(2);
    Register r3 = static_cast<Register>(3);
    Register r4 = static_cast<Register>(4);

    std::vector<Instruction> instructions;
    instructions.push_back(makeInstruction(Opcode::ADD,
                                           {Value::makeRegister(r1), Value::makeRegister(r2)},
                                           {Value::makeRegister(r3)}, 0x1000));
    instructions.push_back(makeInstruction(
        Opcode::BRANCH, {Value::makeRegister(r1), Value::makeAddress(0x1010)}, {}, 0x1004));
    instructions.push_back(makeInstruction(Opcode::ADD,
                                           {Value::makeRegister(r3), Value::makeImmediate(1)},
                                           {Value::makeRegister(r3)}, 0x1008));
    instructions.push_back(makeInstruction(Opcode::JUMP, {Value::makeAddress(0x1014)}, {}, 0x100C));
    instructions.push_back(makeInstruction(Opcode::SUB,
                                           {Value::makeRegister(r3), Value::makeImmediate(2)},
                                           {Value::makeRegister(r3)}, 0x1010));
    instructions.push_back(makeInstruction(Opcode::MOVE, {Value::makeRegister(r3)},
                                           {Value::makeRegister(r4)}, 0x1014));
    instructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0x1018));

    ControlFlowBuildResult buildResult =
        psxrecomp::ir::buildControlFlowFunction("test", 0x1000, instructions);

    assert(buildResult.errors.empty());
    assert(buildResult.function.blocks.size() == 4);

    const BasicBlock& entry = buildResult.function.blocks[0];
    const BasicBlock& fallthrough = buildResult.function.blocks[1];
    const BasicBlock& branchTarget = buildResult.function.blocks[2];
    const BasicBlock& joinBlock = buildResult.function.blocks[3];

    assert(entry.name == "block_0x1000");
    assert(fallthrough.name == "block_0x1008");
    assert(branchTarget.name == "block_0x1010");
    assert(joinBlock.name == "block_0x1014");

    assert(entry.successors.size() == 2);
    assert(fallthrough.successors.size() == 1);
    assert(branchTarget.successors.size() == 1);
    assert(joinBlock.successors.empty());

    auto ssaResult = psxrecomp::ir::convertToSSA(buildResult.function);
    assert(ssaResult.errors.empty());

    const BasicBlock& ssaJoin = buildResult.function.blocks[3];
    assert(!ssaJoin.instructions.empty());
    assert(ssaJoin.instructions.front().opcode == Opcode::PHI);

    auto verification = psxrecomp::ir::verifyFunction(buildResult.function);
    assert(verification.success());

    std::vector<Instruction> invalidInstructions;
    invalidInstructions.push_back(
        makeInstruction(Opcode::BRANCH, {Value::makeRegister(r1)}, {}, 0x2000));
    invalidInstructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0x2004));
    ControlFlowBuildResult invalidResult =
        psxrecomp::ir::buildControlFlowFunction("invalid", 0x2000, invalidInstructions);
    assert(!invalidResult.errors.empty());

    std::vector<Instruction> delaySlotInstructions;
    delaySlotInstructions.push_back(
        makeInstruction(Opcode::COMPARE_EQ, {Value::makeRegister(r1), Value::makeRegister(r2)},
                        {Value::makeTemporary(10)}, 0x8000));
    delaySlotInstructions.push_back(makeInstruction(
        Opcode::BRANCH, {Value::makeTemporary(10), Value::makeAddress(0x8014)}, {}, 0x8008));
    delaySlotInstructions.push_back(
        makeInstruction(Opcode::ADD, {Value::makeRegister(r3), Value::makeImmediate(1)},
                        {Value::makeRegister(r3)}, 0x800C));
    delaySlotInstructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0x8010));
    delaySlotInstructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0x8014));

    ControlFlowBuildResult delaySlotCfg =
        psxrecomp::ir::buildControlFlowFunction("delay_slot", 0x8000, delaySlotInstructions);
    assert(delaySlotCfg.errors.empty());
    assert(delaySlotCfg.function.blocks.size() == 3);
    bool foundBlock8000 = false;
    bool foundBlock800c = false;
    bool foundBlock8014 = false;
    for (const auto& block : delaySlotCfg.function.blocks)
    {
        if (block.name == "block_0x8000")
        {
            foundBlock8000 = true;
            assert(block.successors.size() == 2);
        }
        if (block.name == "block_0x800c")
        {
            foundBlock800c = true;
        }
        if (block.name == "block_0x8014")
        {
            foundBlock8014 = true;
        }
    }
    assert(foundBlock8000);
    assert(foundBlock800c);
    assert(foundBlock8014);

    std::vector<Instruction> externalTargetInstructions;
    externalTargetInstructions.push_back(
        makeInstruction(Opcode::JUMP, {Value::makeAddress(0x899195B0)}, {}, 0x9000));
    externalTargetInstructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0x9004));
    ControlFlowBuildResult externalTargetCfg = psxrecomp::ir::buildControlFlowFunction(
        "external_target", 0x9000, externalTargetInstructions);
    assert(externalTargetCfg.errors.empty());
    bool foundExternalBlock = false;
    bool foundJumpBlock = false;
    for (const auto& block : externalTargetCfg.function.blocks)
    {
        if (block.name == "block_external")
        {
            foundExternalBlock = true;
        }
        if (block.name == "block_0x9000")
        {
            foundJumpBlock = true;
            assert(block.successors.size() == 1);
            assert(block.successors[0] == "block_external");
        }
    }
    assert(foundExternalBlock);
    assert(foundJumpBlock);

    std::vector<Instruction> indirectJumpInstructions;
    indirectJumpInstructions.push_back(
        makeInstruction(Opcode::JUMP, {Value::makeRegister(r1)}, {}, 0x9100));
    indirectJumpInstructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0x9104));
    ControlFlowBuildResult indirectJumpCfg =
        psxrecomp::ir::buildControlFlowFunction("indirect_jump", 0x9100, indirectJumpInstructions);
    assert(indirectJumpCfg.errors.empty());
    bool foundIndirectExternal = false;
    bool foundIndirectJumpBlock = false;
    for (const auto& block : indirectJumpCfg.function.blocks)
    {
        if (block.name == "block_external")
        {
            foundIndirectExternal = true;
        }
        if (block.name == "block_0x9100")
        {
            foundIndirectJumpBlock = true;
            assert(block.successors.size() == 1);
            assert(block.successors[0] == "block_external");
        }
    }
    assert(foundIndirectExternal);
    assert(foundIndirectJumpBlock);

    using psxrecomp::ir::Function;

    Function phiMismatch{"phi_mismatch", 0x3000, {}};
    phiMismatch.blocks.push_back(BasicBlock{"entry", {}, {"join"}, {}});
    phiMismatch.blocks.push_back(BasicBlock{"other", {}, {"join"}, {}});
    phiMismatch.blocks.push_back(BasicBlock{"join", {}, {}, {}});
    phiMismatch.blocks[2].instructions.push_back(Instruction{
        Opcode::PHI, {Value::makeTemporary(1)}, {Value::makeTemporary(2)}, std::nullopt});

    auto phiVerify = psxrecomp::ir::verifyFunction(phiMismatch);
    assert(!phiVerify.success());

    Function undefinedPhi{"undefined_phi", 0x4000, {}};
    undefinedPhi.blocks.push_back(BasicBlock{"entry", {}, {"join"}, {}});
    undefinedPhi.blocks.push_back(BasicBlock{"other", {}, {"join"}, {}});
    undefinedPhi.blocks.push_back(BasicBlock{"join", {}, {}, {}});
    undefinedPhi.blocks[2].instructions.push_back(
        Instruction{Opcode::PHI,
                    {Value::makeTemporary(99), Value::makeTemporary(98)},
                    {Value::makeTemporary(100)},
                    std::nullopt});

    auto undefinedVerify = psxrecomp::ir::verifyFunction(undefinedPhi);
    assert(!undefinedVerify.success());

    Function useBeforeDef{"use_before_def", 0x5000, {}};
    useBeforeDef.blocks.push_back(BasicBlock{"entry", {}, {}, {}});
    useBeforeDef.blocks[0].instructions.push_back(
        Instruction{Opcode::ADD, {Value::makeTemporary(7)}, {Value::makeTemporary(1)}, 0x5000});
    useBeforeDef.blocks[0].instructions.push_back(
        Instruction{Opcode::MOVE, {Value::makeTemporary(1)}, {Value::makeTemporary(7)}, 0x5004});

    auto useBeforeVerify = psxrecomp::ir::verifyFunction(useBeforeDef);
    assert(!useBeforeVerify.success());

    Function optimizations{"optimizations", 0x6000, {}};
    optimizations.blocks.push_back(BasicBlock{"entry", {}, {}, {}});
    optimizations.blocks[0].instructions.push_back(
        Instruction{Opcode::ADD,
                    {Value::makeImmediate(1), Value::makeImmediate(2)},
                    {Value::makeTemporary(1)},
                    0x6000});
    optimizations.blocks[0].instructions.push_back(
        Instruction{Opcode::MOVE, {Value::makeTemporary(1)}, {Value::makeTemporary(2)}, 0x6004});
    optimizations.blocks[0].instructions.push_back(Instruction{
        Opcode::STORE, {Value::makeAddress(0x7000), Value::makeTemporary(2)}, {}, 0x6008});
    optimizations.blocks[0].instructions.push_back(
        Instruction{Opcode::ADD,
                    {Value::makeImmediate(3), Value::makeImmediate(4)},
                    {Value::makeTemporary(3)},
                    0x600C});
    optimizations.blocks[0].instructions.push_back(Instruction{Opcode::RETURN, {}, {}, 0x6010});

    auto stats = psxrecomp::ir::runOptimizations(optimizations);
    assert(stats.constantsFolded > 0);
    assert(stats.deadInstructionsRemoved > 0);
    bool foundFolded = false;
    for (const auto& instruction : optimizations.blocks[0].instructions)
    {
        if (instruction.opcode == Opcode::MOVE && !instruction.inputs.empty() &&
            instruction.inputs.front().kind == psxrecomp::ir::ValueKind::IMMEDIATE &&
            instruction.inputs.front().immediate == 3)
        {
            foundFolded = true;
            break;
        }
    }
    assert(foundFolded);

    Function crossBlockDce{"cross_block_dce", 0x7000, {}};
    crossBlockDce.blocks.push_back(BasicBlock{"entry", {}, {"use"}, {}});
    crossBlockDce.blocks.push_back(BasicBlock{"use", {}, {}, {}});
    crossBlockDce.blocks[0].instructions.push_back(
        Instruction{Opcode::ADD,
                    {Value::makeImmediate(10), Value::makeImmediate(20)},
                    {Value::makeTemporary(1)},
                    0x7000});
    crossBlockDce.blocks[0].instructions.push_back(
        Instruction{Opcode::JUMP, {Value::makeAddress(0x7008)}, {}, 0x7004});
    crossBlockDce.blocks[1].instructions.push_back(
        Instruction{Opcode::MOVE, {Value::makeTemporary(1)}, {Value::makeTemporary(2)}, 0x7008});
    crossBlockDce.blocks[1].instructions.push_back(Instruction{Opcode::RETURN, {}, {}, 0x700C});

    psxrecomp::ir::runOptimizations(crossBlockDce);
    bool producerKept = false;
    for (const auto& instruction : crossBlockDce.blocks[0].instructions)
    {
        if (!instruction.outputs.empty() &&
            instruction.outputs.front().kind == psxrecomp::ir::ValueKind::TEMPORARY &&
            instruction.outputs.front().temporaryId == 1)
        {
            producerKept = true;
            break;
        }
    }
    assert(producerKept);

    // Test: Multiple IR instructions at the same source address that is a
    // block start should be placed in the SAME block (not split into
    // separate blocks with duplicate names).  This mirrors the pattern
    // produced by LW/SW translation: the IR emits ADD (address computation)
    // + LOAD/STORE (memory access) both at the same MIPS address.
    {
        // Simulate: branch target at 0xA000, with two IR instructions at that
        // address (ADD + LOAD), followed by a RETURN at 0xA004.
        // A preceding block ends with a BRANCH that targets 0xA000.
        std::vector<Instruction> dupAddrInstructions;
        // Block entry at 0x9FF0: compare + branch to 0xA000 (creating a
        // block start at 0xA000).
        dupAddrInstructions.push_back(makeInstruction(
            Opcode::COMPARE_EQ, {Value::makeRegister(r1), Value::makeImmediate(0)},
            {Value::makeTemporary(50)}, 0x9FF0));
        dupAddrInstructions.push_back(makeInstruction(
            Opcode::BRANCH, {Value::makeTemporary(50), Value::makeAddress(0xA000)}, {}, 0x9FF4));
        // Fallthrough block at 0x9FF8 just returns.
        dupAddrInstructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0x9FF8));
        // Block at 0xA000 with TWO instructions at the same address
        // (like LW translation: ADD then LOAD), followed by RETURN at
        // a different address that forces a new block boundary.
        dupAddrInstructions.push_back(makeInstruction(
            Opcode::ADD,
            {Value::makeRegister(r1), Value::makeImmediate(100)},
            {Value::makeTemporary(51)}, 0xA000));
        dupAddrInstructions.push_back(makeInstruction(
            Opcode::LOAD, {Value::makeTemporary(51)},
            {Value::makeRegister(r2)}, 0xA000));
        dupAddrInstructions.push_back(makeInstruction(Opcode::RETURN, {}, {}, 0xA004));

        ControlFlowBuildResult dupAddrCfg =
            psxrecomp::ir::buildControlFlowFunction("dup_addr", 0x9FF0, dupAddrInstructions);
        assert(dupAddrCfg.errors.empty());

        // The block at 0xA000 should contain BOTH the ADD and LOAD
        // (not split into two blocks with the same name).
        bool foundBlockA000 = false;
        for (const auto& block : dupAddrCfg.function.blocks)
        {
            if (block.name == "block_0xa000")
            {
                foundBlockA000 = true;
                // Must contain at least 2 instructions starting with ADD + LOAD.
                assert(block.instructions.size() >= 2);
                assert(block.instructions[0].opcode == Opcode::ADD);
                assert(block.instructions[1].opcode == Opcode::LOAD);
                // Successor should NOT be block_external.
                bool hasExternalSuccessor = false;
                for (const auto& successor : block.successors)
                {
                    if (successor == "block_external")
                    {
                        hasExternalSuccessor = true;
                    }
                }
                assert(!hasExternalSuccessor);
                break;
            }
        }
        assert(foundBlockA000);

        // There should be NO block_external block at all.
        bool hasExternalBlock = false;
        for (const auto& block : dupAddrCfg.function.blocks)
        {
            if (block.name == "block_external")
            {
                hasExternalBlock = true;
            }
        }
        assert(!hasExternalBlock);

        // Should not have any duplicate block names.
        std::unordered_set<std::string> blockNamesSet;
        for (const auto& block : dupAddrCfg.function.blocks)
        {
            assert(blockNamesSet.count(block.name) == 0);
            blockNamesSet.insert(block.name);
        }

        std::cerr << "[PASS] Duplicate-address block merging\n";
    }

    return 0;
}
