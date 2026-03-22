#include "psxrecomp/disasm/instruction.h"
#include "psxrecomp/ir/control_flow.h"
#include "psxrecomp/ir/ir.h"
#include "psxrecomp/ir/mips_ir_builder.h"

#include <algorithm>
#include <cassert>

int main()
{
    using namespace psxrecomp::Registers;
    using psxrecomp::disasm::Instruction;
    using psxrecomp::disasm::InstructionType;
    using psxrecomp::disasm::Opcode;

    auto makeDelayNop = [](psxrecomp::Address address, psxrecomp::Address owner)
    {
        Instruction nop{};
        nop.address = address;
        nop.opcode = Opcode::SLL;
        nop.type = InstructionType::R_TYPE;
        nop.rd = ZERO;
        nop.rt = ZERO;
        nop.shamt = 0;
        nop.isInDelaySlot = true;
        nop.delaySlotOwner = owner;
        return nop;
    };

    Instruction jal{};
    jal.address = 0x80010000;
    jal.opcode = Opcode::JAL;
    jal.type = InstructionType::J_TYPE;
    jal.target = 0x00002000;

    Instruction jr{};
    jr.address = 0x80010008;
    jr.opcode = Opcode::JR;
    jr.type = InstructionType::R_TYPE;
    jr.rs = T0;

    Instruction jalr{};
    jalr.address = 0x80010010;
    jalr.opcode = Opcode::JALR;
    jalr.type = InstructionType::R_TYPE;
    jalr.rs = T1;
    jalr.rd = S0;

    auto result = psxrecomp::ir::buildIrFromMips({jal, makeDelayNop(0x80010004, 0x80010000), jr,
                                                  makeDelayNop(0x8001000C, 0x80010008), jalr,
                                                  makeDelayNop(0x80010014, 0x80010010)});

    assert(result.errors.empty());
    assert(result.warnings.empty());

    [[maybe_unused]] const auto hasInstruction =
        [&](psxrecomp::ir::Opcode opcode, psxrecomp::ir::Value input, psxrecomp::ir::Value output)
    {
        return std::any_of(result.instructions.begin(), result.instructions.end(),
                           [&](const auto& instr)
                           {
                               if (instr.opcode != opcode)
                               {
                                   return false;
                               }
                               if (input.kind != psxrecomp::ir::ValueKind::INVALID)
                               {
                                   if (instr.inputs.empty() || !(instr.inputs.front() == input))
                                   {
                                       return false;
                                   }
                               }
                               if (output.kind != psxrecomp::ir::ValueKind::INVALID)
                               {
                                   if (instr.outputs.empty() || !(instr.outputs.front() == output))
                                   {
                                       return false;
                                   }
                               }
                               return true;
                           });
    };

    assert(hasInstruction(psxrecomp::ir::Opcode::MOVE,
                          psxrecomp::ir::Value::makeImmediate(0x80010008),
                          psxrecomp::ir::Value::makeRegister(RA)));
    assert(hasInstruction(psxrecomp::ir::Opcode::MOVE,
                          psxrecomp::ir::Value::makeImmediate(0x80010018),
                          psxrecomp::ir::Value::makeRegister(S0)));

    auto jrCaptureIt = std::find_if(result.instructions.begin(), result.instructions.end(),
                                    [&](const auto& instruction)
                                    {
                                        return instruction.opcode == psxrecomp::ir::Opcode::MOVE &&
                                               instruction.inputs.size() == 1 &&
                                               instruction.inputs.front() ==
                                                   psxrecomp::ir::Value::makeRegister(T0) &&
                                               instruction.outputs.size() == 1 &&
                                               instruction.outputs.front().kind ==
                                                   psxrecomp::ir::ValueKind::TEMPORARY;
                                    });
    auto jrDelayIt = std::find_if(result.instructions.begin(), result.instructions.end(),
                                  [&](const auto& instruction)
                                  {
                                      return instruction.opcode == psxrecomp::ir::Opcode::NOP &&
                                             instruction.sourceAsmAddress.value_or(0) ==
                                                 0x8001000C;
                                  });
    auto jrJumpIt = std::find_if(result.instructions.begin(), result.instructions.end(),
                                 [&](const auto& instruction)
                                 {
                                     return instruction.opcode == psxrecomp::ir::Opcode::JUMP &&
                                            instruction.inputs.size() == 1 &&
                                            jrCaptureIt != result.instructions.end() &&
                                            instruction.inputs.front() ==
                                                jrCaptureIt->outputs.front();
                                 });

    assert(jrCaptureIt != result.instructions.end());
    assert(jrDelayIt != result.instructions.end());
    assert(jrJumpIt != result.instructions.end());
    assert(jrCaptureIt < jrDelayIt);
    assert(jrDelayIt < jrJumpIt);

    auto jalrLinkIt = std::find_if(result.instructions.begin(), result.instructions.end(),
                                   [&](const auto& instruction)
                                   {
                                       return instruction.opcode == psxrecomp::ir::Opcode::MOVE &&
                                              !instruction.outputs.empty() &&
                                              instruction.outputs.front() ==
                                                  psxrecomp::ir::Value::makeRegister(S0);
                                   });
    auto jalrDelayIt =
        std::find_if(result.instructions.begin(), result.instructions.end(),
                     [&](const auto& instruction)
                     {
                         return instruction.opcode == psxrecomp::ir::Opcode::NOP &&
                                instruction.sourceAsmAddress.value_or(0) == 0x80010014;
                     });
    auto jalrCaptureIt = std::find_if(result.instructions.begin(), result.instructions.end(),
                                      [&](const auto& instruction)
                                      {
                                          return instruction.opcode ==
                                                     psxrecomp::ir::Opcode::MOVE &&
                                                 instruction.inputs.size() == 1 &&
                                                 instruction.inputs.front() ==
                                                     psxrecomp::ir::Value::makeRegister(T1) &&
                                                 instruction.outputs.size() == 1 &&
                                                 instruction.outputs.front().kind ==
                                                     psxrecomp::ir::ValueKind::TEMPORARY;
                                      });
    auto jalrCallIt = std::find_if(result.instructions.begin(), result.instructions.end(),
                                   [&](const auto& instruction)
                                   {
                                       return instruction.opcode == psxrecomp::ir::Opcode::CALL &&
                                              instruction.inputs.size() == 1 &&
                                              jalrCaptureIt != result.instructions.end() &&
                                              instruction.inputs.front() ==
                                                  jalrCaptureIt->outputs.front();
                                   });
    assert(jalrLinkIt != result.instructions.end());
    assert(jalrDelayIt != result.instructions.end());
    assert(jalrCaptureIt != result.instructions.end());
    assert(jalrCallIt != result.instructions.end());
    assert(jalrCaptureIt < jalrLinkIt);
    assert(jalrLinkIt < jalrDelayIt);
    assert(jalrDelayIt < jalrCallIt);

    Instruction jalrSelf{};
    jalrSelf.address = 0x80010120;
    jalrSelf.opcode = Opcode::JALR;
    jalrSelf.type = InstructionType::R_TYPE;
    jalrSelf.rs = RA;
    jalrSelf.rd = RA;

    auto jalrSelfResult =
        psxrecomp::ir::buildIrFromMips({jalrSelf, makeDelayNop(0x80010124, 0x80010120)});
    assert(jalrSelfResult.errors.empty());

    auto jalrSelfCaptureIt =
        std::find_if(jalrSelfResult.instructions.begin(), jalrSelfResult.instructions.end(),
                     [&](const auto& instruction)
                     {
                         return instruction.opcode == psxrecomp::ir::Opcode::MOVE &&
                                instruction.inputs.size() == 1 &&
                                instruction.inputs.front() ==
                                    psxrecomp::ir::Value::makeRegister(RA) &&
                                instruction.outputs.size() == 1 &&
                                instruction.outputs.front().kind ==
                                    psxrecomp::ir::ValueKind::TEMPORARY;
                     });
    auto jalrSelfLinkIt =
        std::find_if(jalrSelfResult.instructions.begin(), jalrSelfResult.instructions.end(),
                     [&](const auto& instruction)
                     {
                         return instruction.opcode == psxrecomp::ir::Opcode::MOVE &&
                                instruction.inputs.size() == 1 &&
                                instruction.inputs.front() ==
                                    psxrecomp::ir::Value::makeImmediate(0x80010128) &&
                                instruction.outputs.size() == 1 &&
                                instruction.outputs.front() ==
                                    psxrecomp::ir::Value::makeRegister(RA);
                     });
    auto jalrSelfDelayIt =
        std::find_if(jalrSelfResult.instructions.begin(), jalrSelfResult.instructions.end(),
                     [&](const auto& instruction)
                     {
                         return instruction.opcode == psxrecomp::ir::Opcode::NOP &&
                                instruction.sourceAsmAddress.value_or(0) == 0x80010124;
                     });
    auto jalrSelfCallIt =
        std::find_if(jalrSelfResult.instructions.begin(), jalrSelfResult.instructions.end(),
                     [&](const auto& instruction)
                     {
                         return instruction.opcode == psxrecomp::ir::Opcode::CALL &&
                                instruction.inputs.size() == 1 &&
                                jalrSelfCaptureIt != jalrSelfResult.instructions.end() &&
                                instruction.inputs.front() ==
                                    jalrSelfCaptureIt->outputs.front();
                     });
    assert(jalrSelfCaptureIt != jalrSelfResult.instructions.end());
    assert(jalrSelfLinkIt != jalrSelfResult.instructions.end());
    assert(jalrSelfDelayIt != jalrSelfResult.instructions.end());
    assert(jalrSelfCallIt != jalrSelfResult.instructions.end());
    assert(jalrSelfCaptureIt < jalrSelfLinkIt);
    assert(jalrSelfLinkIt < jalrSelfDelayIt);
    assert(jalrSelfDelayIt < jalrSelfCallIt);

    Instruction bgezal{};
    bgezal.address = 0x80010100;
    bgezal.opcode = Opcode::BGEZAL;
    bgezal.type = InstructionType::I_TYPE;
    bgezal.rs = A0;
    bgezal.rt = RA;
    bgezal.immediate = 1;

    auto linkBranchResult =
        psxrecomp::ir::buildIrFromMips({bgezal, makeDelayNop(0x80010104, 0x80010100)});
    assert(linkBranchResult.errors.empty());
    auto branchLinkIt = std::find_if(
        linkBranchResult.instructions.begin(), linkBranchResult.instructions.end(),
        [&](const auto& instruction)
        {
            return instruction.opcode == psxrecomp::ir::Opcode::MOVE &&
                   !instruction.outputs.empty() &&
                   instruction.outputs.front() == psxrecomp::ir::Value::makeRegister(RA);
        });
    auto branchDelayIt =
        std::find_if(linkBranchResult.instructions.begin(), linkBranchResult.instructions.end(),
                     [&](const auto& instruction)
                     {
                         return instruction.opcode == psxrecomp::ir::Opcode::NOP &&
                                instruction.sourceAsmAddress.value_or(0) == 0x80010104;
                     });
    assert(branchLinkIt != linkBranchResult.instructions.end());
    assert(branchDelayIt != linkBranchResult.instructions.end());
    assert(branchLinkIt < branchDelayIt);

    // Regression: if a JR delay-slot instruction is also a branch target,
    // keep both contexts:
    // 1) delay-context copy attached to the JR block
    // 2) normal-context copy at its own address
    Instruction branch{};
    branch.address = 0x80020000;
    branch.opcode = Opcode::BEQ;
    branch.type = InstructionType::I_TYPE;
    branch.rs = ZERO;
    branch.rt = ZERO;
    branch.immediate = 2; // target 0x8002000C

    Instruction ret{};
    ret.address = 0x80020008;
    ret.opcode = Opcode::JR;
    ret.type = InstructionType::R_TYPE;
    ret.rs = RA;

    Instruction delayTarget{};
    delayTarget.address = 0x8002000C;
    delayTarget.opcode = Opcode::OR;
    delayTarget.type = InstructionType::R_TYPE;
    delayTarget.rd = V0;
    delayTarget.rs = ZERO;
    delayTarget.rt = ZERO;
    delayTarget.isInDelaySlot = true;
    delayTarget.delaySlotOwner = 0x80020008;

    Instruction afterDelay{};
    afterDelay.address = 0x80020010;
    afterDelay.opcode = Opcode::ADDIU;
    afterDelay.type = InstructionType::I_TYPE;
    afterDelay.rs = ZERO;
    afterDelay.rt = V0;
    afterDelay.immediate = 1;

    auto dualContext = psxrecomp::ir::buildIrFromMips(
        {branch, makeDelayNop(0x80020004, 0x80020000), ret, delayTarget, afterDelay});
    assert(dualContext.errors.empty());

    size_t delayCopies = 0;
    [[maybe_unused]] bool hasReturnContextDelayCopy = false;
    [[maybe_unused]] bool hasNormalContextDelayCopy = false;
    for (const auto& instruction : dualContext.instructions)
    {
        if (instruction.opcode == psxrecomp::ir::Opcode::OR && !instruction.outputs.empty() &&
            instruction.outputs.front() == psxrecomp::ir::Value::makeRegister(V0))
        {
            ++delayCopies;
            if (instruction.sourceAddress.value_or(0) == 0x80020008)
            {
                hasReturnContextDelayCopy = true;
            }
            if (instruction.sourceAddress.value_or(0) == 0x8002000C)
            {
                hasNormalContextDelayCopy = true;
            }
        }
    }
    assert(delayCopies >= 2);
    assert(hasReturnContextDelayCopy);
    assert(hasNormalContextDelayCopy);

    auto cfg = psxrecomp::ir::buildControlFlowFunction("delay_target", 0x80020000,
                                                       dualContext.instructions);
    assert(cfg.errors.empty());

    const auto findBlock = [&](const char* name) -> const psxrecomp::ir::BasicBlock*
    {
        auto it = std::find_if(cfg.function.blocks.begin(), cfg.function.blocks.end(),
                               [&](const psxrecomp::ir::BasicBlock& block)
                               { return block.name == name; });
        if (it == cfg.function.blocks.end())
        {
            return nullptr;
        }
        return &(*it);
    };

    [[maybe_unused]] const auto* returnBlock = findBlock("block_0x80020008");
    assert(returnBlock != nullptr);
    assert(returnBlock->instructions.size() >= 2);
    assert(returnBlock->instructions[0].opcode == psxrecomp::ir::Opcode::OR);
    assert(returnBlock->instructions[1].opcode == psxrecomp::ir::Opcode::RETURN);

    const auto* delayBlock = findBlock("block_0x8002000c");
    assert(delayBlock != nullptr);
    [[maybe_unused]] bool reachesAfterDelay = false;
    for (const auto& instruction : delayBlock->instructions)
    {
        if (instruction.sourceAddress.value_or(0) == 0x80020010)
        {
            reachesAfterDelay = true;
            break;
        }
    }
    assert(reachesAfterDelay);
    assert(std::find(delayBlock->successors.begin(), delayBlock->successors.end(),
                     "block_0x80020008") == delayBlock->successors.end());

    return 0;
}
