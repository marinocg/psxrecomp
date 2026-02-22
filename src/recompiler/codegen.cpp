#include "psxrecomp/recompiler/codegen.h"

#include "codegen_helpers.h"
#include "cpp_emitter.h"

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
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

void emitRuntimeSupportHelpers(CppEmitter& emitter)
{
    // readMemory32 — straightforward RAM read.
    emitter.writeLine("inline u32 readMemory32(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return system.read<u32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory32(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.write<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory8 — unsigned byte read.
    emitter.writeLine("inline u32 readMemory8(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return static_cast<u32>(system.read<u8>(address));");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory8s — signed byte read (sign-extend to 32 bits).
    emitter.writeLine("inline u32 readMemory8s(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine(
        "return static_cast<u32>(static_cast<s32>(static_cast<s8>(system.read<u8>(address))));");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory16 — unsigned halfword read.
    emitter.writeLine("inline u32 readMemory16(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return static_cast<u32>(system.read<u16>(address));");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory16s — signed halfword read (sign-extend to 32 bits).
    emitter.writeLine("inline u32 readMemory16s(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine(
        "return static_cast<u32>(static_cast<s32>(static_cast<s16>(system.read<u16>(address))));");
    emitter.closeBlock();
    emitter.writeBlank();

    // writeMemory8 — byte store.
    emitter.writeLine(
        "inline void writeMemory8(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.write<u8>(address, static_cast<u8>(value & 0xFF));");
    emitter.closeBlock();
    emitter.writeBlank();

    // writeMemory16 — halfword store.
    emitter.writeLine(
        "inline void writeMemory16(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("system.write<u16>(address, static_cast<u16>(value & 0xFFFF));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio32(runtime::PsxSystem& system, Address address)");
    emitter.openBlock("");
    emitter.writeLine("u32 value = system.readMmioExplicit<u32>(address);");
    emitter.writeLine("static const bool traceMmio = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_TRACE_MMIO\"))");
    emitter.openBlock("");
    emitter.writeLine("return env[0] == '1';");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock("();");
    emitter.writeLine("if (traceMmio)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[mmio] read32 0x\" << std::hex << address << \" = 0x\" "
                      "<< value << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("return value;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMmio32(runtime::PsxSystem& system, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("static const bool traceMmio = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_TRACE_MMIO\"))");
    emitter.openBlock("");
    emitter.writeLine("return env[0] == '1';");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock("();");
    emitter.writeLine("if (traceMmio)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[mmio] write32 0x\" << std::hex << address << \" = 0x\" "
                      "<< value << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("system.writeMmioExplicit<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void callSyscall(runtime::PsxSystem& system, u32 code, std::array<u32, "
        "Registers::NUM_REGISTERS>& regs)");
    emitter.openBlock("");
    emitter.writeLine("system.callBiosSyscall(code, regs.data(), regs.size());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void flushCycles(RecompilerContext& context)");
    emitter.openBlock("");
    emitter.writeLine("if (context.pendingCycles != 0)");
    emitter.openBlock("");
    emitter.writeLine("context.system.tickCpuCycles(context.pendingCycles);");
    emitter.writeLine("context.pendingCycles = 0;");
    emitter.writeLine("context.system.serviceInterrupts();");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("struct CycleScope");
    emitter.openBlock("");
    emitter.writeLine("RecompilerContext& context;");
    emitter.writeLine("explicit CycleScope(RecompilerContext& ctx) : context(ctx) {}");
    emitter.writeLine("~CycleScope() { flushCycles(context); }");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLine("inline void setProgramCounter(RecompilerContext& context, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("context.system.debugOverlay().setLastProgramCounter(pc);");
    emitter.writeBlank();
    emitter.writeLine("// Step budget: throw after N PC updates to break hangs.");
    emitter.writeLine("static uint64_t stepCount = 0;");
    emitter.writeLine("++stepCount;");
    emitter.writeLine("static const uint64_t maxSteps = []() -> uint64_t");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_MAX_STEPS\"))");
    emitter.openBlock("");
    emitter.writeLine("return std::strtoull(env, nullptr, 10);");
    emitter.closeBlock();
    emitter.writeLine("return 0; // 0 = unlimited");
    emitter.closeBlock("();");
    emitter.writeLine("if (maxSteps > 0 && stepCount >= maxSteps)");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine(
        "stream << \"Step budget exhausted after \" << stepCount << \" steps at PC 0x\"");
    emitter.writeLine("       << std::hex << pc;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("// Breakpoint: throw when a specific PC is reached.");
    emitter.writeLine("static const Address breakPc = []() -> Address");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_BREAK_PC\"))");
    emitter.openBlock("");
    emitter.writeLine("return static_cast<Address>(std::strtoul(env, nullptr, 16));");
    emitter.closeBlock();
    emitter.writeLine("return 0;");
    emitter.closeBlock("();");
    emitter.writeLine("if (breakPc != 0 && pc == breakPc)");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"Breakpoint hit at PC 0x\" << std::hex << pc;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("// Approximate one emulated CPU cycle per original instruction.");
    emitter.writeLine("++context.pendingCycles;");
    emitter.writeLine("static constexpr u32 kCycleFlushThreshold = 2048;");
    emitter.writeLine("if (context.pendingCycles >= kCycleFlushThreshold)");
    emitter.openBlock("");
    emitter.writeLine("flushCycles(context);");
    emitter.closeBlock();
    emitter.closeBlock(); // close setProgramCounter
    emitter.writeBlank();
    emitter.writeLine("inline bool callIntrinsic(runtime::PsxSystem& system, Address address, "
                      "std::array<u32, Registers::NUM_REGISTERS>& regs)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("// Handle BIOS vector calls (A0h/B0h/C0h).");
    emitter.writeLine("if (physical == 0xA0 || physical == 0xB0 || physical == 0xC0)");
    emitter.openBlock("");
    emitter.writeLine("system.callBiosVector(physical, regs.data(), regs.size());");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801810 && physical <= 0x1F801817)");
    emitter.openBlock("");
    emitter.writeLine("system.callGpuIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801800 && physical <= 0x1F801803)");
    emitter.openBlock("");
    emitter.writeLine("system.callCdromIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("if (physical >= 0x1F801C00 && physical <= 0x1F801DFF)");
    emitter.openBlock("");
    emitter.writeLine("system.callSpuIntrinsic(physical);");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void failUnsupportedCall(Address target, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("static std::unordered_set<Address> warned;");
    emitter.writeLine("if (warned.insert(target).second)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[psxrecomp][warn] Unsupported CALL target 0x\" << std::hex "
                      "<< target << \" at PC 0x\" << pc << \" (stubbed)\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("[[noreturn]] inline void failUnsupportedJump(Address target, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"Unsupported JR/JUMP target 0x\" << std::hex << target << \" at "
                      "PC 0x\" << pc;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void triggerTrap(u32 code, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("// On real PSX, BREAK triggers an exception whose handler simply");
    emitter.writeLine("// returns — execution continues at the next instruction.  The");
    emitter.writeLine("// most common code is 0x1C00 (divide-by-zero guard inserted by");
    emitter.writeLine("// PSn00bSDK / PSY-Q); the divide result is already safely set");
    emitter.writeLine("// to zero before we reach here.");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << \"[trap] BREAK code=0x\" << std::hex << code");
    emitter.writeLine("          << \" at PC 0x\" << pc << \" -- continuing\\n\";");
    emitter.closeBlock();
    emitter.writeLine("(void)code; (void)pc;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void logWarning(const char* message)");
    emitter.openBlock("");
    emitter.writeLine("if (PSXRECOMP_ENABLE_LOGGING)");
    emitter.openBlock("");
    emitter.writeLine("std::cerr << message << \"\\n\";");
    emitter.closeBlock();
    emitter.closeBlock();
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
    CppEmitter emitter;
    std::vector<std::pair<Address, std::string>> functionSymbols;
    functionSymbols.reserve(program.functions.size());
    std::unordered_set<std::string> usedFunctionNames;
    for (const auto& function : program.functions)
    {
        functionSymbols.emplace_back(function.entryAddress,
                                     uniquifyIdentifier(function.name, usedFunctionNames));
    }

    Address moduleEntryAddress = metadata.entryAddress;
    if (moduleEntryAddress == 0 && !functionSymbols.empty())
    {
        moduleEntryAddress = functionSymbols.front().first;
    }
    emitter.writeLine("#include \"" + moduleName + ".h\"");
    emitter.writeBlank();
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
    emitter.closeBlock(";");
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
                emitter.writeLine(entry.second + "(context);");
                emitter.writeLine("return true;");
                emitter.closeBlock();
            }

            // Second: emit cases for every non-entry block address so that
            // JALR/JR targets to a mid-function address can enter at the
            // correct block via the startAddress parameter.
            {
                std::unordered_set<std::string> usedNames2;
                for (const auto& function : program.functions)
                {
                    std::string funcName = uniquifyIdentifier(function.name, usedNames2);
                    for (const auto& block : function.blocks)
                    {
                        if (block.name.size() > 8 && block.name.substr(0, 8) == "block_0x")
                        {
                            const Address blockAddr =
                                std::stoul(block.name.substr(6), nullptr, 16) & 0x1FFFFFFFu;
                            if (emittedEntries.insert(blockAddr).second)
                            {
                                std::ostringstream caseLine;
                                caseLine << "case 0x" << std::hex << blockAddr << ":";
                                emitter.writeLine(caseLine.str());
                                emitter.openBlock("");
                                std::ostringstream callLine;
                                callLine << funcName << "(context, 0x" << std::hex << blockAddr
                                         << ");";
                                emitter.writeLine(callLine.str());
                                emitter.writeLine("return true;");
                                emitter.closeBlock();
                            }
                        }
                    }
                }
            }
        }
        emitter.writeLine("default:");
        emitter.openBlock("");
        emitter.writeLine("if (physical <= psxrecomp::MemoryMap::RAM_SIZE - sizeof(u32))");
        emitter.openBlock("");
        emitter.writeLine("const Address indirect = readMemory32(context.system, address) & 0x1FFFFFFF;");
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
    emitter.writeLine(
        "    [&context](u32 address) -> u32 { callRecompiledFunction(context, address); "
        "return context.regs[Registers::V0]; });");

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
