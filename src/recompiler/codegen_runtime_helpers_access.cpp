#include "codegen_runtime_helpers_internal.h"

#include "cpp_emitter.h"

namespace psxrecomp
{
namespace recompiler
{

void emitRuntimeAccessHelpers(CppEmitter& emitter)
{
    emitter.writeLine("inline void commitPendingLoad(RecompilerContext& context);");
    emitter.writeLine("inline void flushCycles(RecompilerContext& context);");
    emitter.writeBlank();
    emitter.writeLine("inline bool strictAddrErrorsEnabled()");
    emitter.openBlock("");
    emitter.writeLine("static const bool strictAddrErrors = []()");
    emitter.openBlock("");
    emitter.writeLine("if (const char* env = std::getenv(\"PSXRECOMP_STRICT_ADDR_ERRORS\"))");
    emitter.openBlock("");
    emitter.writeLine("if (env[0] == '0')");
    emitter.openBlock("");
    emitter.writeLine("return false;");
    emitter.closeBlock();
    emitter.writeLine("if (env[0] == '1')");
    emitter.openBlock("");
    emitter.writeLine("return true;");
    emitter.closeBlock();
    emitter.closeBlock();
    emitter.writeLine("#if PSXRECOMP_STRICT_ADDR_ERRORS");
    emitter.writeLine("return true;");
    emitter.writeLine("#else");
    emitter.writeLine("return false;");
    emitter.writeLine("#endif");
    emitter.closeBlock("();");
    emitter.writeLine("return strictAddrErrors;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("[[noreturn]] inline void raiseAddressError(RecompilerContext& context,");
    emitter.writeLine(
        "                                               runtime::Cop0::ExceptionCode code,");
    emitter.writeLine(
        "                                               Address pc, Address badVaddr,");
    emitter.writeLine("                                               bool inDelaySlot)");
    emitter.openBlock("");
    emitter.writeLine("commitPendingLoad(context);");
    emitter.writeLine("context.system.cop0().exceptionEnter(code, pc, inDelaySlot, badVaddr);");
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

    emitter.writeLine("inline u32 readMemory32(RecompilerContext& context, Address address,");
    emitter.writeLine("                        Address pc, bool inDelaySlot)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x3u) != 0)");
    emitter.writeLine("raiseAddressError(context, runtime::Cop0::ExceptionCode::AddressErrorLoad,");
    emitter.writeLine("                  pc, address, inDelaySlot);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return context.system.read<u32>(address);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMemory32(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return readMemory32(context, address,");
    emitter.writeLine("                    "
                      "context.system.debugOverlay().lastArchitecturalProgramCounter(), false);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory32(RecompilerContext& context, Address address, u32 value,");
    emitter.writeLine("                          Address pc, bool inDelaySlot)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x3u) != 0)");
    emitter.writeLine(
        "raiseAddressError(context, runtime::Cop0::ExceptionCode::AddressErrorStore,");
    emitter.writeLine("                  pc, address, inDelaySlot);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.write<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory32(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("writeMemory32(context, address, value,");
    emitter.writeLine(
        "              context.system.debugOverlay().lastArchitecturalProgramCounter(), false);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMemory8(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return static_cast<u32>(context.system.read<u8>(address));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMemory8s(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine(
        "return "
        "static_cast<u32>(static_cast<s32>(static_cast<s8>(context.system.read<u8>(address))));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMemory16(RecompilerContext& context, Address address,");
    emitter.writeLine("                        Address pc, bool inDelaySlot)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x1u) != 0)");
    emitter.writeLine("raiseAddressError(context, runtime::Cop0::ExceptionCode::AddressErrorLoad,");
    emitter.writeLine("                  pc, address, inDelaySlot);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return static_cast<u32>(context.system.read<u16>(address));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMemory16(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return readMemory16(context, address,");
    emitter.writeLine("                    "
                      "context.system.debugOverlay().lastArchitecturalProgramCounter(), false);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMemory16s(RecompilerContext& context, Address address,");
    emitter.writeLine("                         Address pc, bool inDelaySlot)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x1u) != 0)");
    emitter.writeLine("raiseAddressError(context, runtime::Cop0::ExceptionCode::AddressErrorLoad,");
    emitter.writeLine("                  pc, address, inDelaySlot);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine(
        "return "
        "static_cast<u32>(static_cast<s32>(static_cast<s16>(context.system.read<u16>(address))));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMemory16s(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("return readMemory16s(context, address,");
    emitter.writeLine("                     "
                      "context.system.debugOverlay().lastArchitecturalProgramCounter(), false);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline u32 readMemoryLwl(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return runtime::loadWordLeft(context.system, address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline u32 readMemoryLwr(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return runtime::loadWordRight(context.system, address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory8(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.write<u8>(address, static_cast<u8>(value & 0xFF));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory16(RecompilerContext& context, Address address, u32 value,");
    emitter.writeLine("                          Address pc, bool inDelaySlot)");
    emitter.openBlock("");
    emitter.openBlock("if (strictAddrErrorsEnabled() && (address & 0x1u) != 0)");
    emitter.writeLine(
        "raiseAddressError(context, runtime::Cop0::ExceptionCode::AddressErrorStore,");
    emitter.writeLine("                  pc, address, inDelaySlot);");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.write<u16>(address, static_cast<u16>(value & 0xFFFF));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemory16(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("writeMemory16(context, address, value,");
    emitter.writeLine(
        "              context.system.debugOverlay().lastArchitecturalProgramCounter(), false);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemorySwl(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("runtime::storeWordLeft(context.system, address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMemorySwr(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("runtime::storeWordRight(context.system, address, value);");
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
    emitter.writeLine("std::cerr << \"[mmio] read32 0x\" << std::hex << address << \" = 0x\" << "
                      "value << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return value;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio8(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine(
        "u32 value = static_cast<u32>(context.system.readMmioExplicit<u8>(address));");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return value;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio8s(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("u32 value = "
                      "static_cast<u32>(static_cast<s32>(static_cast<s8>(context.system."
                      "readMmioExplicit<u8>(address))));");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return value;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio16(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine(
        "u32 value = static_cast<u32>(context.system.readMmioExplicit<u16>(address));");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, false);");
    emitter.writeLine("return value;");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine("inline u32 readMmio16s(RecompilerContext& context, Address address)");
    emitter.openBlock("");
    emitter.writeLine("u32 value = "
                      "static_cast<u32>(static_cast<s32>(static_cast<s16>(context.system."
                      "readMmioExplicit<u16>(address))));");
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
    emitter.writeLine("std::cerr << \"[mmio] write32 0x\" << std::hex << address << \" = 0x\" << "
                      "value << \"\\n\";");
    emitter.closeBlock();
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine("context.system.writeMmioExplicit<u32>(address, value);");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMmio16(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine(
        "context.system.writeMmioExplicit<u16>(address, static_cast<u16>(value & 0xFFFFu));");
    emitter.closeBlock();
    emitter.writeBlank();
    emitter.writeLine(
        "inline void writeMmio8(RecompilerContext& context, Address address, u32 value)");
    emitter.openBlock("");
    emitter.writeLine("accountCpuDataAccessCycles(context, address, true);");
    emitter.writeLine(
        "context.system.writeMmioExplicit<u8>(address, static_cast<u8>(value & 0xFFu));");
    emitter.closeBlock();
    emitter.writeBlank();
}

} // namespace recompiler
} // namespace psxrecomp