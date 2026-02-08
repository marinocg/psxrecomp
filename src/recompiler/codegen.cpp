#include "psxrecomp/recompiler/codegen.h"

#include "cpp_emitter.h"

#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{
namespace
{
struct LoweringContext
{
    std::map<u32, std::string> temporaries;
    bool generateComments = true;
    bool enableOptimizations = true;
};
std::string toIdentifier(std::string_view name)
{
    std::string identifier;
    identifier.reserve(name.size());
    for (char ch : name)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
        {
            identifier.push_back(ch);
        }
        else
        {
            identifier.push_back('_');
        }
    }
    if (identifier.empty())
    {
        identifier = "unnamed";
    }
    if (std::isdigit(static_cast<unsigned char>(identifier.front())))
    {
        identifier.insert(identifier.begin(), '_');
    }
    static const std::set<std::string> reserved = {"alignas",      "alignof",
                                                   "and",          "and_eq",
                                                   "asm",          "auto",
                                                   "bitand",       "bitor",
                                                   "bool",         "break",
                                                   "case",         "catch",
                                                   "char",         "char16_t",
                                                   "char32_t",     "class",
                                                   "compl",        "const",
                                                   "constexpr",    "const_cast",
                                                   "continue",     "decltype",
                                                   "default",      "delete",
                                                   "do",           "double",
                                                   "dynamic_cast", "else",
                                                   "enum",         "explicit",
                                                   "export",       "extern",
                                                   "false",        "float",
                                                   "for",          "friend",
                                                   "goto",         "if",
                                                   "inline",       "int",
                                                   "long",         "mutable",
                                                   "namespace",    "new",
                                                   "noexcept",     "not",
                                                   "not_eq",       "nullptr",
                                                   "operator",     "or",
                                                   "or_eq",        "private",
                                                   "protected",    "public",
                                                   "register",     "reinterpret_cast",
                                                   "return",       "short",
                                                   "signed",       "sizeof",
                                                   "static",       "static_assert",
                                                   "static_cast",  "struct",
                                                   "switch",       "template",
                                                   "this",         "thread_local",
                                                   "throw",        "true",
                                                   "try",          "typedef",
                                                   "typeid",       "typename",
                                                   "union",        "unsigned",
                                                   "using",        "virtual",
                                                   "void",         "volatile",
                                                   "wchar_t",      "while",
                                                   "xor",          "xor_eq"};
    if (reserved.find(identifier) != reserved.end())
    {
        identifier.insert(identifier.begin(), '_');
    }
    return identifier;
}

std::string uniquifyIdentifier(std::string_view name, std::unordered_set<std::string>& used)
{
    std::string base = toIdentifier(name);
    std::string candidate = base;
    size_t suffix = 1;
    while (!used.insert(candidate).second)
    {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}
std::string valueToExpr(const ir::Value& value, LoweringContext& context)
{
    switch (value.kind)
    {
    case ir::ValueKind::IMMEDIATE:
        return std::to_string(value.immediate);
    case ir::ValueKind::ADDRESS:
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << value.address;
        return stream.str();
    }
    case ir::ValueKind::REGISTER:
        return "context.regs[" + std::to_string(value.reg) + "]";
    case ir::ValueKind::TEMPORARY:
    {
        auto it = context.temporaries.find(value.temporaryId);
        if (it != context.temporaries.end())
        {
            return it->second;
        }
        std::string name = "temp" + std::to_string(value.temporaryId);
        context.temporaries[value.temporaryId] = name;
        return name;
    }
    case ir::ValueKind::INVALID:
        return "/* invalid */ 0";
    }
    return "0";
}
std::string opcodeToComment(ir::Opcode opcode)
{
    switch (opcode)
    {
    case ir::Opcode::NOP:
        return "nop";
    case ir::Opcode::PHI:
        return "phi";
    case ir::Opcode::MOVE:
        return "move";
    case ir::Opcode::ADD:
        return "add";
    case ir::Opcode::SUB:
        return "sub";
    case ir::Opcode::AND:
        return "and";
    case ir::Opcode::OR:
        return "or";
    case ir::Opcode::XOR:
        return "xor";
    case ir::Opcode::LOAD:
        return "load";
    case ir::Opcode::STORE:
        return "store";
    case ir::Opcode::BRANCH:
        return "branch";
    case ir::Opcode::JUMP:
        return "jump";
    case ir::Opcode::CALL:
        return "call";
    case ir::Opcode::RETURN:
        return "return";
    }
    return "unknown";
}
std::set<u32> collectTemporaries(const ir::Function& function)
{
    std::set<u32> temporaries;
    for (const auto& block : function.blocks)
    {
        for (const auto& instruction : block.instructions)
        {
            for (const auto& value : instruction.inputs)
            {
                if (value.kind == ir::ValueKind::TEMPORARY)
                {
                    temporaries.insert(value.temporaryId);
                }
            }
            for (const auto& value : instruction.outputs)
            {
                if (value.kind == ir::ValueKind::TEMPORARY)
                {
                    temporaries.insert(value.temporaryId);
                }
            }
        }
    }
    return temporaries;
}
enum class ZeroOptimization
{
    None,
    Elide,
    ZeroResult
};
std::string resolveBlockId(std::string_view name,
                           const std::unordered_map<std::string, std::string>& blockNames)
{
    auto it = blockNames.find(std::string(name));
    if (it != blockNames.end())
    {
        return "BlockId::" + it->second;
    }
    return "BlockId::" + toIdentifier(name);
}

void emitInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                     const std::unordered_map<std::string, std::string>& blockNames,
                     LoweringContext& context, CppEmitter& emitter)
{
    if (context.generateComments)
    {
        std::string comment = "// " + opcodeToComment(instruction.opcode);
        if (instruction.sourceAddress.has_value())
        {
            std::ostringstream stream;
            stream << comment << " @0x" << std::hex << *instruction.sourceAddress;
            comment = stream.str();
        }
        emitter.writeLine(comment);
    }

    auto writeBinaryOp = [&](const char* op, ZeroOptimization optimization)
    {
        if (instruction.outputs.empty() || instruction.inputs.size() < 2)
        {
            emitter.writeLine("// TODO: malformed binary op");
            return;
        }
        std::string lhs = valueToExpr(instruction.outputs.front(), context);
        std::string rhsA = valueToExpr(instruction.inputs[0], context);
        std::string rhsB = valueToExpr(instruction.inputs[1], context);

        if (context.enableOptimizations && instruction.inputs[1].kind == ir::ValueKind::IMMEDIATE &&
            instruction.inputs[1].immediate == 0)
        {
            if (optimization == ZeroOptimization::Elide)
            {
                emitter.writeLine(lhs + " = " + rhsA + ";");
                return;
            }
            if (optimization == ZeroOptimization::ZeroResult)
            {
                emitter.writeLine(lhs + " = 0;");
                return;
            }
            return;
        }
        emitter.writeLine(lhs + " = " + rhsA + " " + op + " " + rhsB + ";");
    };

    switch (instruction.opcode)
    {
    case ir::Opcode::NOP:
        emitter.writeLine(";");
        break;
    case ir::Opcode::PHI:
        emitter.writeLine("// TODO: phi node lowering");
        emitter.writeLine("std::abort();");
        break;
    case ir::Opcode::MOVE:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string source = valueToExpr(instruction.inputs.front(), context);
            if (!context.enableOptimizations || dest != source)
            {
                emitter.writeLine(dest + " = " + source + ";");
            }
        }
        break;
    case ir::Opcode::ADD:
        writeBinaryOp("+", ZeroOptimization::Elide);
        break;
    case ir::Opcode::SUB:
        writeBinaryOp("-", ZeroOptimization::Elide);
        break;
    case ir::Opcode::AND:
        writeBinaryOp("&", ZeroOptimization::ZeroResult);
        break;
    case ir::Opcode::OR:
        writeBinaryOp("|", ZeroOptimization::Elide);
        break;
    case ir::Opcode::XOR:
        writeBinaryOp("^", ZeroOptimization::Elide);
        break;
    case ir::Opcode::LOAD:
        if (!instruction.outputs.empty() && !instruction.inputs.empty())
        {
            std::string dest = valueToExpr(instruction.outputs.front(), context);
            std::string address = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine(dest + " = readMemory32(context.system, " + address + ");");
        }
        break;
    case ir::Opcode::STORE:
        if (instruction.inputs.size() >= 2)
        {
            std::string address = valueToExpr(instruction.inputs[0], context);
            std::string value = valueToExpr(instruction.inputs[1], context);
            emitter.writeLine("writeMemory32(context.system, " + address + ", " + value + ");");
        }
        break;
    case ir::Opcode::BRANCH:
        if (block.successors.size() >= 2 && !instruction.inputs.empty())
        {
            std::string cond = valueToExpr(instruction.inputs.front(), context);
            emitter.openBlock("if (" + cond + ")");
            emitter.writeLine("block = " + resolveBlockId(block.successors[0], blockNames) + ";");
            emitter.writeLine("continue;");
            emitter.closeBlock();
            emitter.openBlock("else");
            emitter.writeLine("block = " + resolveBlockId(block.successors[1], blockNames) + ";");
            emitter.writeLine("continue;");
            emitter.closeBlock();
        }
        break;
    case ir::Opcode::JUMP:
        if (!block.successors.empty())
        {
            emitter.writeLine("block = " + resolveBlockId(block.successors.front(), blockNames) +
                              ";");
            emitter.writeLine("continue;");
        }
        break;
    case ir::Opcode::CALL:
        if (!instruction.inputs.empty())
        {
            std::string target = valueToExpr(instruction.inputs.front(), context);
            emitter.writeLine("callIntrinsic(context.system, " + target + ");");
        }
        else
        {
            emitter.writeLine("// TODO: call lowering");
        }
        break;
    case ir::Opcode::RETURN:
        emitter.writeLine("return;");
        break;
    }
}
} // namespace
CodeGenerator::CodeGenerator(const CodeGenOptions& options) : m_options(options) {}
std::string CodeGenerator::generateHeader(const ir::Program& program, const std::string& moduleName)
{
    (void)moduleName;
    CppEmitter emitter;
    emitter.writeLine("#pragma once");
    emitter.writeBlank();
    emitter.writeLine("#include \"psxrecomp/runtime/psx_system.h\"");
    emitter.writeLine("#include \"psxrecomp/types.h\"");
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");
    emitter.writeLine("struct RecompilerContext;");
    emitter.openBlock("struct RecompiledModule");
    emitter.writeLine("static void run(runtime::PsxSystem& system);");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLines(generateFunctionDeclarations(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateSource(const ir::Program& program, const std::string& moduleName)
{
    CppEmitter emitter;
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
    emitter.writeLine("#include <array>");
    emitter.writeLine("#include <cstdlib>");
    emitter.writeLine("#include <cstdint>");
    emitter.writeLine("#include <cstring>");
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");
    emitter.openBlock("struct RecompilerContext");
    emitter.writeLine("runtime::PsxSystem& system;");
    emitter.writeLine("std::array<s32, Registers::NUM_REGISTERS> regs{};");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.openBlock("namespace");
    emitter.writeLine("inline s32 readMemory32(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return system.read<s32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory32(runtime::PsxSystem& system, Address address, s32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.write<s32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void callIntrinsic(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("if (physical >= 0x1F801810 && physical <= 0x1F801817)");
    emitter.openBlock("");
    emitter.writeLine("system.callGpuIntrinsic(physical);");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801800 && physical <= 0x1F801803)");
    emitter.openBlock("");
    emitter.writeLine("system.callCdromIntrinsic(physical);");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801C00 && physical <= 0x1F801DFF)");
    emitter.openBlock("");
    emitter.writeLine("system.callSpuIntrinsic(physical);");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateGlobals(program));
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::run(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("RecompilerContext context{system, {}};");
    if (!program.functions.empty())
    {
        emitter.writeLine(toIdentifier(program.functions.front().name) + "(context);");
    }
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateFunctionDefinitions(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateBuildFile(const std::string& projectName)
{
    std::ostringstream stream;
    stream << "cmake_minimum_required(VERSION 3.15)\n";
    stream << "project(" << projectName << " LANGUAGES CXX)\n";
    stream << "set(CMAKE_CXX_STANDARD 17)\n";
    stream << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    stream << "set(CMAKE_CXX_EXTENSIONS OFF)\n";
    stream << "\n";
    stream << "set(PSXRECOMP_INCLUDE_DIR \"\" CACHE PATH \"Path to psxrecomp headers\")\n";
    stream << "if(NOT PSXRECOMP_INCLUDE_DIR)\n";
    stream << "    message(FATAL_ERROR \"PSXRECOMP_INCLUDE_DIR is not set.\")\n";
    stream << "endif()\n";
    stream << "\n";
    stream << "add_library(" << projectName << " " << projectName << ".cpp)\n";
    stream << "target_include_directories(" << projectName << " PUBLIC\n";
    stream << "    ${PSXRECOMP_INCLUDE_DIR}\n";
    stream << "    include\n";
    stream << ")\n";
    return stream.str();
}
std::string CodeGenerator::generateGlobals(const ir::Program& program) const
{
    CppEmitter emitter;
    if (program.globals.empty())
    {
        emitter.writeLine("// No global data.");
        return emitter.str();
    }
    std::unordered_set<std::string> usedNames;
    for (const auto& global : program.globals)
    {
        std::string name = uniquifyIdentifier(global.name, usedNames);
        emitter.writeLine("static const std::array<u8, " + std::to_string(global.bytes.size()) +
                          "> " + name + " = {");
        if (!global.bytes.empty())
        {
            std::ostringstream stream;
            stream << "    ";
            for (size_t index = 0; index < global.bytes.size(); ++index)
            {
                stream << static_cast<int>(global.bytes[index]);
                if (index + 1 < global.bytes.size())
                {
                    stream << ", ";
                }
            }
            emitter.writeLine(stream.str());
        }
        emitter.writeLine("};");
    }
    return emitter.str();
}
std::string CodeGenerator::generateFunctionDeclarations(const ir::Program& program) const
{
    std::ostringstream stream;
    std::unordered_set<std::string> usedNames;
    for (const auto& function : program.functions)
    {
        std::string name = uniquifyIdentifier(function.name, usedNames);
        stream << "void " << name << "(RecompilerContext& context);\n";
    }
    return stream.str();
}
std::string CodeGenerator::generateFunctionDefinitions(const ir::Program& program) const
{
    CppEmitter emitter;
    std::unordered_set<std::string> usedFunctionNames;
    for (const auto& function : program.functions)
    {
        LoweringContext context;
        context.generateComments = m_options.generateComments;
        context.enableOptimizations = m_options.enableOptimizations;

        std::string functionName = uniquifyIdentifier(function.name, usedFunctionNames);
        emitter.writeLine("void " + functionName + "(RecompilerContext& context)");
        emitter.openBlock("");
        if (function.blocks.empty())
        {
            emitter.writeLine("// TODO: empty function body");
            emitter.writeLine("return;");
            emitter.closeBlock();
            emitter.writeBlank();
            continue;
        }

        auto temporaries = collectTemporaries(function);
        for (u32 temporaryId : temporaries)
        {
            context.temporaries[temporaryId] = "temp" + std::to_string(temporaryId);
            emitter.writeLine("s32 " + context.temporaries[temporaryId] + " = 0;");
        }

        emitter.writeBlank();
        std::unordered_set<std::string> usedBlocks;
        std::unordered_map<std::string, std::string> blockNames;
        std::vector<std::string> orderedBlockNames;
        orderedBlockNames.reserve(function.blocks.size());
        for (const auto& block : function.blocks)
        {
            std::string uniqueName = uniquifyIdentifier(block.name, usedBlocks);
            blockNames.emplace(block.name, uniqueName);
            orderedBlockNames.push_back(uniqueName);
        }

        emitter.writeLine("enum class BlockId {");
        for (size_t index = 0; index < orderedBlockNames.size(); ++index)
        {
            emitter.writeLine("    " + orderedBlockNames[index] +
                              (index + 1 < orderedBlockNames.size() ? "," : ""));
        }
        emitter.writeLine("};");
        if (!orderedBlockNames.empty())
        {
            emitter.writeLine("BlockId block = BlockId::" + orderedBlockNames.front() + ";");
        }
        emitter.writeLine("while (true)");
        emitter.openBlock("");
        emitter.writeLine("switch (block)");
        emitter.openBlock("");
        for (const auto& block : function.blocks)
        {
            emitter.writeLine("case " + resolveBlockId(block.name, blockNames) + ":");
            emitter.openBlock("");
            for (const auto& instruction : block.instructions)
            {
                emitInstruction(instruction, block, blockNames, context, emitter);
            }
            if (block.instructions.empty() ||
                block.instructions.back().opcode != ir::Opcode::RETURN)
            {
                if (!block.successors.empty())
                {
                    emitter.writeLine(
                        "block = " + resolveBlockId(block.successors.front(), blockNames) + ";");
                    emitter.writeLine("continue;");
                }
                else
                {
                    emitter.writeLine("return;");
                }
            }
            emitter.closeBlock();
        }
        emitter.writeLine("default:");
        emitter.openBlock("");
        emitter.writeLine("return;");
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.writeBlank();
    }
    return emitter.str();
}
} // namespace recompiler
} // namespace psxrecomp
