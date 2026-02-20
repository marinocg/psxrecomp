#include "mips_ir_translation.h"

#include <iomanip>
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

namespace
{
std::string formatUnsupportedOpcodeMessage(const disasm::Instruction& instr)
{
    std::ostringstream stream;
    const u32 primaryOpcode = (instr.encoding >> 26) & 0x3Fu;
    const u32 functionCode = instr.encoding & 0x3Fu;
    stream << "Unsupported opcode: " << instr.toString() << " (word=0x" << std::hex << std::setw(8)
           << std::setfill('0') << instr.encoding << ", op=0x" << std::setw(2) << primaryOpcode
           << ", funct=0x" << std::setw(2) << functionCode;
    if (instr.isInDelaySlot)
    {
        stream << ", in_delay_slot";
        if (instr.delaySlotOwner.has_value())
        {
            stream << ", owner=0x" << std::setw(8) << instr.delaySlotOwner.value();
        }
    }
    stream << ")";
    return stream.str();
}
} // namespace

void MipsIrTranslator::translateNoDelay(const disasm::Instruction& instr,
                                        std::optional<Address> sourceAddressOverride)
{
    const Address sourceAddress = sourceAddressOverride.value_or(instr.address);
    auto emit = [&](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs)
    { emitInstruction(opcode, std::move(inputs), std::move(outputs), sourceAddress); };
    auto emitLinkRegister = [&](Register linkRegister)
    {
        emit(Opcode::MOVE, {Value::makeImmediate(static_cast<s32>(linkAddressForJump(instr)))},
             {Value::makeRegister(linkRegister)});
    };

    if (isMipsNop(instr))
    {
        emit(Opcode::NOP, {}, {});
        return;
    }

    switch (instr.opcode)
    {
    case disasm::Opcode::ADD:
    case disasm::Opcode::ADDU:
        emit(Opcode::ADD, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SUB:
    case disasm::Opcode::SUBU:
        emit(Opcode::SUB, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::AND:
        emit(Opcode::AND, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::OR:
        emit(Opcode::OR, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::XOR:
        emit(Opcode::XOR, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::NOR:
        emit(Opcode::OR, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        emit(Opcode::XOR,
             {Value::makeRegister(instr.rd), Value::makeImmediate(static_cast<s32>(0xFFFFFFFFu))},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SLT:
        emit(Opcode::COMPARE_LT, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SLTU:
        emit(Opcode::COMPARE_LTU, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SLL:
        emit(Opcode::SHL,
             {Value::makeRegister(instr.rt), Value::makeImmediate(static_cast<s32>(instr.shamt))},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SRL:
        emit(Opcode::SHR_LOGICAL,
             {Value::makeRegister(instr.rt), Value::makeImmediate(static_cast<s32>(instr.shamt))},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SRA:
        emit(Opcode::SHR_ARITH,
             {Value::makeRegister(instr.rt), Value::makeImmediate(static_cast<s32>(instr.shamt))},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SLLV:
        emit(Opcode::SHL, {Value::makeRegister(instr.rt), Value::makeRegister(instr.rs)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SRLV:
        emit(Opcode::SHR_LOGICAL, {Value::makeRegister(instr.rt), Value::makeRegister(instr.rs)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::SRAV:
        emit(Opcode::SHR_ARITH, {Value::makeRegister(instr.rt), Value::makeRegister(instr.rs)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::MULT:
        emit(Opcode::MUL, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeSpecial(SpecialRegister::HI), Value::makeSpecial(SpecialRegister::LO)});
        break;
    case disasm::Opcode::MULTU:
        emit(Opcode::MULU, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeSpecial(SpecialRegister::HI), Value::makeSpecial(SpecialRegister::LO)});
        break;
    case disasm::Opcode::DIV:
        emit(Opcode::DIV, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeSpecial(SpecialRegister::HI), Value::makeSpecial(SpecialRegister::LO)});
        break;
    case disasm::Opcode::DIVU:
        emit(Opcode::DIVU, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {Value::makeSpecial(SpecialRegister::HI), Value::makeSpecial(SpecialRegister::LO)});
        break;
    case disasm::Opcode::MFHI:
        emit(Opcode::MOVE, {Value::makeSpecial(SpecialRegister::HI)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::MFLO:
        emit(Opcode::MOVE, {Value::makeSpecial(SpecialRegister::LO)},
             {Value::makeRegister(instr.rd)});
        break;
    case disasm::Opcode::MTHI:
        emit(Opcode::MOVE, {Value::makeRegister(instr.rs)},
             {Value::makeSpecial(SpecialRegister::HI)});
        break;
    case disasm::Opcode::MTLO:
        emit(Opcode::MOVE, {Value::makeRegister(instr.rs)},
             {Value::makeSpecial(SpecialRegister::LO)});
        break;
    case disasm::Opcode::ADDI:
    case disasm::Opcode::ADDIU:
        emit(Opcode::ADD,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<s32>(instr.immediate))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::ANDI:
        emit(Opcode::AND,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<u16>(instr.immediate))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::ORI:
        emit(Opcode::OR,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<u16>(instr.immediate))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::XORI:
        emit(Opcode::XOR,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<u16>(instr.immediate))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::SLTI:
        emit(Opcode::COMPARE_LT,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<s32>(instr.immediate))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::SLTIU:
        emit(Opcode::COMPARE_LTU,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<s32>(instr.immediate))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::LUI:
        emit(Opcode::MOVE,
             {Value::makeImmediate(static_cast<s32>(static_cast<u16>(instr.immediate)) << 16)},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::LB:
    case disasm::Opcode::LBU:
    case disasm::Opcode::LH:
    case disasm::Opcode::LHU:
    case disasm::Opcode::LWL:
    case disasm::Opcode::LWR:
    case disasm::Opcode::LW:
    {
        if (isMmioImmediate(instr.rs, instr.immediate))
        {
            emit(Opcode::MMIO_LOAD,
                 {Value::makeAddress(static_cast<Address>(static_cast<s32>(instr.immediate)))},
                 {Value::makeRegister(instr.rt)});
            break;
        }
        Value addressTemp = m_builder.createTemporary();
        emit(Opcode::ADD,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<s32>(instr.immediate))},
             {addressTemp});
        Opcode loadOp = Opcode::LOAD;
        if (instr.opcode == disasm::Opcode::LB)
            loadOp = Opcode::LOAD8;
        else if (instr.opcode == disasm::Opcode::LBU)
            loadOp = Opcode::LOAD8U;
        else if (instr.opcode == disasm::Opcode::LH)
            loadOp = Opcode::LOAD16;
        else if (instr.opcode == disasm::Opcode::LHU)
            loadOp = Opcode::LOAD16U;
        emit(loadOp, {addressTemp}, {Value::makeRegister(instr.rt)});
        break;
    }
    case disasm::Opcode::SB:
    case disasm::Opcode::SH:
    case disasm::Opcode::SWL:
    case disasm::Opcode::SWR:
    case disasm::Opcode::SW:
    {
        if (isMmioImmediate(instr.rs, instr.immediate))
        {
            emit(Opcode::MMIO_STORE,
                 {Value::makeAddress(static_cast<Address>(static_cast<s32>(instr.immediate))),
                  Value::makeRegister(instr.rt)},
                 {});
            break;
        }
        Value addressTemp = m_builder.createTemporary();
        emit(Opcode::ADD,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<s32>(instr.immediate))},
             {addressTemp});
        Opcode storeOp = Opcode::STORE;
        if (instr.opcode == disasm::Opcode::SB)
            storeOp = Opcode::STORE8;
        else if (instr.opcode == disasm::Opcode::SH)
            storeOp = Opcode::STORE16;
        emit(storeOp, {addressTemp, Value::makeRegister(instr.rt)}, {});
        break;
    }
    case disasm::Opcode::BEQ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_EQ, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {condTemp});
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BEQ missing branch target");
        }
        break;
    }
    case disasm::Opcode::BNE:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_NE, {Value::makeRegister(instr.rs), Value::makeRegister(instr.rt)},
             {condTemp});
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BNE missing branch target");
        }
        break;
    }
    case disasm::Opcode::BLEZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_LE, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BLEZ missing branch target");
        }
        break;
    }
    case disasm::Opcode::BGTZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_GT, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BGTZ missing branch target");
        }
        break;
    }
    case disasm::Opcode::BLTZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_LT, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BLTZ missing branch target");
        }
        break;
    }
    case disasm::Opcode::BGEZ:
    {
        Value condTemp = m_builder.createTemporary();
        emit(Opcode::COMPARE_GE, {Value::makeRegister(instr.rs), Value::makeImmediate(0)},
             {condTemp});
        auto target = instr.getBranchTarget();
        if (target.has_value())
        {
            emit(Opcode::BRANCH, {condTemp, Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "BGEZ missing branch target");
        }
        break;
    }
    case disasm::Opcode::J:
    {
        auto target = instr.getJumpTarget();
        if (target.has_value())
        {
            emit(Opcode::JUMP, {Value::makeAddress(*target)}, {});
        }
        else
        {
            addError(instr, "J missing jump target");
        }
        break;
    }
    case disasm::Opcode::JAL:
    {
        emitLinkRegister(Registers::RA);
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
        break;
    }
    case disasm::Opcode::BLTZAL:
    case disasm::Opcode::BGEZAL:
    {
        Value condTemp = m_builder.createTemporary();
        const Opcode compareOp =
            instr.opcode == disasm::Opcode::BLTZAL ? Opcode::COMPARE_LT : Opcode::COMPARE_GE;
        emit(compareOp, {Value::makeRegister(instr.rs), Value::makeImmediate(0)}, {condTemp});
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
        break;
    }
    case disasm::Opcode::JR:
        if (instr.isReturn())
        {
            emit(Opcode::RETURN, {}, {});
        }
        else
        {
            emit(Opcode::JUMP, {Value::makeRegister(instr.rs)}, {});
        }
        break;
    case disasm::Opcode::JALR:
        emitLinkRegister(instr.rd == Registers::ZERO ? Registers::RA : instr.rd);
        emit(Opcode::CALL, {Value::makeRegister(instr.rs)}, {});
        break;
    case disasm::Opcode::BREAK:
    {
        const u32 breakCode = (instr.encoding >> 6) & 0xFFFFF;
        addWarning(instr, "BREAK lowered to TRAP");
        emit(Opcode::TRAP, {Value::makeImmediate(static_cast<s32>(breakCode))}, {});
        break;
    }
    case disasm::Opcode::SYSCALL:
    {
        const u32 syscallCode = (instr.encoding >> 6) & 0xFFFFF;
        emit(Opcode::SYSCALL, {Value::makeImmediate(static_cast<s32>(syscallCode))}, {});
        break;
    }
    default:
        addWarning(instr, formatUnsupportedOpcodeMessage(instr));
        if (m_options.emitUnknownAsNop)
        {
            emit(Opcode::NOP, {}, {});
        }
        break;
    }
}

} // namespace detail
} // namespace ir
} // namespace psxrecomp
