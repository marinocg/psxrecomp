#pragma once

#include "psxrecomp/types.h"

#include <deque>
#include <optional>
#include <string>
#include <string_view>
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
    ADD,
    SUB,
    AND,
    OR,
    XOR,
    COMPARE_EQ,
    COMPARE_NE,
    COMPARE_LT,
    COMPARE_LE,
    COMPARE_GT,
    COMPARE_GE,
    LOAD,
    STORE,
    BRANCH,
    JUMP,
    CALL,
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
    TEMPORARY
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

    static Value invalid();
    static Value makeRegister(Register reg);
    static Value makeImmediate(s32 value);
    static Value makeAddress(Address value);
    static Value makeTemporary(u32 id);

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
    std::optional<Address> sourceAddress;

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
                                std::optional<Address> sourceAddress = std::nullopt);

  private:
    Program& m_program;
    u32 m_nextTemporaryId;
};

} // namespace ir
} // namespace psxrecomp
