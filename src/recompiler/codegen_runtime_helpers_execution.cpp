#include "codegen_runtime_helpers_internal.h"

#include "cpp_emitter.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRuntimeExecutionHelpers(CppEmitter& emitter)
{
    emitter.writeLine(
        "inline u32 resolveLoadMergeValue(const RecompilerContext& context, Register reg)");
    emitter.openBlock("");
    emitter.openBlock("if (reg == Registers::ZERO)");
    emitter.writeLine("return 0;");
    emitter.closeBlock();
    emitter.openBlock("if (context.pendingLoadValid && context.pendingLoadRegister == reg)");
    emitter.writeLine("return context.pendingLoadValue;");
    emitter.closeBlock();
    emitter.writeLine("return context.regs[reg];");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 gprWriteMask(Register reg)");
    emitter.openBlock("");
    emitter.openBlock("if (reg == Registers::ZERO)");
    emitter.writeLine("return 0;");
    emitter.closeBlock();
    emitter.writeLine("return 1u << static_cast<u32>(reg);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void stagePendingLoad(RecompilerContext& context, Register reg, u32 value)");
    emitter.openBlock("");
    emitter.openBlock("if (reg == Registers::ZERO)");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("context.stagedLoadValid = true;");
    emitter.writeLine("context.stagedLoadRegister = reg;");
    emitter.writeLine("context.stagedLoadValue = value;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline void commitPendingLoad(RecompilerContext& context)");
    emitter.openBlock("");
    emitter.openBlock("if (!context.pendingLoadValid)");
    emitter.writeLine("return;");
    emitter.closeBlock();
    emitter.writeLine("context.regs[context.pendingLoadRegister] = context.pendingLoadValue;");
    emitter.writeLine("context.pendingLoadValid = false;");
    emitter.writeLine("context.pendingLoadRegister = Registers::ZERO;");
    emitter.writeLine("context.pendingLoadValue = 0;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void finishLoadDelayCycle(RecompilerContext& context, u32 completedGprWriteMask)");
    emitter.openBlock("");
    emitter.openBlock("if (context.pendingLoadValid)");
    emitter.openBlock(
        "if ((completedGprWriteMask & gprWriteMask(context.pendingLoadRegister)) == 0)");
    emitter.writeLine("context.regs[context.pendingLoadRegister] = context.pendingLoadValue;");
    emitter.closeBlock();
    emitter.writeLine("context.pendingLoadValid = false;");
    emitter.writeLine("context.pendingLoadRegister = Registers::ZERO;");
    emitter.writeLine("context.pendingLoadValue = 0;");
    emitter.closeBlock();
    emitter.openBlock("if (context.stagedLoadValid)");
    emitter.writeLine("context.pendingLoadValid = true;");
    emitter.writeLine("context.pendingLoadRegister = context.stagedLoadRegister;");
    emitter.writeLine("context.pendingLoadValue = context.stagedLoadValue;");
    emitter.writeLine("context.stagedLoadValid = false;");
    emitter.writeLine("context.stagedLoadRegister = Registers::ZERO;");
    emitter.writeLine("context.stagedLoadValue = 0;");
    emitter.closeBlock();
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
    emitter.writeLine("case static_cast<u32>(runtime::Cop0::ExceptionCode::Breakpoint):");
    emitter.writeLine("return \"Breakpoint\";");
    emitter.writeLine("case static_cast<u32>(runtime::Cop0::ExceptionCode::ArithmeticOverflow):");
    emitter.writeLine("return \"ArithmeticOverflow\";");
    emitter.writeLine("default:");
    emitter.writeLine("return \"Unknown\";");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("[[noreturn]] inline void raiseCpuException(RecompilerContext& context, u32 "
                      "code, Address pc, bool inDelaySlot)");
    emitter.openBlock("");
    emitter.writeLine("commitPendingLoad(context);");
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
    emitter.writeLine("commitPendingLoad(context);");
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
    emitter.writeLine("stream << \"Unsupported exception vector instruction 0x\" << std::hex << "
                      "vectorInstruction << \" at 0x\" << vectorBase;");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeLine("const Address handlerTarget = ((vectorInstruction & 0x03FFFFFFu) << 2) | "
                      "(vectorBase & 0xF0000000u);");
    emitter.openBlock("if (!jumpRecompiledFunction(context, handlerTarget))");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"Exception vector target 0x\" << std::hex << handlerTarget << \" "
                      "is not recompiled.\";");
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
    emitter.writeLine("const bool interruptSavedPendingLoadValid = context.pendingLoadValid;");
    emitter.writeLine(
        "const Register interruptSavedPendingLoadRegister = context.pendingLoadRegister;");
    emitter.writeLine("const u32 interruptSavedPendingLoadValue = context.pendingLoadValue;");
    emitter.writeLine("const bool interruptSavedStagedLoadValid = context.stagedLoadValid;");
    emitter.writeLine(
        "const Register interruptSavedStagedLoadRegister = context.stagedLoadRegister;");
    emitter.writeLine("const u32 interruptSavedStagedLoadValue = context.stagedLoadValue;");
    emitter.writeLine("const u32 interruptCallbackCommitGeneration =");
    emitter.writeLine("    context.system.callbackContextCommitGeneration();");
    emitter.writeLine("context.system.serviceInterrupts();");
    emitter.writeLine("if (context.system.callbackContextCommitGeneration() !=");
    emitter.writeLine("    interruptCallbackCommitGeneration)");
    emitter.openBlock("");
    emitter.writeLine("context.regs = interruptSavedRegs;");
    emitter.writeLine("context.hi = interruptSavedHi;");
    emitter.writeLine("context.lo = interruptSavedLo;");
    emitter.writeLine("context.pendingLoadValid = interruptSavedPendingLoadValid;");
    emitter.writeLine("context.pendingLoadRegister = interruptSavedPendingLoadRegister;");
    emitter.writeLine("context.pendingLoadValue = interruptSavedPendingLoadValue;");
    emitter.writeLine("context.stagedLoadValid = interruptSavedStagedLoadValid;");
    emitter.writeLine("context.stagedLoadRegister = interruptSavedStagedLoadRegister;");
    emitter.writeLine("context.stagedLoadValue = interruptSavedStagedLoadValue;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("struct CycleScope");
    emitter.openBlock("");
    emitter.writeLine("RecompilerContext& context;");
    emitter.writeLine("explicit CycleScope(RecompilerContext& ctx) : context(ctx) {}");
    emitter.writeLine("~CycleScope() { try { flushCycles(context); } catch (...) {} }");
    emitter.closeBlock(";");
    emitter.writeBlank();
    emitter.writeLine("inline void setProgramCounter(RecompilerContext& context, Address pc)");
    emitter.openBlock("");
    emitter.writeLine("context.system.observeProgramCounter(pc, context.regs.data(),");
    emitter.writeLine("                                  context.regs.size());");
    emitter.writeBlank();
    emitter.writeLine("static uint64_t stepCount = 0;");
    emitter.writeLine("++stepCount;");
    emitter.writeLine("static const uint64_t maxSteps = []() -> uint64_t");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_MAX_STEPS\"))");
    emitter.openBlock("");
    emitter.writeLine("return std::strtoull(env, nullptr, 10);");
    emitter.closeBlock();
    emitter.writeLine("return 0;");
    emitter.closeBlock("();");
    emitter.writeLine("if (maxSteps > 0 && stepCount >= maxSteps)");
    emitter.openBlock("");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine(
        "stream << \"Step budget exhausted after \" << stepCount << \" steps at PC 0x\";");
    emitter.writeLine("stream << std::hex << pc << \"\\n\";");
    emitter.writeLine("stream << context.system.stallClassifier().classify();");
    emitter.writeLine("stream << context.system.diagTracepoints().formatRecentTraces();");
    emitter.writeLine("if (context.system.diagWatchpoints().eventCount() > 0)");
    emitter.openBlock("");
    emitter.writeLine("stream << context.system.diagWatchpoints().formatSummary();");
    emitter.closeBlock();
    emitter.writeLine("if (context.system.diagCdromBankTracer().isEnabled())");
    emitter.openBlock("");
    emitter.writeLine("stream << "
                      "runtime::DiagExplainerEngine::explainCdromBankSummary(context.system."
                      "diagCdromBankTracer());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine(
        "if "
        "(context.system.diagExplainers().isEnabled(runtime::ExplainerKind::CdromPhaseSummary))");
    emitter.openBlock("");
    emitter.writeLine("stream << context.system.cdrom().formatPhaseTraceSummary(32);");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if "
                      "(context.system.diagExplainers().isEnabled(runtime::ExplainerKind::"
                      "CdromXaClassification))");
    emitter.openBlock("");
    emitter.writeLine(
        "stream << "
        "runtime::DiagExplainerEngine::explainCdromXaClassification(context.system.cdrom());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if "
                      "(context.system.diagExplainers().isEnabled(runtime::ExplainerKind::"
                      "CdromPostStreamValidator))");
    emitter.openBlock("");
    emitter.writeLine(
        "stream << "
        "runtime::DiagExplainerEngine::explainCdromPostStreamValidator(context.system.cdrom());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if "
                      "(context.system.diagExplainers().isEnabled(runtime::ExplainerKind::"
                      "CdromCpuPayloadSummary))");
    emitter.openBlock("");
    emitter.writeLine(
        "stream << "
        "runtime::DiagExplainerEngine::explainCdromCpuPayloadSummary(context.system.cdrom());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if "
                      "(context.system.diagExplainers().isEnabled(runtime::ExplainerKind::"
                      "CdromIrqLifecycleSummary))");
    emitter.openBlock("");
    emitter.writeLine(
        "stream << "
        "runtime::DiagExplainerEngine::explainCdromIrqLifecycleSummary(context.system.cdrom());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if "
                      "(context.system.diagExplainers().isEnabled(runtime::ExplainerKind::"
                      "CdromLateBufferSummary))");
    emitter.openBlock("");
    emitter.writeLine("stream << "
                      "runtime::DiagExplainerEngine::explainCdromLateBufferSummary(context.system."
                      "diagCdromLateBufferTracker());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("if "
                      "(context.system.diagExplainers().isEnabled(runtime::ExplainerKind::"
                      "Rev2DecoderHandoffSummary))");
    emitter.openBlock("");
    emitter.writeLine("stream << "
                      "runtime::DiagExplainerEngine::explainRev2DecoderHandoffSummary(context."
                      "system.diagRev2DecoderHandoffTracker());");
    emitter.writeLine("stream << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("stream << context.system.callbackTrace().formatRecentCallbacks();");
    emitter.writeLine("stream << context.system.formatHookEntryIntResumeTrace();");
    emitter.writeLine("throw std::runtime_error(stream.str());");
    emitter.closeBlock();
    emitter.writeBlank();
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
    emitter.writeLine("++context.pendingCycles;");
    emitter.writeLine("static constexpr u32 kCycleFlushThreshold = 2048;");
    emitter.writeLine("if (context.pendingCycles >= kCycleFlushThreshold)");
    emitter.openBlock("");
    emitter.writeLine("flushCycles(context);");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline bool callIntrinsic(runtime::PsxSystem& system, Address address, "
                      "std::array<u32, Registers::NUM_REGISTERS>& regs)");
    emitter.openBlock("");
    emitter.writeLine("Address physical = address & 0x1FFFFFFF;");
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
    emitter.writeLine(
        "[[noreturn]] inline void triggerTrap(RecompilerContext& context, u32 code, Address pc,");
    emitter.writeLine("                                 bool inDelaySlot)");
    emitter.openBlock("");
    emitter.writeLine("commitPendingLoad(context);");
    emitter.writeLine(
        "context.system.cop0().exceptionEnter(runtime::Cop0::ExceptionCode::Breakpoint,");
    emitter.writeLine("                                   pc, inDelaySlot);");
    emitter.writeLine("std::ostringstream stream;");
    emitter.writeLine("stream << \"BREAK exception code=0x\" << std::hex << code");
    emitter.writeLine("       << \" at PC 0x\" << pc;");
    emitter.writeLine("if (inDelaySlot)");
    emitter.openBlock("");
    emitter.writeLine("stream << \" (delay slot)\";");
    emitter.closeBlock();
    emitter.writeLine("throw std::runtime_error(stream.str());");
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