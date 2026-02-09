#include "psxrecomp/ir/mips_ir_builder.h"

#include <sstream>

namespace psxrecomp
{
namespace ir
{

namespace
{
bool isMipsNop(const disasm::Instruction& instruction)
{
    return instruction.opcode == disasm::Opcode::SLL && instruction.rd == Registers::ZERO &&
           instruction.rt == Registers::ZERO && instruction.shamt == 0;
}

std::string formatAddress(Address address)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << address;
    return stream.str();
}

void addWarning(std::vector<std::string>& warnings, const disasm::Instruction& instruction,
                const std::string& message)
{
    std::ostringstream stream;
    stream << message << " @ " << formatAddress(instruction.address);
    warnings.push_back(stream.str());
}

void addError(std::vector<std::string>& errors, const disasm::Instruction& instruction,
              const std::string& message)
{
    std::ostringstream stream;
    stream << message << " @ " << formatAddress(instruction.address);
    errors.push_back(stream.str());
}
} // namespace

MipsIrBuildResult buildIrFromMips(const std::vector<disasm::Instruction>& instructions,
                                  const MipsIrBuildOptions& options)
{
    MipsIrBuildResult result;
    Program program;
    Builder builder(program);

    const auto registerValue = [](Register reg) { return Value::makeRegister(reg); };

    for (size_t index = 0; index < instructions.size(); ++index)
    {
        if (index > 0 && instructions[index].isInDelaySlot &&
            instructions[index - 1].hasDelaySlot())
        {
            continue;
        }

        const auto& instruction = instructions[index];
        const auto address = instruction.address;
        auto emit = [&](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs)
        {
            result.instructions.push_back(
                builder.makeInstruction(opcode, std::move(inputs), std::move(outputs), address));
        };

        auto delaySlotIsNop = [&]() -> bool
        {
            if (!instruction.hasDelaySlot())
            {
                return true;
            }
            if (index + 1 >= instructions.size())
            {
                addError(result.errors, instruction, "Missing delay-slot instruction");
                return false;
            }
            const auto& delaySlot = instructions[index + 1];
            if (!delaySlot.isInDelaySlot || delaySlot.delaySlotOwner != instruction.address)
            {
                addError(result.errors, instruction, "Delay-slot metadata mismatch");
                return false;
            }
            if (!isMipsNop(delaySlot))
            {
                addError(result.errors, instruction,
                         "Delay-slot semantics not modeled; only nop delay slots are supported");
                return false;
            }
            return true;
        };

        if (isMipsNop(instruction))
        {
            emit(Opcode::NOP, {}, {});
            continue;
        }

        switch (instruction.opcode)
        {
        case disasm::Opcode::ADD:
        case disasm::Opcode::ADDU:
            emit(Opcode::ADD, {registerValue(instruction.rs), registerValue(instruction.rt)},
                 {registerValue(instruction.rd)});
            break;
        case disasm::Opcode::SUB:
        case disasm::Opcode::SUBU:
            emit(Opcode::SUB, {registerValue(instruction.rs), registerValue(instruction.rt)},
                 {registerValue(instruction.rd)});
            break;
        case disasm::Opcode::AND:
            emit(Opcode::AND, {registerValue(instruction.rs), registerValue(instruction.rt)},
                 {registerValue(instruction.rd)});
            break;
        case disasm::Opcode::OR:
            emit(Opcode::OR, {registerValue(instruction.rs), registerValue(instruction.rt)},
                 {registerValue(instruction.rd)});
            break;
        case disasm::Opcode::XOR:
            emit(Opcode::XOR, {registerValue(instruction.rs), registerValue(instruction.rt)},
                 {registerValue(instruction.rd)});
            break;
        case disasm::Opcode::ADDI:
        case disasm::Opcode::ADDIU:
            emit(Opcode::ADD,
                 {registerValue(instruction.rs),
                  Value::makeImmediate(static_cast<s32>(instruction.immediate))},
                 {registerValue(instruction.rt)});
            break;
        case disasm::Opcode::ANDI:
            emit(Opcode::AND,
                 {registerValue(instruction.rs),
                  Value::makeImmediate(static_cast<u16>(instruction.immediate))},
                 {registerValue(instruction.rt)});
            break;
        case disasm::Opcode::ORI:
            emit(Opcode::OR,
                 {registerValue(instruction.rs),
                  Value::makeImmediate(static_cast<u16>(instruction.immediate))},
                 {registerValue(instruction.rt)});
            break;
        case disasm::Opcode::XORI:
            emit(Opcode::XOR,
                 {registerValue(instruction.rs),
                  Value::makeImmediate(static_cast<u16>(instruction.immediate))},
                 {registerValue(instruction.rt)});
            break;
        case disasm::Opcode::LUI:
            emit(Opcode::MOVE,
                 {Value::makeImmediate(static_cast<s32>(static_cast<u16>(instruction.immediate))
                                       << 16)},
                 {registerValue(instruction.rt)});
            break;
        case disasm::Opcode::LW:
        {
            Value addressTemp = builder.createTemporary();
            emit(Opcode::ADD,
                 {registerValue(instruction.rs),
                  Value::makeImmediate(static_cast<s32>(instruction.immediate))},
                 {addressTemp});
            emit(Opcode::LOAD, {addressTemp}, {registerValue(instruction.rt)});
            break;
        }
        case disasm::Opcode::SW:
        {
            Value addressTemp = builder.createTemporary();
            emit(Opcode::ADD,
                 {registerValue(instruction.rs),
                  Value::makeImmediate(static_cast<s32>(instruction.immediate))},
                 {addressTemp});
            emit(Opcode::STORE, {addressTemp, registerValue(instruction.rt)}, {});
            break;
        }
        case disasm::Opcode::BEQ:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            Value condTemp = builder.createTemporary();
            emit(Opcode::COMPARE_EQ, {registerValue(instruction.rs), registerValue(instruction.rt)},
                 {condTemp});
            auto target = instruction.getBranchTarget();
            if (target.has_value())
            {
                emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "BEQ missing branch target");
            }
            break;
        }
        case disasm::Opcode::BNE:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            Value condTemp = builder.createTemporary();
            emit(Opcode::COMPARE_NE, {registerValue(instruction.rs), registerValue(instruction.rt)},
                 {condTemp});
            auto target = instruction.getBranchTarget();
            if (target.has_value())
            {
                emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "BNE missing branch target");
            }
            break;
        }
        case disasm::Opcode::BLEZ:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            Value condTemp = builder.createTemporary();
            emit(Opcode::COMPARE_LE, {registerValue(instruction.rs), Value::makeImmediate(0)},
                 {condTemp});
            auto target = instruction.getBranchTarget();
            if (target.has_value())
            {
                emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "BLEZ missing branch target");
            }
            break;
        }
        case disasm::Opcode::BGTZ:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            Value condTemp = builder.createTemporary();
            emit(Opcode::COMPARE_GT, {registerValue(instruction.rs), Value::makeImmediate(0)},
                 {condTemp});
            auto target = instruction.getBranchTarget();
            if (target.has_value())
            {
                emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "BGTZ missing branch target");
            }
            break;
        }
        case disasm::Opcode::BLTZ:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            Value condTemp = builder.createTemporary();
            emit(Opcode::COMPARE_LT, {registerValue(instruction.rs), Value::makeImmediate(0)},
                 {condTemp});
            auto target = instruction.getBranchTarget();
            if (target.has_value())
            {
                emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "BLTZ missing branch target");
            }
            break;
        }
        case disasm::Opcode::BGEZ:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            Value condTemp = builder.createTemporary();
            emit(Opcode::COMPARE_GE, {registerValue(instruction.rs), Value::makeImmediate(0)},
                 {condTemp});
            auto target = instruction.getBranchTarget();
            if (target.has_value())
            {
                emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "BGEZ missing branch target");
            }
            break;
        }
        case disasm::Opcode::J:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            auto target = instruction.getJumpTarget();
            if (target.has_value())
            {
                emit(Opcode::JUMP, {Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "J missing jump target");
            }
            break;
        }
        case disasm::Opcode::JAL:
        {
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            auto target = instruction.getJumpTarget();
            if (target.has_value())
            {
                emit(Opcode::CALL, {Value::makeAddress(*target)}, {});
            }
            else
            {
                addError(result.errors, instruction, "JAL missing call target");
            }
            break;
        }
        case disasm::Opcode::JR:
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            if (instruction.isReturn())
            {
                emit(Opcode::RETURN, {}, {});
            }
            else
            {
                addWarning(result.warnings, instruction, "Indirect JR unsupported");
                emit(Opcode::RETURN, {}, {});
            }
            break;
        case disasm::Opcode::JALR:
            if (!delaySlotIsNop())
            {
                emit(Opcode::NOP, {}, {});
                break;
            }
            addWarning(result.warnings, instruction, "Indirect JALR unsupported");
            emit(Opcode::CALL, {registerValue(instruction.rs)}, {});
            break;
        default:
            addWarning(result.warnings, instruction, "Unsupported opcode");
            if (options.emitUnknownAsNop)
            {
                emit(Opcode::NOP, {}, {});
            }
            break;
        }
    }

    return result;
}

} // namespace ir
} // namespace psxrecomp
