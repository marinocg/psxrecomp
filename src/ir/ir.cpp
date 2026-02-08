#include "psxrecomp/ir/ir.h"

#include <sstream>

namespace psxrecomp
{
namespace ir
{

namespace
{
std::string opcodeToString(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::NOP:
        return "nop";
    case Opcode::PHI:
        return "phi";
    case Opcode::MOVE:
        return "move";
    case Opcode::ADD:
        return "add";
    case Opcode::SUB:
        return "sub";
    case Opcode::AND:
        return "and";
    case Opcode::OR:
        return "or";
    case Opcode::XOR:
        return "xor";
    case Opcode::LOAD:
        return "load";
    case Opcode::STORE:
        return "store";
    case Opcode::BRANCH:
        return "branch";
    case Opcode::JUMP:
        return "jump";
    case Opcode::CALL:
        return "call";
    case Opcode::RETURN:
        return "return";
    }
    return "unknown";
}
} // namespace

Value Value::invalid()
{
    return {ValueKind::INVALID, 0, 0, 0, 0};
}

Value Value::makeRegister(Register reg)
{
    return {ValueKind::REGISTER, reg, 0, 0, 0};
}

Value Value::makeImmediate(s32 value)
{
    return {ValueKind::IMMEDIATE, 0, value, 0, 0};
}

Value Value::makeAddress(Address value)
{
    return {ValueKind::ADDRESS, 0, 0, value, 0};
}

Value Value::makeTemporary(u32 id)
{
    return {ValueKind::TEMPORARY, 0, 0, 0, id};
}

std::string Value::toString() const
{
    std::ostringstream stream;
    switch (kind)
    {
    case ValueKind::INVALID:
        return "<invalid>";
    case ValueKind::REGISTER:
        stream << "r" << static_cast<int>(reg);
        return stream.str();
    case ValueKind::IMMEDIATE:
        stream << immediate;
        return stream.str();
    case ValueKind::ADDRESS:
        stream << "0x" << std::hex << address;
        return stream.str();
    case ValueKind::TEMPORARY:
        stream << "t" << temporaryId;
        return stream.str();
    }
    return "<unknown>";
}

bool operator==(const Value& lhs, const Value& rhs)
{
    return lhs.kind == rhs.kind && lhs.reg == rhs.reg && lhs.immediate == rhs.immediate &&
           lhs.address == rhs.address && lhs.temporaryId == rhs.temporaryId;
}

std::string Instruction::toString() const
{
    std::ostringstream stream;
    stream << opcodeToString(opcode);

    if (!outputs.empty())
    {
        stream << " ";
        for (size_t index = 0; index < outputs.size(); ++index)
        {
            if (index > 0)
            {
                stream << ", ";
            }
            stream << outputs[index].toString();
        }
    }

    if (!inputs.empty())
    {
        stream << (outputs.empty() ? " " : " <- ");
        for (size_t index = 0; index < inputs.size(); ++index)
        {
            if (index > 0)
            {
                stream << ", ";
            }
            stream << inputs[index].toString();
        }
    }

    if (sourceAddress.has_value())
    {
        stream << " @0x" << std::hex << *sourceAddress;
        stream << std::dec;
    }

    return stream.str();
}

std::string BasicBlock::toString() const
{
    std::ostringstream stream;
    stream << name << ":\n";
    for (const auto& instruction : instructions)
    {
        stream << "  " << instruction.toString() << "\n";
    }
    return stream.str();
}

BasicBlock& Function::addBlock(std::string_view blockName)
{
    blocks.push_back(BasicBlock{std::string(blockName), {}, {}});
    return blocks.back();
}

Function& Program::addFunction(std::string_view functionName, Address entryAddress)
{
    functions.push_back(Function{std::string(functionName), entryAddress, {}});
    return functions.back();
}

Builder::Builder(Program& program) : m_program(program), m_nextTemporaryId(0) {}

Function& Builder::createFunction(std::string_view functionName, Address entryAddress)
{
    return m_program.addFunction(functionName, entryAddress);
}

BasicBlock& Builder::createBlock(Function& function, std::string_view blockName)
{
    return function.addBlock(blockName);
}

Value Builder::createTemporary()
{
    return Value::makeTemporary(m_nextTemporaryId++);
}

Instruction Builder::makeInstruction(Opcode opcode, std::vector<Value> inputs,
                                     std::vector<Value> outputs,
                                     std::optional<Address> sourceAddress)
{
    return {opcode, std::move(inputs), std::move(outputs), sourceAddress};
}

} // namespace ir
} // namespace psxrecomp
