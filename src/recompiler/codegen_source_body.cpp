#include "codegen_source_internal.h"

#include "codegen_helpers.h"

#include <algorithm>
#include <sstream>
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
        if (ch == '\\' || ch == '"')
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

void emitGeneratedSourceBody(CppEmitter& emitter, const ModuleMetadata& metadata,
                             Address moduleEntryAddress,
                             const std::vector<std::pair<Address, std::string>>& functionSymbols)
{
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
    emitter.writeLine("static const bool traceCallArgs = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_TRACE_CALL_ARGS\"))");
    emitter.openBlock("");
    emitter.writeLine("return env[0] == '1';");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock("();");
    emitter.writeLine("if (traceCalls)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[call] target=0x\" << std::hex << physical");
    emitter.writeLine("         << \" ra=0x\" << context.regs[Registers::RA] << \"\\n\";");
    emitter.writeLine("if (traceCallArgs)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"       a0=0x\" << context.regs[Registers::A0]");
    emitter.writeLine("         << \" a1=0x\" << context.regs[Registers::A1]");
    emitter.writeLine("         << \" a2=0x\" << context.regs[Registers::A2]");
    emitter.writeLine("         << \" a3=0x\" << context.regs[Registers::A3]");
    emitter.writeLine("         << \" sp=0x\" << context.regs[Registers::SP] << \"\\n\";");
    emitter.closeBlock();
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
    emitter.writeLine("context.system.validateAllocatorHeapCallBoundary(physical);");
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
    emitter.writeLine(
        "const u32 callbackCommitGeneration = system.callbackContextCommitGeneration();");
    emitter.writeLine("const auto callbackContextDisposition =");
    emitter.writeLine("    system.consumePendingCallbackRegisters(context.regs);");
    emitter.writeLine(
        "const auto shouldCommitCallbackContext = [&system, callbackCommitGeneration,");
    emitter.writeLine("                                         callbackContextDisposition]()");
    emitter.openBlock("");
    emitter.writeLine(
        "return callbackContextDisposition == "
        "psxrecomp::runtime::PsxSystem::CallbackContextDisposition::CommitMutated ||");
    emitter.writeLine(
        "       system.callbackContextCommitGeneration() != callbackCommitGeneration;");
    emitter.closeBlock(";");
    emitter.writeLine("try");
    emitter.openBlock("");
    emitter.writeLine("callRecompiledFunction(context, address);");
    emitter.writeLine("const u32 callbackResult = context.regs[Registers::V0];");
    emitter.writeLine("if (shouldCommitCallbackContext())");
    emitter.openBlock("");
    emitter.writeLine("return callbackResult;");
    emitter.closeBlock();
    emitter.writeLine("context.regs = savedRegs;");
    emitter.writeLine("context.hi = savedHi;");
    emitter.writeLine("context.lo = savedLo;");
    emitter.writeLine("return callbackResult;");
    emitter.closeBlock();
    emitter.writeLine("catch (...) ");
    emitter.openBlock("");
    emitter.writeLine("if (!shouldCommitCallbackContext())");
    emitter.openBlock("");
    emitter.writeLine("context.regs = savedRegs;");
    emitter.writeLine("context.hi = savedHi;");
    emitter.writeLine("context.lo = savedLo;");
    emitter.closeBlock();
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
}

} // namespace recompiler
} // namespace psxrecomp
