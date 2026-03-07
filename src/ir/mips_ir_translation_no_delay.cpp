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
constexpr s32 EXCEPTION_CODE_RESERVED_INSTRUCTION = 10;
constexpr s32 EXCEPTION_CODE_COPROCESSOR_UNUSABLE = 11;
} // namespace

void MipsIrTranslator::translateNoDelay(const disasm::Instruction& instr,
                                        std::optional<Address> sourceAddressOverride)
{
    const Address sourceAddress = sourceAddressOverride.value_or(instr.address);
    const std::optional<std::string> sourceAsm =
        m_options.captureSourceAsm ? std::make_optional(instr.toString()) : std::nullopt;
    const std::optional<Address> sourceAsmAddress =
        m_options.captureSourceAsm ? std::make_optional(instr.address) : std::nullopt;
    auto emit = [&](Opcode opcode, std::vector<Value> inputs, std::vector<Value> outputs)
    {
        emitInstruction(opcode, std::move(inputs), std::move(outputs), sourceAddress, sourceAsm,
                        sourceAsmAddress);
    };
    auto emitCpuException = [&](s32 exceptionCode)
    {
        emit(Opcode::CPU_EXCEPTION,
             {Value::makeImmediate(exceptionCode),
              Value::makeImmediate(instr.isInDelaySlot ? 1 : 0)},
             {});
    };

    if (isMipsNop(instr))
    {
        emit(Opcode::NOP, {}, {});
        return;
    }

    const u32 primaryOpcode = (instr.encoding >> 26) & 0x3Fu;
    switch (primaryOpcode)
    {
    case 0x31: // LWC1
    case 0x33: // LWC3
    case 0x35: // LDC1
    case 0x37: // LDC3
    case 0x39: // SWC1
    case 0x3B: // SWC3
    case 0x3D: // SDC1
    case 0x3F: // SDC3
        emitCpuException(EXCEPTION_CODE_COPROCESSOR_UNUSABLE);
        return;
    default:
        break;
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
    case disasm::Opcode::MFC0:
    case disasm::Opcode::CFC0:
        emit(Opcode::COP0_MFC, {Value::makeImmediate(static_cast<s32>(instr.rd))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::MTC0:
    case disasm::Opcode::CTC0:
        emit(Opcode::COP0_MTC,
             {Value::makeImmediate(static_cast<s32>(instr.rd)), Value::makeRegister(instr.rt)}, {});
        break;
    case disasm::Opcode::RFE:
        emit(Opcode::COP0_RFE, {}, {});
        break;
    case disasm::Opcode::MFC2:
        emit(Opcode::GTE_MFC2, {Value::makeImmediate(static_cast<s32>(instr.rd))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::MTC2:
        emit(Opcode::GTE_MTC2,
             {Value::makeImmediate(static_cast<s32>(instr.rd)), Value::makeRegister(instr.rt)}, {});
        break;
    case disasm::Opcode::CFC2:
        emit(Opcode::GTE_CFC2, {Value::makeImmediate(static_cast<s32>(instr.rd))},
             {Value::makeRegister(instr.rt)});
        break;
    case disasm::Opcode::CTC2:
        emit(Opcode::GTE_CTC2,
             {Value::makeImmediate(static_cast<s32>(instr.rd)), Value::makeRegister(instr.rt)}, {});
        break;
    case disasm::Opcode::LWC2:
    {
        Value addressTemp = m_builder.createTemporary();
        emit(Opcode::ADD,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<s32>(instr.immediate))},
             {addressTemp});
        emit(Opcode::GTE_LWC2, {Value::makeImmediate(static_cast<s32>(instr.rt)), addressTemp}, {});
        break;
    }
    case disasm::Opcode::SWC2:
    {
        Value addressTemp = m_builder.createTemporary();
        emit(Opcode::ADD,
             {Value::makeRegister(instr.rs),
              Value::makeImmediate(static_cast<s32>(instr.immediate))},
             {addressTemp});
        emit(Opcode::GTE_SWC2, {Value::makeImmediate(static_cast<s32>(instr.rt)), addressTemp}, {});
        break;
    }
    case disasm::Opcode::GTE_RTPS:
    case disasm::Opcode::GTE_RTPT:
    case disasm::Opcode::GTE_NCLIP:
    case disasm::Opcode::GTE_OP:
    case disasm::Opcode::GTE_DPCS:
    case disasm::Opcode::GTE_INTPL:
    case disasm::Opcode::GTE_MVMVA:
    case disasm::Opcode::GTE_NCDS:
    case disasm::Opcode::GTE_CDP:
    case disasm::Opcode::GTE_NCDT:
    case disasm::Opcode::GTE_NCCS:
    case disasm::Opcode::GTE_CC:
    case disasm::Opcode::GTE_NCS:
    case disasm::Opcode::GTE_NCT:
    case disasm::Opcode::GTE_SQR:
    case disasm::Opcode::GTE_DCPL:
    case disasm::Opcode::GTE_DPCT:
    case disasm::Opcode::GTE_AVSZ3:
    case disasm::Opcode::GTE_AVSZ4:
    case disasm::Opcode::GTE_GPF:
    case disasm::Opcode::GTE_GPL:
    case disasm::Opcode::GTE_NCCT:
        emit(Opcode::GTE_EXEC, {Value::makeImmediate(static_cast<s32>(instr.encoding))}, {});
        break;
    case disasm::Opcode::TLBR:
    case disasm::Opcode::TLBWI:
    case disasm::Opcode::TLBWR:
    case disasm::Opcode::TLBP:
    case disasm::Opcode::BC0F:
    case disasm::Opcode::BC0T:
        emitCpuException(EXCEPTION_CODE_RESERVED_INSTRUCTION);
        break;
    case disasm::Opcode::LWC0:
    case disasm::Opcode::SWC0:
        emitCpuException(EXCEPTION_CODE_COPROCESSOR_UNUSABLE);
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
    default:
        translateNoDelayLoadStoreAndBranch(instr, sourceAddress, sourceAsm, sourceAsmAddress);
        break;
    }
}

} // namespace detail
} // namespace ir
} // namespace psxrecomp
