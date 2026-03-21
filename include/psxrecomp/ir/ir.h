#pragma once

#include "psxrecomp/types.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace ir
{

/**
 * @brief IR opcode listing for the recompiler pipeline.
 */
enum class Opcode
{
    NOP,
    PHI,
    MOVE,
    ADD_TRAP,
    ADD,
    SUB_TRAP,
    SUB,
    AND,
    OR,
    XOR,
    SHL,
    SHR_LOGICAL,
    SHR_ARITH,
    MUL,
    MULU,
    DIV,
    DIVU,
    COMPARE_EQ,
    COMPARE_NE,
    COMPARE_LT,
    COMPARE_LTU,
    COMPARE_LE,
    COMPARE_GT,
    COMPARE_GE,
    LOAD,
    LOAD8,
    LOAD8U,
    LOAD16,
    LOAD16U,
    LOAD_LEFT,
    LOAD_RIGHT,
    STORE,
    STORE8,
    STORE16,
    STORE_LEFT,
    STORE_RIGHT,
    MMIO_LOAD8,
    MMIO_LOAD8U,
    MMIO_LOAD16,
    MMIO_LOAD16U,
    MMIO_LOAD,
    MMIO_STORE8,
    MMIO_STORE16,
    MMIO_STORE,
    BRANCH,
    JUMP,
    CALL,
    COP0_MFC,
    COP0_MTC,
    COP0_RFE,
    GTE_MFC2,
    GTE_MTC2,
    GTE_CFC2,
    GTE_CTC2,
    GTE_LWC2,
    GTE_SWC2,
    GTE_EXEC,
    CPU_EXCEPTION,
    SYSCALL,
    TRAP,
    RETURN
};

/**
 * @brief The kind of value referenced by an IR instruction.
 */
enum class ValueKind
{
    INVALID,
    REGISTER,
    IMMEDIATE,
    ADDRESS,
    TEMPORARY,
    SPECIAL
};

/**
 * @brief Special registers represented in IR.
 */
enum class SpecialRegister
{
    HI,
    LO
};

/**
 * @brief Represents a value in the IR.
 */
struct Value
{
    ValueKind kind;
    Register reg;
    s32 immediate;
    Address address;
    u32 temporaryId;
    SpecialRegister specialReg;

    static Value invalid();
    static Value makeRegister(Register reg);
    static Value makeImmediate(s32 value);
    static Value makeAddress(Address value);
    static Value makeTemporary(u32 id);
    static Value makeSpecial(SpecialRegister reg);

    std::string toString() const;
};

bool operator==(const Value& lhs, const Value& rhs);

/**
 * @brief Represents a single IR instruction with input and output values.
 */
struct Instruction
{
    Opcode opcode;
    std::vector<Value> inputs;
    std::vector<Value> outputs;
    std::optional<Address> sourceAddress = std::nullopt;
    std::optional<std::string> sourceAsm = std::nullopt;
    std::optional<Address> sourceAsmAddress = std::nullopt;

    std::string toString() const;
};

/**
 * @brief A basic block of sequential IR instructions.
 */
struct BasicBlock
{
    std::string name;
    std::vector<Instruction> instructions;
    std::vector<std::string> successors;

    /// Maps predecessor block name → continuation block name.
    /// Used by the "block_external" barrier so the code generator
    /// knows where to resume execution after a call/jump that
    /// leaves the current function.
    std::unordered_map<std::string, std::string> continuations;

    std::string toString() const;
};

/**
 * @brief Function-level container for IR blocks.
 */
struct Function
{
    std::string name;
    Address entryAddress;
    std::deque<BasicBlock> blocks;

    BasicBlock& addBlock(std::string_view blockName);
};

/**
 * @brief Represents global data emitted with the program.
 */
struct GlobalData
{
    std::string name;
    std::vector<u8> bytes;
};

/**
 * @brief IR program containing all recompiled functions.
 */
struct Program
{
    std::deque<Function> functions;
    std::deque<GlobalData> globals;

    Function& addFunction(std::string_view functionName, Address entryAddress);
    GlobalData& addGlobal(std::string_view globalName, std::vector<u8> data);
};

/**
 * @brief Helper for building IR values and blocks.
 */
class Builder
{
  public:
    explicit Builder(Program& program);

    Function& createFunction(std::string_view functionName, Address entryAddress);
    BasicBlock& createBlock(Function& function, std::string_view blockName);

    Value createTemporary();
    Instruction makeInstruction(Opcode opcode, std::vector<Value> inputs,
                                std::vector<Value> outputs,
                                std::optional<Address> sourceAddress = std::nullopt,
                                std::optional<std::string> sourceAsm = std::nullopt,
                                std::optional<Address> sourceAsmAddress = std::nullopt);

  private:
    Program& m_program;
    u32 m_nextTemporaryId;
};

} // namespace ir
} // namespace psxrecomp
