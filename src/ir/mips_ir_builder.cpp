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

    for (const auto& instruction : instructions)
    {
        const auto address = instruction.address;
        auto emit = [&](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs)
        {
            result.instructions.push_back(
                builder.makeInstruction(opcode, std::move(inputs), std::move(outputs), address));
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
