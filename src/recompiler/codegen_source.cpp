#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "codegen_runtime_helpers.h"
#include "codegen_source_internal.h"
#include "cpp_emitter.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace
{
std::string escapeStringLiteral(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value)
    {
        if (ch == '\\' || ch == '\"')
            escaped.push_back('\\');
        if (ch == '\n')
            escaped += "\\n";
        else if (ch == '\r')
            escaped += "\\r";
        else if (ch == '\t')
            escaped += "\\t";
        else
            escaped.push_back(ch);
    }
    return escaped;
}

} // namespace
std::string CodeGenerator::generateSource(const ir::Program& program, const std::string& moduleName,
                                          const ModuleMetadata& metadata)
{
    CppEmitter emitter;
    std::vector<std::pair<Address, std::string>> functionSymbols;
    std::vector<FunctionDispatchRange> functionRanges;
    functionSymbols.reserve(program.functions.size());
    functionRanges.reserve(program.functions.size());
    std::unordered_set<std::string> usedFunctionNames;
    for (const auto& function : program.functions)
    {
        std::string functionSymbol = uniquifyIdentifier(function.name, usedFunctionNames);
        functionSymbols.emplace_back(function.entryAddress, functionSymbol);

        const Address start = function.entryAddress & 0x1FFFFFFFu;
        Address end = start;
        for (const auto& block : function.blocks)
        {
            for (const auto& instruction : block.instructions)
            {
                if (!instruction.sourceAddress.has_value())
                {
                    continue;
                }
                end = std::max(end, *instruction.sourceAddress & 0x1FFFFFFFu);
            }
        }

        functionRanges.push_back({start, end + 4u, functionSymbol});
    }
    std::stable_sort(functionRanges.begin(), functionRanges.end(),
                     [](const FunctionDispatchRange& left, const FunctionDispatchRange& right)
                     {
                         if (left.start != right.start)
                         {
                             return left.start < right.start;
                         }
                         return left.endExclusive < right.endExclusive;
                     });
    std::vector<FunctionDispatchRange> deduplicatedRanges;
    deduplicatedRanges.reserve(functionRanges.size());
    for (const auto& range : functionRanges)
    {
        if (!deduplicatedRanges.empty())
        {
            const auto& previous = deduplicatedRanges.back();
            if (range.start == previous.start && range.endExclusive == previous.endExclusive)
            {
                continue;
            }
        }
        deduplicatedRanges.push_back(range);
    }
    functionRanges = std::move(deduplicatedRanges);

    for (size_t index = 1; index < functionRanges.size(); ++index)
    {
        const auto& previous = functionRanges[index - 1];
        const auto& current = functionRanges[index];
        if (current.start < previous.endExclusive)
        {
            std::ostringstream stream;
            stream << "Overlapping function dispatch ranges: " << previous.functionSymbol << " [0x"
                   << std::hex << previous.start << ", 0x" << previous.endExclusive << ") and "
                   << current.functionSymbol << " [0x" << current.start << ", 0x"
                   << current.endExclusive << ").";
            throw std::runtime_error(stream.str());
        }
    }

    Address moduleEntryAddress = metadata.entryAddress;
    if (moduleEntryAddress == 0 && !functionSymbols.empty())
    {
        moduleEntryAddress = functionSymbols.front().first;
    }
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeLine("#include \"psxrecomp/runtime/mips_unaligned_access.h\"");
    emitter.writeBlank();
    emitter.writeLine("#include <algorithm>");
    emitter.writeLine("#include <array>");
    emitter.writeLine("#include <chrono>");
    emitter.writeLine("#include <cstdlib>");
    emitter.writeLine("#include <cstdint>");
    emitter.writeLine("#include <cstring>");
    emitter.writeLine("#include <iostream>");
    emitter.writeLine("#include <sstream>");
    emitter.writeLine("#include <stdexcept>");
    emitter.writeLine("#include <string>");
    emitter.writeLine("#include <unordered_map>");
    emitter.writeLine("#include <unordered_set>");
    emitter.writeLine("#include <vector>");
    emitter.writeBlank();
    emitter.writeLine("#ifndef PSXRECOMP_ENABLE_LOGGING");
    emitter.writeLine("#define PSXRECOMP_ENABLE_LOGGING 0");
    emitter.writeLine("#endif");
    emitter.writeLine("#ifndef PSXRECOMP_LOG_LEVEL");
    emitter.writeLine("#define PSXRECOMP_LOG_LEVEL 1");
    emitter.writeLine("#endif");
    emitter.writeLine("#ifndef PSXRECOMP_ENABLE_CHECKS");
    emitter.writeLine("#define PSXRECOMP_ENABLE_CHECKS 1");
    emitter.writeLine("#endif");
    emitter.writeLine("#ifndef PSXRECOMP_STRICT_ADDR_ERRORS");
    emitter.writeLine("#define PSXRECOMP_STRICT_ADDR_ERRORS 1");
    emitter.writeLine("#endif");
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");
    emitter.openBlock("struct RecompilerContext");
    emitter.writeLine("runtime::PsxSystem& system;");
    emitter.writeLine("std::array<u32, Registers::NUM_REGISTERS> regs{};");
    emitter.writeLine("u32 hi = 0;");
    emitter.writeLine("u32 lo = 0;");
    emitter.writeLine("bool pendingLoadValid = false;");
    emitter.writeLine("Register pendingLoadRegister = Registers::ZERO;");
    emitter.writeLine("u32 pendingLoadValue = 0;");
    emitter.writeLine("bool stagedLoadValid = false;");
    emitter.writeLine("Register stagedLoadRegister = Registers::ZERO;");
    emitter.writeLine("u32 stagedLoadValue = 0;");
    emitter.writeLine("u32 pendingCycles = 0;");
    emitter.writeLine("Address cachedRangeStart = 0;");
    emitter.writeLine("Address cachedRangeEndExclusive = 0;");
    emitter.writeLine("bool (*cachedRangeFn)(RecompilerContext&, Address) = nullptr;");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLine(
        "inline bool jumpRecompiledFunction(RecompilerContext& context, Address address);");
    emitter.writeLine(
        "inline bool callRecompiledFunction(RecompilerContext& context, Address address);");
    emitter.writeBlank();
    emitter.openBlock("namespace");
    emitRuntimeSupportHelpers(emitter);
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateGlobals(program));
    emitter.writeBlank();
    emitter.writeLine("static const std::vector<const char*> kPipelineWarnings = {");
    if (metadata.warnings.empty())
    {
        emitter.writeLine("};");
    }
    else
    {
        for (size_t index = 0; index < metadata.warnings.size(); ++index)
        {
            std::string line = "    \"" + escapeStringLiteral(metadata.warnings[index]) + "\"";
            if (index + 1 < metadata.warnings.size())
            {
                line += ",";
            }
            emitter.writeLine(line);
        }
        emitter.writeLine("};");
    }
    emitter.writeBlank();

    emitter.openBlock("struct FnRange");
    emitter.writeLine("Address start;");
    emitter.writeLine("Address endExclusive;");
    emitter.writeLine("bool (*fn)(RecompilerContext&, Address);");
    emitter.closeBlock(";");
    emitter.writeLine("static constexpr FnRange kFnRanges[] = {");
    for (const auto& range : functionRanges)
    {
        std::ostringstream line;
        line << "    {0x" << std::hex << range.start << ", 0x" << range.endExclusive << ", "
             << range.functionSymbol << "},";
        emitter.writeLine(line.str());
    }
    emitter.writeLine("};");
    emitter.writeBlank();

    emitGeneratedSourceDebugSupport(emitter, metadata);
    emitGeneratedSourceBody(emitter, metadata, moduleEntryAddress, functionSymbols);
    emitter.writeLines(generateFunctionDefinitions(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
} // namespace recompiler
} // namespace psxrecomp
