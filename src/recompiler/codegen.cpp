#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "codegen_runtime_helpers.h"
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
    emitter.writeLine("static void configure(runtime::PsxSystem& system);");
    emitter.writeLine("static void initMemory(runtime::PsxSystem& system);");
    emitter.writeLine("static void run(runtime::PsxSystem& system);");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLines(generateFunctionDeclarations(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}
std::string CodeGenerator::generateSource(const ir::Program& program, const std::string& moduleName,
                                          const ModuleMetadata& metadata)
{
    struct FunctionDispatchRange
    {
        Address start = 0;
        Address endExclusive = 0;
        std::string functionSymbol;
    };

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
    emitter.writeBlank();
    emitter.openBlock("namespace psxrecomp");
    emitter.openBlock("namespace recompiler");
    emitter.openBlock("struct RecompilerContext");
    emitter.writeLine("runtime::PsxSystem& system;");
    emitter.writeLine("std::array<u32, Registers::NUM_REGISTERS> regs{};");
    emitter.writeLine("u32 hi = 0;");
    emitter.writeLine("u32 lo = 0;");
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

    auto emitRecompiledDispatchSwitch = [&](const std::string& recurseHelper)
    {
        emitter.writeLine("switch (physical)");
        emitter.openBlock("");
        {
            std::unordered_set<Address> emittedEntries;

            // First: emit cases for every function entry point (called with
            // default startAddress = 0 so execution begins at the first block).
            for (const auto& entry : functionSymbols)
            {
                const Address normalizedEntry = entry.first & 0x1FFFFFFFu;
                if (!emittedEntries.insert(normalizedEntry).second)
                {
                    continue;
                }
                std::ostringstream caseLine;
                caseLine << "case 0x" << std::hex << normalizedEntry << ":";
                emitter.writeLine(caseLine.str());
                emitter.openBlock("");
                emitter.writeLine("return " + entry.second + "(context);");
                emitter.closeBlock();
            }
        }
        emitter.writeLine("default:");
        emitter.openBlock("");
        emitter.writeLine("if (context.cachedRangeFn != nullptr)");
        emitter.openBlock("");
        emitter.writeLine("if (context.cachedRangeStart <= physical &&");
        emitter.writeLine("    physical < context.cachedRangeEndExclusive)");
        emitter.openBlock("");
        emitter.writeLine("if (context.cachedRangeFn(context, physical))");
        emitter.openBlock("");
        emitter.writeLine("return true;");
        emitter.closeBlock();
        emitter.writeLine("context.cachedRangeStart = 0;");
        emitter.writeLine("context.cachedRangeEndExclusive = 0;");
        emitter.writeLine("context.cachedRangeFn = nullptr;");
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.writeLine("// kFnRanges are non-overlapping (validated during generation).");
        emitter.writeLine("const size_t rangeCount = sizeof(kFnRanges) / sizeof(kFnRanges[0]);");
        emitter.writeLine("size_t low = 0;");
        emitter.writeLine("size_t high = rangeCount;");
        emitter.writeLine("while (low < high)");
        emitter.openBlock("");
        emitter.writeLine("const size_t mid = low + ((high - low) / 2);");
        emitter.writeLine("if (kFnRanges[mid].start <= physical)");
        emitter.openBlock("");
        emitter.writeLine("low = mid + 1;");
        emitter.closeBlock();
        emitter.writeLine("else");
        emitter.openBlock("");
        emitter.writeLine("high = mid;");
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.writeLine("if (low > 0)");
        emitter.openBlock("");
        emitter.writeLine("const FnRange& range = kFnRanges[low - 1];");
        emitter.writeLine("if (physical < range.endExclusive)");
        emitter.openBlock("");
        emitter.writeLine("context.cachedRangeStart = range.start;");
        emitter.writeLine("context.cachedRangeEndExclusive = range.endExclusive;");
        emitter.writeLine("context.cachedRangeFn = range.fn;");
        emitter.writeLine("if (range.fn(context, physical))");
        emitter.openBlock("");
        emitter.writeLine("return true;");
        emitter.closeBlock();
        emitter.writeLine("context.cachedRangeStart = 0;");
        emitter.writeLine("context.cachedRangeEndExclusive = 0;");
        emitter.writeLine("context.cachedRangeFn = nullptr;");
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.writeLine("if (physical <= psxrecomp::MemoryMap::RAM_SIZE - sizeof(u32))");
        emitter.openBlock("");
        emitter.writeLine(
            "const Address indirect = readMemory32(context.system, address) & 0x1FFFFFFF;");
        emitter.writeLine("if (indirect != physical)");
        emitter.openBlock("");
        emitter.writeLine("return " + recurseHelper + "(context, indirect);");
        emitter.closeBlock();
        emitter.closeBlock();
        emitter.writeLine("return false;");
        emitter.closeBlock();
        emitter.closeBlock();
    };

    emitter.writeLine(
        "inline bool jumpRecompiledFunction(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("static const bool traceCalls = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_TRACE_CALLS\"))");
    emitter.openBlock("");
    emitter.writeLine("return env[0] == '1';");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock("();");
    emitter.writeLine("if (traceCalls)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[jump] target=0x\" << std::hex << physical << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("const bool handled = [&]() -> bool");
    emitter.openBlock("");
    emitRecompiledDispatchSwitch("jumpRecompiledFunction");
    emitter.closeBlock("();");
    emitter.writeLine("if (handled)");
    emitter.openBlock("");
    emitter.writeLine("context.regs[Registers::ZERO] = 0;");
    emitter.closeBlock();
    emitter.writeLine("return handled;");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine(
        "inline bool callRecompiledFunction(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("static const bool traceCalls = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_TRACE_CALLS\"))");
    emitter.openBlock("");
    emitter.writeLine("return env[0] == '1';");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock("();");
    emitter.writeLine("if (traceCalls)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[call] target=0x\" << std::hex << physical");
    emitter.writeLine("         << \" ra=0x\" << context.regs[Registers::RA] << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("const u32 preservedS0 = context.regs[Registers::S0];");
    emitter.writeLine("const u32 preservedS1 = context.regs[Registers::S1];");
    emitter.writeLine("const u32 preservedS2 = context.regs[Registers::S2];");
    emitter.writeLine("const u32 preservedS3 = context.regs[Registers::S3];");
    emitter.writeLine("const u32 preservedS4 = context.regs[Registers::S4];");
    emitter.writeLine("const u32 preservedS5 = context.regs[Registers::S5];");
    emitter.writeLine("const u32 preservedS6 = context.regs[Registers::S6];");
    emitter.writeLine("const u32 preservedS7 = context.regs[Registers::S7];");
    emitter.writeLine("const u32 preservedGp = context.regs[Registers::GP];");
    emitter.writeLine("const u32 preservedSp = context.regs[Registers::SP];");
    emitter.writeLine("const u32 preservedFp = context.regs[Registers::FP];");
    emitter.writeLine("const u32 preservedRa = context.regs[Registers::RA];");
    emitter.writeLine("auto restoreCalleeSaved = [&]()");
    emitter.openBlock("");
    emitter.writeLine("context.regs[Registers::S0] = preservedS0;");
    emitter.writeLine("context.regs[Registers::S1] = preservedS1;");
    emitter.writeLine("context.regs[Registers::S2] = preservedS2;");
    emitter.writeLine("context.regs[Registers::S3] = preservedS3;");
    emitter.writeLine("context.regs[Registers::S4] = preservedS4;");
    emitter.writeLine("context.regs[Registers::S5] = preservedS5;");
    emitter.writeLine("context.regs[Registers::S6] = preservedS6;");
    emitter.writeLine("context.regs[Registers::S7] = preservedS7;");
    emitter.writeLine("context.regs[Registers::GP] = preservedGp;");
    emitter.writeLine("context.regs[Registers::SP] = preservedSp;");
    emitter.writeLine("context.regs[Registers::FP] = preservedFp;");
    emitter.writeLine("context.regs[Registers::RA] = preservedRa;");
    emitter.writeLine("context.regs[Registers::ZERO] = 0;");
    emitter.closeBlock(";");
    emitter.writeLine("const bool handled = [&]() -> bool");
    emitter.openBlock("");
    emitRecompiledDispatchSwitch("callRecompiledFunction");
    emitter.closeBlock("();");
    emitter.writeLine("if (traceCalls && handled)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[call-ret] target=0x\" << std::hex << physical");
    emitter.writeLine("         << \" v0=0x\" << context.regs[Registers::V0] << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if (handled)");
    emitter.openBlock("");
    emitter.writeLine("restoreCalleeSaved();");
    emitter.closeBlock();
    emitter.writeLine("return handled;");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::configure(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("runtime::PsxSystem::DiscSwapInfo info;");
    emitter.writeLine("info.setName = \"" + escapeStringLiteral(metadata.discSetName) + "\";");
    emitter.writeLine("info.activeDiscIndex = " + std::to_string(metadata.activeDiscIndex) + ";");
    if (metadata.discs.empty())
    {
        emitter.writeLine("info.discs = {};");
    }
    else
    {
        emitter.writeLine("info.discs = {");
        for (size_t index = 0; index < metadata.discs.size(); ++index)
        {
            const auto& disc = metadata.discs[index];
            std::ostringstream line;
            line << "    {" << disc.index << ", \"" << escapeStringLiteral(disc.label) << "\", \""
                 << escapeStringLiteral(disc.path) << "\"}";
            if (index + 1 < metadata.discs.size())
            {
                line << ",";
            }
            emitter.writeLine(line.str());
        }
        emitter.writeLine("};");
    }
    emitter.writeLine("system.setDiscSwapInfo(info);");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("for (const auto* warning : kPipelineWarnings)");
    emitter.openBlock("");
    emitter.writeLine("logWarning(warning);");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    // Emit RAM init image from PSX-EXE payload.
    if (!metadata.programData.empty())
    {
        emitter.writeLine("// PSX-EXE payload data to be loaded into RAM at boot.");
        {
            std::ostringstream loadAddrLiteral;
            loadAddrLiteral << "static constexpr Address kRamInitLoadAddress = 0x" << std::hex
                            << metadata.loadAddress << ";";
            emitter.writeLine(loadAddrLiteral.str());
        }
        emitter.writeLine("static constexpr u32 kRamInitLoadSize = " +
                          std::to_string(metadata.programData.size()) + ";");
        emitter.writeLine("static const u8 kRamInitData[] = {");
        {
            constexpr size_t bytesPerLine = 32;
            for (size_t i = 0; i < metadata.programData.size(); i += bytesPerLine)
            {
                std::ostringstream line;
                line << "    ";
                size_t end = std::min(i + bytesPerLine, metadata.programData.size());
                for (size_t j = i; j < end; ++j)
                {
                    line << static_cast<int>(metadata.programData[j]);
                    if (j + 1 < metadata.programData.size())
                    {
                        line << ",";
                    }
                }
                emitter.writeLine(line.str());
            }
        }
        emitter.writeLine("};");
    }
    else
    {
        emitter.writeLine("// No PSX-EXE payload data available.");
        emitter.writeLine("static constexpr Address kRamInitLoadAddress = 0;");
        emitter.writeLine("static constexpr u32 kRamInitLoadSize = 0;");
        emitter.writeLine("static const u8* kRamInitData = nullptr;");
    }
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::initMemory(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("if (kRamInitLoadSize > 0)");
    emitter.openBlock("");
    emitter.writeLine("const Address offset = kRamInitLoadAddress & 0x1FFFFF;");
    emitter.writeLine("if (offset + kRamInitLoadSize <= psxrecomp::MemoryMap::RAM_SIZE)");
    emitter.openBlock("");
    emitter.writeLine("std::memcpy(system.getRam() + offset, kRamInitData, kRamInitLoadSize);");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp] Loaded \" << kRamInitLoadSize");
    emitter.writeLine(
        "           << \" bytes into RAM at 0x\" << std::hex << kRamInitLoadAddress << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("else");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] RAM init image exceeds RAM bounds.\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("void RecompiledModule::run(runtime::PsxSystem& system)");
    emitter.openBlock("");
    emitter.writeLine("RecompilerContext context{system, {}};");

    // Install only the ABI-required callback invoker bridge.
    // Compatibility shims (VSync/DrawSync detection, runtime toggles) are intentionally omitted.
    emitter.writeBlank();
    emitter.writeLine("// Install callback invoker bridge for interrupt dispatch.");
    emitter.writeLine("system.setCallbackInvoker(");
    emitter.writeLine("    [&context, &system](u32 address) -> u32");
    emitter.openBlock("");
    emitter.writeLine("const auto savedRegs = context.regs;");
    emitter.writeLine("const u32 savedHi = context.hi;");
    emitter.writeLine("const u32 savedLo = context.lo;");
    emitter.writeLine("system.consumePendingCallbackRegisters(context.regs);");
    emitter.writeLine("try");
    emitter.openBlock("");
    emitter.writeLine("callRecompiledFunction(context, address);");
    emitter.writeLine("const u32 callbackResult = context.regs[Registers::V0];");
    emitter.writeLine("context.regs = savedRegs;");
    emitter.writeLine("context.hi = savedHi;");
    emitter.writeLine("context.lo = savedLo;");
    emitter.writeLine("return callbackResult;");
    emitter.closeBlock();
    emitter.writeLine("catch (...) ");
    emitter.openBlock("");
    emitter.writeLine("context.regs = savedRegs;");
    emitter.writeLine("context.hi = savedHi;");
    emitter.writeLine("context.lo = savedLo;");
    emitter.writeLine("throw;");
    emitter.closeBlock();
    emitter.closeBlock(");");

    // Set initial registers from PSX-EXE header.
    {
        // Stack pointer: use header value if present, else default 0x801FFF00.
        u32 initialSp = metadata.stackAddress;
        if (initialSp == 0)
        {
            initialSp = 0x801FFF00u;
        }
        else if (metadata.stackSize > 0)
        {
            // Stack grows downward; initial SP = stackAddress + stackSize.
            initialSp += metadata.stackSize;
        }
        std::ostringstream spLiteral;
        spLiteral << "context.regs[Registers::SP] = 0x" << std::hex << initialSp
                  << "; // initial stack pointer";
        emitter.writeLine(spLiteral.str());
    }
    {
        std::ostringstream gpLiteral;
        gpLiteral << "context.regs[Registers::GP] = 0x" << std::hex << metadata.initialGp
                  << "; // initial global pointer";
        emitter.writeLine(gpLiteral.str());
    }
    // Also set $fp = $sp as many programs assume this.
    emitter.writeLine("context.regs[Registers::FP] = context.regs[Registers::SP];");
    // Set $ra to 0 so a return from the entry function exits cleanly.
    emitter.writeLine("context.regs[Registers::RA] = 0;");

    {
        std::ostringstream entryLiteral;
        entryLiteral << "constexpr Address kModuleEntryAddress = 0x" << std::hex
                     << (moduleEntryAddress & 0x1FFFFFFFu) << ";";
        emitter.writeLine(entryLiteral.str());
    }
    emitter.writeLine("if (!callRecompiledFunction(context, kModuleEntryAddress))");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"Module entry function not emitted for address 0x\" << std::hex");
    emitter.writeLine("       << kModuleEntryAddress << \".\";");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLines(generateFunctionDefinitions(program));
    emitter.closeBlock();
    emitter.closeBlock();
    return emitter.str();
}

} // namespace recompiler
} // namespace psxrecomp
