#include "codegen_runtime_helpers.h"

#include "cpp_emitter.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRuntimeSupportHelpers(CppEmitter& emitter)
{
    emitter.writeLine("inline void flushCycles(RecompilerContext& context);");
    emitter.writeBlank();
    emitter.writeLine("inline bool strictAddrErrorsEnabled()");
    emitter.openBlock("");
    emitter.writeLine("#if PSXRECOMP_STRICT_ADDR_ERRORS");
    emitter.writeLine("return true;");
    emitter.writeLine("#else");
    emitter.writeLine("static const bool strictAddrErrors = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_STRICT_ADDR_ERRORS\"))");
    emitter.openBlock("");
    emitter.writeLine("return env[0] == '1';");
    emitter.closeBlock();
    emitter.writeLine("return false;");
    emitter.closeBlock("();");
    emitter.writeLine("return strictAddrErrors;");
    emitter.writeLine("#endif");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("[[noreturn]] inline void raiseAddressError(runtime::PsxSystem& system,");
    emitter.writeLine(
        "                                               runtime::Cop0::ExceptionCode code,");
    emitter.writeLine(
        "                                               Address pc, Address badVaddr)");
    emitter.openBlock("");
    emitter.writeLine("system.cop0().exceptionEnter(code, pc, false, badVaddr);");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine(
        "stream << \"Address error exception code=\" << std::dec << static_cast<u32>(code)");
    emitter.writeLine("       << \" at PC 0x\" << std::hex << pc");
    emitter.writeLine("       << \" badvaddr=0x\" << badVaddr;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();

    emitter.writeLine("inline void accountCpuDataAccessCycles(RecompilerContext& context,");
    emitter.writeLine("                                      Address address, bool isWrite)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
    emitter.writeLine("u32 extraCycles = 0;");
    emitter.openBlock("if (!isWrite && physical < 0x00800000u)");
    emitter.writeLine("// PSX-SPX: CPU data reads from main RAM take 6 waitstates plus");
    emitter.writeLine("// the opcode cycle. Writes are buffered through the write queue,");
    emitter.writeLine("// so do not need the same penalty in the common case.");
    emitter.writeLine("extraCycles = 6;");
    emitter.closeBlock();
    emitter.openBlock("if (extraCycles == 0)");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("context.pendingCycles += extraCycles;");
    emitter.writeLine("static constexpr u32 kCycleFlushThreshold = 2048;");
    emitter.writeLine("if (context.pendingCycles >= kCycleFlushThreshold)");
    emitter.openBlock("");
    emitter.writeLine("flushCycles(context);");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory32 — straightforward RAM read.
    emitter.writeLine("inline u32 readMemory32(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x3u) != 0)");
    emitter.writeLine(
        "raiseAddressError(context.system, runtime::Cop0::ExceptionCode::AddressErrorLoad,");
    emitter.writeLine(
        "                  context.system.debugOverlay().lastProgramCounter(), address);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return context.system.read<u32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory32(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x3u) != 0)");
    emitter.writeLine(
        "raiseAddressError(context.system, runtime::Cop0::ExceptionCode::AddressErrorStore,");
    emitter.writeLine(
        "                  context.system.debugOverlay().lastProgramCounter(), address);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.write<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory8 — unsigned byte read.
    emitter.writeLine("inline u32 readMemory8(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return static_cast<u32>(context.system.read<u8>(address));");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory8s — signed byte read (sign-extend to 32 bits).
    emitter.writeLine("inline u32 readMemory8s(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return static_cast<u32>(static_cast<s32>("
                      "static_cast<s8>(context.system.read<u8>(address))));");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory16 — unsigned halfword read.
    emitter.writeLine("inline u32 readMemory16(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x1u) != 0)");
    emitter.writeLine(
        "raiseAddressError(context.system, runtime::Cop0::ExceptionCode::AddressErrorLoad,");
    emitter.writeLine(
        "                  context.system.debugOverlay().lastProgramCounter(), address);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return static_cast<u32>(context.system.read<u16>(address));");
    emitter.closeBlock();
    emitter.writeBlank();

    // readMemory16s — signed halfword read (sign-extend to 32 bits).
    emitter.writeLine("inline u32 readMemory16s(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return static_cast<u32>(static_cast<s32>("
                      "static_cast<s16>(context.system.read<u16>(address))));");
    emitter.closeBlock();
    emitter.writeBlank();

    // writeMemory8 — byte store.
    emitter.writeLine(
        "inline void writeMemory8(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.write<u8>(address, static_cast<u8>(value & 0xFF));");
    emitter.closeBlock();
    emitter.writeBlank();

    // writeMemory16 — halfword store.
    emitter.writeLine(
        "inline void writeMemory16(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x1u) != 0)");
    emitter.writeLine(
        "raiseAddressError(context.system, runtime::Cop0::ExceptionCode::AddressErrorStore,");
    emitter.writeLine(
        "                  context.system.debugOverlay().lastProgramCounter(), address);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.write<u16>(address, static_cast<u16>(value & 0xFFFF));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio32(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("u32 value = context.system.readMmioExplicit<u32>(address);");
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
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return value;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMmio32(RecompilerContext& context, Address address, u32 value)");
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
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.writeMmioExplicit<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline const char* cpuExceptionCodeToString(u32 code)");
    emitter.openBlock("");
    emitter.openBlock("switch (code)");
    emitter.writeLine("case static_cast<u32>(runtime::Cop0::ExceptionCode::ReservedInstruction):");
    emitter.writeLine("return \"ReservedInstruction\";");
    emitter.writeLine("case static_cast<u32>(runtime::Cop0::ExceptionCode::CoprocessorUnusable):");
    emitter.writeLine("return \"CoprocessorUnusable\";");
    emitter.writeLine("case static_cast<u32>(runtime::Cop0::ExceptionCode::Syscall):");
    emitter.writeLine("return \"Syscall\";");
    emitter.writeLine("default:");
    emitter.writeLine("return \"Unknown\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "[[noreturn]] inline void raiseCpuException(RecompilerContext& context, u32 code, "
        "Address pc, bool inDelaySlot)");
    emitter.openBlock("");
    emitter.writeLine("context.system.cop0().exceptionEnter(");
    emitter.writeLine("    static_cast<runtime::Cop0::ExceptionCode>(code), pc, inDelaySlot);");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"CPU exception \" << cpuExceptionCodeToString(code)");
    emitter.writeLine("       << \" (code=\" << std::dec << code << \") at PC 0x\" << std::hex");
    emitter.writeLine("       << pc;");
    emitter.writeLine("if (inDelaySlot)");
    emitter.openBlock("");
    emitter.writeLine("stream << \" (delay slot)\";");
    emitter.closeBlock();
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void callSyscall(RecompilerContext& context, u32 code, Address pc)");
    emitter.openBlock("");
    emitter.openBlock("if (code == 0)");
    emitter.writeLine(
        "context.system.callBiosSyscall(code, context.regs.data(), context.regs.size());");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine(
        "context.system.cop0().exceptionEnter(runtime::Cop0::ExceptionCode::Syscall, pc, false);");
    emitter.writeLine(
        "const u32 status = context.system.cop0().mfc0(runtime::Cop0::RegisterIndex::Status);");
    emitter.writeLine(
        "const Address vectorBase = (status & (1u << 22)) != 0 ? 0xBFC00180u : 0x80000080u;");
    emitter.writeLine("const u32 vectorInstruction = context.system.read<u32>(vectorBase);");
    emitter.writeLine("const u32 vectorOp = (vectorInstruction >> 26) & 0x3Fu;");
    emitter.openBlock("if (vectorOp != 0x02u)");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"Unsupported exception vector instruction 0x\" << std::hex "
                      "<< vectorInstruction << \" at 0x\" << vectorBase;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeLine("const Address handlerTarget = ((vectorInstruction & 0x03FFFFFFu) << 2) | "
                      "(vectorBase & 0xF0000000u);");
    emitter.openBlock("if (!jumpRecompiledFunction(context, handlerTarget))");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"Exception vector target 0x\" << std::hex << handlerTarget "
                      "<< \" is not recompiled.\";");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void flushCycles(RecompilerContext& context)");
    emitter.openBlock("");
    emitter.writeLine("if (context.pendingCycles != 0)");
    emitter.openBlock("");
    emitter.writeLine("context.system.tickCpuCycles(context.pendingCycles);");
    emitter.writeLine("context.pendingCycles = 0;");
    emitter.writeLine("const auto interruptSavedRegs = context.regs;");
    emitter.writeLine("const u32 interruptSavedHi = context.hi;");
    emitter.writeLine("const u32 interruptSavedLo = context.lo;");
    emitter.writeLine("const u32 interruptCallbackCommitGeneration =");
    emitter.writeLine("    context.system.callbackContextCommitGeneration();");
    emitter.writeLine("context.system.serviceInterrupts();");
    emitter.writeLine("if (context.system.callbackContextCommitGeneration() !=");
    emitter.writeLine("    interruptCallbackCommitGeneration)");
    emitter.openBlock("");
    emitter.writeLine("// HookEntryInt resumes run inside callback context and");
    emitter.writeLine("// must not leak their restored registers into the");
    emitter.writeLine("// interrupted execution state.");
    emitter.writeLine("context.regs = interruptSavedRegs;");
    emitter.writeLine("context.hi = interruptSavedHi;");
    emitter.writeLine("context.lo = interruptSavedLo;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("struct CycleScope");
    emitter.openBlock("");
    emitter.writeLine("RecompilerContext& context;");
    emitter.writeLine("explicit CycleScope(RecompilerContext& ctx) : context(ctx) {}");
    emitter.writeLine(
        "~CycleScope() { try { flushCycles(context); } catch (...) {} }");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLine("inline void setProgramCounter(RecompilerContext& context, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("context.system.observeProgramCounter(pc);");
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
    emitter.writeLine("       << std::hex << pc << \"\\n\";");
    emitter.writeLine("stream << context.system.stallClassifier().classify();");
    emitter.writeLine("stream << context.system.diagTracepoints().formatRecentTraces();");
    emitter.writeLine("if (context.system.diagWatchpoints().eventCount() > 0)");
    emitter.openBlock("");
    emitter.writeLine("stream << context.system.diagWatchpoints().formatSummary();");
    emitter.closeBlock();
    emitter.writeLine("if (context.system.diagCdromBankTracer().isEnabled())");
    emitter.openBlock("");
    emitter.writeLine(
        "stream << runtime::DiagExplainerEngine::explainCdromBankSummary("
        "context.system.diagCdromBankTracer());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("stream << context.system.callbackTrace().formatRecentCallbacks();");
    emitter.writeLine("stream << context.system.formatHookEntryIntResumeTrace();");
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

} // namespace recompiler
} // namespace psxrecomp
