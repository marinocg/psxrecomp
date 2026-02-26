#include "codegen_runtime_helpers.h"

#include "cpp_emitter.h"

namespace psxrecomp
{
namespace recompiler
{

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

} // namespace recompiler
} // namespace psxrecomp
