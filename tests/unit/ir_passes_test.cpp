#include "psxrecomp/ir/control_flow.h"
#include "psxrecomp/ir/ssa.h"
#include "psxrecomp/ir/verify.h"

#include <cassert>
#include <string>
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

    auto makeInstruction = [](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs,
                              Address address) {
        return Instruction{opcode, std::move(inputs), std::move(outputs), address};
    };

    Register r1 = static_cast<Register>(1);
    Register r2 = static_cast<Register>(2);
    Register r3 = static_cast<Register>(3);
    Register r4 = static_cast<Register>(4);

    std::vector<Instruction> instructions;
    instructions.push_back(makeInstruction(Opcode::ADD, {Value::makeRegister(r1), Value::makeRegister(r2)},
                                           {Value::makeRegister(r3)}, 0x1000));
    instructions.push_back(makeInstruction(Opcode::BRANCH,
                                           {Value::makeRegister(r1), Value::makeAddress(0x1010)}, {},
                                           0x1004));
    instructions.push_back(makeInstruction(Opcode::ADD, {Value::makeRegister(r3), Value::makeImmediate(1)},
                                           {Value::makeRegister(r3)}, 0x1008));
    instructions.push_back(
        makeInstruction(Opcode::JUMP, {Value::makeAddress(0x1014)}, {}, 0x100C));
    instructions.push_back(makeInstruction(Opcode::SUB, {Value::makeRegister(r3), Value::makeImmediate(2)},
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

    return 0;
}
