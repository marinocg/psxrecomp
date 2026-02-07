#pragma once

#include "psxrecomp/types.h"
#include <optional>
#include <string>

namespace psxrecomp
{
namespace disasm
{

/**
 * @brief MIPS instruction types
 */
enum class InstructionType
{
    R_TYPE,      // Register type (arithmetic, logic)
    I_TYPE,      // Immediate type (load, store, branch)
    J_TYPE,      // Jump type
    COPROCESSOR, // Coprocessor instructions (GTE)
    UNKNOWN
};

/**
 * @brief MIPS instruction opcodes
 */
enum class Opcode
{
    // R-type
    ADD,
    ADDU,
    SUB,
    SUBU,
    AND,
    OR,
    XOR,
    NOR,
    SLT,
    SLTU,
    SLL,
    SRL,
    SRA,
    SLLV,
    SRLV,
    SRAV,
    MULT,
    MULTU,
    DIV,
    DIVU,
    MFHI,
    MTHI,
    MFLO,
    MTLO,
    JR,
    JALR,
    SYSCALL,
    BREAK,

    // I-type
    ADDI,
    ADDIU,
    ANDI,
    ORI,
    XORI,
    SLTI,
    SLTIU,
    LUI,
    LB,
    LH,
    LW,
    LBU,
    LHU,
    LWL,
    LWR,
    SB,
    SH,
    SW,
    SWL,
    SWR,
    BEQ,
    BNE,
    BLEZ,
    BGTZ,
    BLTZ,
    BGEZ,
    BLTZAL,
    BGEZAL,

    // J-type
    J,
    JAL,

    // Coprocessor
    MFC0,
    MTC0,
    CFC0,
    CTC0,
    TLBR,
    TLBWI,
    TLBWR,
    TLBP,
    RFE,
    MFC2,
    MTC2,
    CFC2,
    CTC2,
    GTE_RTPS,
    GTE_RTPT,
    GTE_NCLIP,
    GTE_OP,
    GTE_DPCS,
    GTE_INTPL,
    GTE_MVMVA,
    GTE_NCDS,
    GTE_CDP,
    GTE_NCDT,
    GTE_NCCS,
    GTE_CC,
    GTE_NCS,
    GTE_NCT,
    GTE_SQR,
    GTE_DCPL,
    GTE_DPCT,
    GTE_AVSZ3,
    GTE_AVSZ4,
    GTE_GPF,
    GTE_GPL,
    GTE_NCCT,
    LWC2,
    SWC2,

    UNKNOWN
};

/**
 * @brief Represents a decoded MIPS instruction
 */
struct Instruction
{
    Address address;      // Address of this instruction
    u32 encoding;         // Raw 32-bit encoding
    Opcode opcode;        // Decoded opcode
    InstructionType type; // Instruction type

    // Operands
    Register rd;   // Destination register
    Register rs;   // Source register 1
    Register rt;   // Source register 2
    s16 immediate; // Immediate value
    u32 target;    // Jump target
    u8 shamt;      // Shift amount

    // Branch delay slot flag
    bool isInDelaySlot;
    std::optional<Address> delaySlotOwner;

    /**
     * @brief Convert instruction to assembly string
     * @return Assembly representation
     */
    std::string toString() const;

    /**
     * @brief Check if this is a branch instruction
     */
    bool isBranch() const;

    /**
     * @brief Check if this is a jump instruction
     */
    bool isJump() const;

    /**
     * @brief Check if this is a function call
     */
    bool isCall() const;

    /**
     * @brief Check if this is a function return
     */
    bool isReturn() const;

    /**
     * @brief Get branch/jump target address if applicable
     */
    std::optional<Address> getTargetAddress() const;
};

/**
 * @brief MIPS R3000 disassembler
 */
class MipsDisassembler
{
  public:
    /**
     * @brief Decode a single MIPS instruction
     * @param encoding 32-bit instruction encoding
     * @param address Address of the instruction
     * @return Decoded instruction
     */
    static Instruction decode(u32 encoding, Address address);

    /**
     * @brief Disassemble a block of code
     * @param data Pointer to code data
     * @param size Size of code in bytes
     * @param baseAddress Base address for instructions
     * @return Vector of decoded instructions
     */
    static std::vector<Instruction> disassemble(const u8* data, size_t size, Address baseAddress);

    /**
     * @brief Get register name for the given index.
     */
    static std::string getRegisterName(Register reg);

  private:
    static Instruction decodeRType(u32 encoding, Address address);
    static Instruction decodeIType(u32 encoding, Address address);
    static Instruction decodeJType(u32 encoding, Address address);
    static Instruction decodeCoprocessor(u32 encoding, Address address);
};

} // namespace disasm
} // namespace psxrecomp
