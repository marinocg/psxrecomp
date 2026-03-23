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

namespace
{
constexpr psxrecomp::s32 EXCEPTION_CODE_RESERVED_INSTRUCTION = 10;
constexpr psxrecomp::s32 EXCEPTION_CODE_COPROCESSOR_UNUSABLE = 11;
} // namespace

void MipsIrTranslator::translateNoDelayLoadStoreAndBranch(
    const disasm::Instruction& instr, Address sourceAddress,
    const std::optional<std::string>& sourceAsm, std::optional<Address> sourceAsmAddress)
{
    auto emit = [&](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs)
    {
        emitInstruction(opcode, std::move(inputs), std::move(outputs), sourceAddress, sourceAsm,
                        sourceAsmAddress);
    };
    auto emitLinkRegister = [&](Register linkRegister)
    {
        emit(Opcode::MOVE, {Value::makeImmediate(static_cast<s32>(linkAddressForJump(instr)))},
             {Value::makeRegister(linkRegister)});
    };

    switch (instr.opcode)
    {
    case disasm::Opcode::LB:
    case disasm::Opcode::LBU:
    case disasm::Opcode::LH:
    case disasm::Opcode::LHU:
    case disasm::Opcode::LWL:
    case disasm::Opcode::LWR:
    case disasm::Opcode::LW:
    {
        const bool supportsMmioShortcut =
            instr.opcode == disasm::Opcode::LB || instr.opcode == disasm::Opcode::LBU ||
            instr.opcode == disasm::Opcode::LH || instr.opcode == disasm::Opcode::LHU ||
            instr.opcode == disasm::Opcode::LW;
        if (supportsMmioShortcut && isMmioImmediate(instr.rs, instr.immediate))
        {
            Opcode mmioLoadOp = Opcode::MMIO_LOAD;
            if (instr.opcode == disasm::Opcode::LB)
                mmioLoadOp = Opcode::MMIO_LOAD8;
            else if (instr.opcode == disasm::Opcode::LBU)
                mmioLoadOp = Opcode::MMIO_LOAD8U;
            else if (instr.opcode == disasm::Opcode::LH)
                mmioLoadOp = Opcode::MMIO_LOAD16;
            else if (instr.opcode == disasm::Opcode::LHU)
                mmioLoadOp = Opcode::MMIO_LOAD16U;

            emit(mmioLoadOp,
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
        else if (instr.opcode == disasm::Opcode::LWL)
            loadOp = Opcode::LOAD_LEFT;
        else if (instr.opcode == disasm::Opcode::LWR)
            loadOp = Opcode::LOAD_RIGHT;
        std::vector<Value> loadInputs = {addressTemp};
        if (loadOp == Opcode::LOAD_LEFT || loadOp == Opcode::LOAD_RIGHT)
        {
            loadInputs.push_back(Value::makeRegister(instr.rt));
        }
        emit(loadOp, std::move(loadInputs), {Value::makeRegister(instr.rt)});
        break;
    }
    case disasm::Opcode::SB:
    case disasm::Opcode::SH:
    case disasm::Opcode::SWL:
    case disasm::Opcode::SWR:
    case disasm::Opcode::SW:
    {
        const bool supportsMmioShortcut = instr.opcode == disasm::Opcode::SB ||
                                          instr.opcode == disasm::Opcode::SH ||
                                          instr.opcode == disasm::Opcode::SW;
        if (supportsMmioShortcut && isMmioImmediate(instr.rs, instr.immediate))
        {
            Opcode mmioStoreOp = Opcode::MMIO_STORE;
            if (instr.opcode == disasm::Opcode::SB)
                mmioStoreOp = Opcode::MMIO_STORE8;
            else if (instr.opcode == disasm::Opcode::SH)
                mmioStoreOp = Opcode::MMIO_STORE16;

            emit(mmioStoreOp,
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
        else if (instr.opcode == disasm::Opcode::SWL)
            storeOp = Opcode::STORE_LEFT;
        else if (instr.opcode == disasm::Opcode::SWR)
            storeOp = Opcode::STORE_RIGHT;
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
