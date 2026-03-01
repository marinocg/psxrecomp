#include "mips_ir_translation.h"

#include <sstream>

namespace psxrecomp
{
namespace ir
{
namespace detail
{

namespace
{
Address linkAddressForJump(const disasm::Instruction& instruction)
{
    return instruction.address + 8;
}
} // namespace

MipsIrTranslator::MipsIrTranslator(Builder& builder, MipsIrBuildResult& result,
                                   const MipsIrBuildOptions& options)
    : m_builder(builder), m_result(result), m_options(options)
{
}

void MipsIrTranslator::translate(const std::vector<disasm::Instruction>& instructions)
{
    m_targetedAddresses.clear();
    for (const auto& instruction : instructions)
    {
        if (auto target = instruction.getBranchTarget())
        {
            m_targetedAddresses.insert(*target);
        }
        if (auto target = instruction.getJumpTarget())
        {
            m_targetedAddresses.insert(*target);
        }
    }

    for (size_t index = 0; index < instructions.size(); ++index)
    {
        const auto& instruction = instructions[index];
        if (instruction.isInDelaySlot && instruction.delaySlotOwner.has_value())
        {
            const bool ownedByPrevious =
                index > 0 && instructions[index - 1].hasDelaySlot() &&
                instructions[index - 1].address == instruction.delaySlotOwner.value();
            if (ownedByPrevious)
            {
                // Delay-slot instructions are normally emitted by their owner.
                // Keep a second, normal-context copy only if the slot is a
                // direct branch/jump/call target.
                if (m_targetedAddresses.find(instruction.address) == m_targetedAddresses.end())
                {
                    continue;
                }
            }
            else
            {
                addWarning(instruction, "Delay-slot instruction without owner");
            }
        }

        const disasm::Instruction* delaySlot = nullptr;
        if (instruction.hasDelaySlot())
        {
            if (index + 1 >= instructions.size())
            {
                addError(instruction, "Missing delay-slot instruction");
            }
            else
            {
                const auto& candidate = instructions[index + 1];
                if (!candidate.isInDelaySlot || candidate.delaySlotOwner != instruction.address)
                {
                    addError(instruction, "Delay-slot metadata mismatch");
                }
                else
                {
                    delaySlot = &candidate;
                }
            }
        }

        translateWithDelay(instruction, delaySlot);
    }
}

void MipsIrTranslator::translateWithDelay(const disasm::Instruction& instr,
                                          const disasm::Instruction* delaySlot)
{
    const std::string sourceAsm = instr.toString();
    auto emit = [&](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs)
    { emitInstruction(opcode, std::move(inputs), std::move(outputs), instr.address, sourceAsm); };
    auto emitLinkRegister = [&](Register linkRegister)
    {
        emit(Opcode::MOVE, {Value::makeImmediate(static_cast<s32>(linkAddressForJump(instr)))},
             {Value::makeRegister(linkRegister)});
    };

    switch (instr.opcode)
    {
    case disasm::Opcode::BEQ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_EQ, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {condTemp});
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BEQ missing branch target");
        }
        return;
    }
    case disasm::Opcode::BNE:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_NE, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {condTemp});
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BNE missing branch target");
        }
        return;
    }
    case disasm::Opcode::BLEZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_LE, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BLEZ missing branch target");
        }
        return;
    }
    case disasm::Opcode::BGTZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_GT, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BGTZ missing branch target");
        }
        return;
    }
    case disasm::Opcode::BLTZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_LT, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BLTZ missing branch target");
        }
        return;
    }
    case disasm::Opcode::BGEZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_GE, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BGEZ missing branch target");
        }
        return;
    }
    case disasm::Opcode::J:
    {
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getJumpTarget();
        if (target.has_value())
        {
            emit(Opcode::JUMP, {Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "J missing jump target");
        }
        return;
    }
    case disasm::Opcode::JAL:
    {
        emitLinkRegister(Registers::RA);
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getJumpTarget();
        if (target.has_value())
        {
            if (isBiosStubAddress(*target))
            {
                addWarning(instr, "JAL to BIOS vector lowered to CALL intrinsic");
                emit(Opcode::CALL, {Value::makeAddress(*target)}, {});
            }
            else
            {
                emit(Opcode::CALL, {Value::makeAddress(*target)}, {});
            }
        }
        else
        {
            addError(instr, "JAL missing call target");
        }
        return;
    }
    case disasm::Opcode::BLTZAL:
    case disasm::Opcode::BGEZAL:
    {
        Value condTemp = m_builder.createTemporary();
        const Opcode compareOp =
            instr.opcode == disasm::Opcode::BLTZAL ? Opcode::COMPARE_LT : Opcode::COMPARE_GE;
        emit(compareOp, {Value::makeRegister(instr.rs), Value::makeImmediate(0)}, {condTemp});
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emitLinkRegister(Registers::RA);
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "Conditional link branch missing target");
        }
        return;
    }
    case disasm::Opcode::JR:
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        if (instr.isReturn())
        {
            emit(Opcode::RETURN, {}, {});
        }
        else
        {
            emit(Opcode::JUMP, {Value::makeRegister(instr.rs)}, {});
        }
        return;
    case disasm::Opcode::JALR:
        if (delaySlot != nullptr)
        {
            translateNoDelay(*delaySlot, instr.address);
        }
        emitLinkRegister(instr.rd == Registers::ZERO ? Registers::RA : instr.rd);
        emit(Opcode::CALL, {Value::makeRegister(instr.rs)}, {});
        return;
    default:
        translateNoDelay(instr);
        return;
    }
}

void MipsIrTranslator::emitInstruction(Opcode opcode, std::vector<Value> inputs,
                                       std::vector<Value> outputs, Address sourceAddress,
                                       const std::string& sourceAsm)
{
    m_result.instructions.push_back(m_builder.makeInstruction(
        opcode, std::move(inputs), std::move(outputs), sourceAddress, sourceAsm));
}

void MipsIrTranslator::addWarning(const disasm::Instruction& instruction,
                                  const std::string& message)
{
    std::ostringstream stream;
    stream << message << " @ " << formatAddress(instruction.address);
    m_result.warnings.push_back(stream.str());
}

void MipsIrTranslator::addError(const disasm::Instruction& instruction, const std::string& message)
{
    std::ostringstream stream;
    stream << message << " @ " << formatAddress(instruction.address);
    m_result.errors.push_back(stream.str());
}

bool MipsIrTranslator::isMipsNop(const disasm::Instruction& instruction)
{
    return instruction.opcode == disasm::Opcode::SLL && instruction.rd == Registers::ZERO &&
           instruction.rt == Registers::ZERO && instruction.shamt == 0;
}

bool MipsIrTranslator::isMmioImmediate(Register base, s16 immediate)
{
    if (base != Registers::ZERO)
    {
        return false;
    }
    const Address address = static_cast<Address>(static_cast<s32>(immediate));
    return address >= MemoryMap::IO_BASE && address < (MemoryMap::IO_BASE + MemoryMap::IO_SIZE);
}

bool MipsIrTranslator::isBiosStubAddress(Address address)
{
    const Address normalized = address & 0x1FFFFFFFu;
    return normalized == 0xA0 || normalized == 0xB0 || normalized == 0xC0;
}

std::string MipsIrTranslator::formatAddress(Address address)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << address;
    return stream.str();
}

} // namespace detail
} // namespace ir
} // namespace psxrecomp
