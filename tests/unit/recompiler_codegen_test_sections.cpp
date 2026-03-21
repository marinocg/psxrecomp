#include "recompiler_codegen_test_sections.h"

#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

void runCodegenOverlapTest(psxrecomp::recompiler::CodeGenerator& generator)
{
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;

    Program overlappingProgram;
    Builder overlappingBuilder(overlappingProgram);
    auto& outerFunction = overlappingBuilder.createFunction("outer_func", 0x80010000);
    auto& outerBlock = overlappingBuilder.createBlock(outerFunction, "entry");
    outerBlock.instructions.push_back(
        overlappingBuilder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(1)},
                                           {overlappingBuilder.createTemporary()}, 0x80010000));
    outerBlock.instructions.push_back(
        overlappingBuilder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(2)},
                                           {overlappingBuilder.createTemporary()}, 0x80010010));
    outerBlock.instructions.push_back(overlappingBuilder.makeInstruction(Opcode::RETURN, {}, {}));

    auto& innerFunction = overlappingBuilder.createFunction("inner_func", 0x80010008);
    auto& innerBlock = overlappingBuilder.createBlock(innerFunction, "entry");
    innerBlock.instructions.push_back(
        overlappingBuilder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(3)},
                                           {overlappingBuilder.createTemporary()}, 0x80010008));
    innerBlock.instructions.push_back(overlappingBuilder.makeInstruction(Opcode::RETURN, {}, {}));

    [[maybe_unused]] bool overlapDetected = false;
    try
    {
        (void)generator.generateSource(overlappingProgram, "module_overlap");
    }
    catch (const std::runtime_error& error)
    {
        overlapDetected = std::string(error.what()).find("Overlapping function dispatch ranges") !=
                          std::string::npos;
    }
    assert(overlapDetected);
}

void runCodegenCompileHarnessTest(psxrecomp::recompiler::CodeGenerator& generator,
                                  const std::string& header, const std::string& source,
                                  const std::filesystem::path& outputDir,
                                  const std::filesystem::path& repoRoot,
                                  const std::string& compiler)
{
    auto quote = [](const std::filesystem::path& path)
    { return std::string("\"") + path.string() + "\""; };

    const auto includeDir = outputDir / "include/psxrecomp/runtime";
    std::filesystem::create_directories(includeDir);

    const auto headerPath = outputDir / "module.h";
    const auto sourcePath = outputDir / "module.cpp";
    const auto harnessPath = outputDir / "harness.cpp";
    const auto runtimeHeaderPath = includeDir / "psx_system.h";
    const auto unalignedHeaderPath = includeDir / "mips_unaligned_access.h";
    const auto exePath = outputDir / "module_test";

    std::ofstream headerFile(headerPath);
    headerFile << header;
    headerFile.close();
    std::ofstream sourceFile(sourcePath);
    sourceFile << source;
    sourceFile.close();

    std::ofstream runtimeHeader(runtimeHeaderPath);
    runtimeHeader << "#pragma once\n";
    runtimeHeader << "#include \"psxrecomp/types.h\"\n";
    runtimeHeader << "#include <array>\n";
    runtimeHeader << "#include <cstddef>\n";
    runtimeHeader << "#include <cstring>\n";
    runtimeHeader << "#include <functional>\n";
    runtimeHeader << "#include <optional>\n";
    runtimeHeader << "#include <cstddef>\n";
    runtimeHeader << "#include <string>\n";
    runtimeHeader << "#include <type_traits>\n";
    runtimeHeader << "#include <vector>\n";
    runtimeHeader << "namespace psxrecomp { namespace runtime {\n";
    runtimeHeader << "class RuntimeDebugOverlay {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    void setLastProgramCounter(u32 pc) { m_pc = pc; }\n";
    runtimeHeader << "    u32 lastProgramCounter() const { return m_pc; }\n";
    runtimeHeader << "    std::string renderText() const { return {}; }\n";
    runtimeHeader << "  private:\n";
    runtimeHeader << "    u32 m_pc = 0;\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class Cop0 {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader
        << "    enum RegisterIndex : u8 { BadVAddr = 8, Status = 12, Cause = 13, Epc = 14 };"
        << "\n";
    runtimeHeader << "    enum class ExceptionCode : u32 {\n";
    runtimeHeader << "      AddressErrorLoad = 4,\n";
    runtimeHeader << "      AddressErrorStore = 5,\n";
    runtimeHeader << "      Syscall = 8,\n";
    runtimeHeader << "      ReservedInstruction = 10,\n";
    runtimeHeader << "      CoprocessorUnusable = 11,\n";
    runtimeHeader << "      ArithmeticOverflow = 12\n";
    runtimeHeader << "    };\n";
    runtimeHeader << "    u32 mfc0(u8 rd) const { return m_regs[rd]; }\n";
    runtimeHeader << "    void mtc0(u8 rd, u32 value) { m_regs[rd] = value; }\n";
    runtimeHeader << "    void exceptionEnter(ExceptionCode code, u32 pc, bool inDelaySlot,\n";
    runtimeHeader << "                        std::optional<u32> badVaddr = std::nullopt) {\n";
    runtimeHeader << "      const u32 status = m_regs[Status];\n";
    runtimeHeader
        << "      m_regs[Status] = (status & ~0x3Fu) | (((status & 0x3Fu) << 2) & 0x3Fu);\n";
    runtimeHeader << "      u32 cause = m_regs[Cause];\n";
    runtimeHeader << "      cause &= ~(0x7Cu | 0x80000000u);\n";
    runtimeHeader << "      cause |= (static_cast<u32>(code) & 0x1Fu) << 2;\n";
    runtimeHeader << "      if (inDelaySlot) {\n";
    runtimeHeader << "        cause |= 0x80000000u;\n";
    runtimeHeader << "      }\n";
    runtimeHeader << "      m_regs[Cause] = cause;\n";
    runtimeHeader << "      m_regs[Epc] = inDelaySlot ? (pc - 4u) : pc;\n";
    runtimeHeader << "      if (badVaddr.has_value()) {\n";
    runtimeHeader << "        m_regs[BadVAddr] = *badVaddr;\n";
    runtimeHeader << "      }\n";
    runtimeHeader << "    }\n";
    runtimeHeader
        << "    bool cop2Enabled() const { return (m_regs[Status] & (1u << 30)) != 0u; }\n";
    runtimeHeader << "    void rfe() {\n";
    runtimeHeader << "      const u32 status = m_regs[Status];\n";
    runtimeHeader << "      m_regs[Status] = (status & ~0x3Fu) | ((status & 0x3Fu) >> 2);\n";
    runtimeHeader << "    }\n";
    runtimeHeader << "  private:\n";
    runtimeHeader << "    std::array<u32, 32> m_regs{};\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class Gte {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    u32 mfc2(u8 rd) const { return m_data[rd & 31u]; }\n";
    runtimeHeader << "    void mtc2(u8 rd, u32 value) { m_data[rd & 31u] = value; }\n";
    runtimeHeader << "    u32 cfc2(u8 rd) const { return m_ctrl[rd & 31u]; }\n";
    runtimeHeader << "    void ctc2(u8 rd, u32 value) { m_ctrl[rd & 31u] = value; }\n";
    runtimeHeader << "  private:\n";
    runtimeHeader << "    std::array<u32, 32> m_data{};\n";
    runtimeHeader << "    std::array<u32, 32> m_ctrl{};\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class StallClassifier {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    void recordPc(Address) {}\n";
    runtimeHeader << "    std::string classify() const { return {}; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class DiagTracepointEngine {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    std::string formatRecentTraces() const { return {}; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class DiagWatchpointEngine {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    std::size_t eventCount() const { return 0; }\n";
    runtimeHeader << "    std::string formatSummary() const { return {}; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class DiagCdromLateBufferTracker {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    bool isEnabled() const { return false; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "enum class ExplainerKind : unsigned char {\n";
    runtimeHeader << "    Gpustat, CdromIrq, IrqController, DmaChannel,\n";
    runtimeHeader << "    CdromBankSummary, CdromPhaseSummary, CdromXaClassification,\n";
    runtimeHeader << "    CdromPostStreamValidator, CdromCpuPayloadSummary,\n";
    runtimeHeader << "    CdromIrqLifecycleSummary, CdromLateBufferSummary,\n";
    runtimeHeader << "    Rev2DecoderHandoffSummary\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class DiagRev2DecoderHandoffTracker {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    bool isEnabled() const { return false; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class Cdrom {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader
        << "    std::string formatPhaseTraceSummary(std::size_t = 10) const { return {}; }\n";
    runtimeHeader << "    std::string formatXaClassificationSummary() const { return {}; }\n";
    runtimeHeader << "    std::string formatPostStreamSummary() const { return {}; }\n";
    runtimeHeader << "    std::string formatCpuPayloadSummary() const { return {}; }\n";
    runtimeHeader << "    std::string formatIrqLifecycleSummary() const { return {}; }\n";
    runtimeHeader << "    std::string formatAdpbusyLifecycleSummary() const { return {}; }\n";
    runtimeHeader << "    void noteIrqCallbackDispatch(unsigned char, bool) {}\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class DiagCdromBankTracer {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    bool isEnabled() const { return false; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class DiagExplainerEngine {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    bool isEnabled(ExplainerKind) const { return false; }\n";
    runtimeHeader << "    static std::string explainCdromBankSummary(const DiagCdromBankTracer&)"
                     " { return {}; }\n";
    runtimeHeader
        << "    static std::string explainCdromXaClassification(const Cdrom&) { return {}; }\n";
    runtimeHeader
        << "    static std::string explainCdromPostStreamValidator(const Cdrom&) { return {}; }\n";
    runtimeHeader
        << "    static std::string explainCdromCpuPayloadSummary(const Cdrom&) { return {}; }\n";
    runtimeHeader
        << "    static std::string explainCdromIrqLifecycleSummary(const Cdrom&) { return {}; }\n";
    runtimeHeader << "    static std::string explainCdromLateBufferSummary(const "
                     "DiagCdromLateBufferTracker&) { return {}; }\n";
    runtimeHeader << "    static std::string explainRev2DecoderHandoffSummary(const "
                     "DiagRev2DecoderHandoffTracker&) { return {}; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class CallbackTraceEngine {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    bool hasActiveInvocation() const { return false; }\n";
    runtimeHeader << "    void setActiveInvocationStackPointer(Address) {}\n";
    runtimeHeader << "    void recordRamWrite(Address, u8, u32, u32) {}\n";
    runtimeHeader << "    void recordCommittedRegisterDelta(const std::array<u32, 32>&,\n";
    runtimeHeader << "                                      const std::array<u32, 32>&,\n";
    runtimeHeader << "                                      u32, u32, u32, u32, bool) {}\n";
    runtimeHeader << "    std::string formatRecentCallbacks() const { return {}; }\n";
    runtimeHeader << "};\n";
    runtimeHeader << "class PsxSystem {\n";
    runtimeHeader << "  public:\n";
    runtimeHeader << "    struct DiscSwapInfo {\n";
    runtimeHeader << "      struct DiscEntry {\n";
    runtimeHeader << "        u32 index = 0;\n";
    runtimeHeader << "        std::string label;\n";
    runtimeHeader << "        std::string path;\n";
    runtimeHeader << "      };\n";
    runtimeHeader << "      std::string setName;\n";
    runtimeHeader << "      u32 activeDiscIndex = 0;\n";
    runtimeHeader << "      std::vector<DiscEntry> discs;\n";
    runtimeHeader << "    };\n";
    runtimeHeader << "    explicit PsxSystem(u8* ram) : m_ram(ram) {}\n";
    runtimeHeader << "    u8* getRam() { return m_ram; }\n";
    runtimeHeader << "    template <typename T> T read(Address address) {\n";
    runtimeHeader << "      T value{};\n";
    runtimeHeader << "      const std::size_t offset = static_cast<std::size_t>(address);\n";
    runtimeHeader << "      if (offset + sizeof(T) <= psxrecomp::MemoryMap::RAM_SIZE) {\n";
    runtimeHeader << "        std::memcpy(&value, m_ram + offset, sizeof(T));\n";
    runtimeHeader << "      }\n";
    runtimeHeader << "      return value;\n";
    runtimeHeader << "    }\n";
    runtimeHeader << "    template <typename T> void write(Address address, T value) {\n";
    runtimeHeader << "      const std::size_t offset = static_cast<std::size_t>(address);\n";
    runtimeHeader << "      if (offset + sizeof(T) <= psxrecomp::MemoryMap::RAM_SIZE) {\n";
    runtimeHeader << "        std::memcpy(m_ram + offset, &value, sizeof(T));\n";
    runtimeHeader << "      }\n";
    runtimeHeader << "    }\n";
    runtimeHeader << "    template <typename T> T readMmioExplicit(Address) { return {}; }\n";
    runtimeHeader << "    template <typename T> void writeMmioExplicit(Address, T) {}\n";
    runtimeHeader << "    void callBiosSyscall(u32, u32*, std::size_t) {}\n";
    runtimeHeader << "    void callBiosVector(u32, u32*, std::size_t) {}\n";
    runtimeHeader << "    void callGpuIntrinsic(Address) {}\n";
    runtimeHeader << "    void callSpuIntrinsic(Address) {}\n";
    runtimeHeader << "    void callCdromIntrinsic(Address) {}\n";
    runtimeHeader << "    void setDiscSwapInfo(const DiscSwapInfo&) {}\n";
    runtimeHeader << "    void tickCpuCycles(u32) {}\n";
    runtimeHeader << "    u32 frameCount() const { return 0; }\n";
    runtimeHeader << "    u32 advanceFrame() { return 0; }\n";
    runtimeHeader
        << "    void observeProgramCounter(Address pc, const u32* = nullptr, std::size_t = 0) { "
           "m_overlay.setLastProgramCounter(pc); m_stallClassifier.recordPc(pc); }\n";
    runtimeHeader
        << "    void setLastResumeAddress(Address address) { m_lastResumeAddress = address; }\n";
    runtimeHeader << "    Address lastResumeAddress() const { return m_lastResumeAddress; }\n";
    runtimeHeader << "    std::string describeBiosCdromState() const { return {}; }\n";
    runtimeHeader << "    void serviceInterrupts() {}\n";
    runtimeHeader << "    void validateAllocatorHeapCallBoundary(Address) {}\n";
    runtimeHeader << "    enum class CallbackContextDisposition { RestoreSaved, CommitMutated };\n";
    runtimeHeader
        << "    CallbackContextDisposition consumePendingCallbackRegisters(std::array<u32, 32>&) { "
           "return CallbackContextDisposition::RestoreSaved; }\n";
    runtimeHeader << "    u32 callbackContextCommitGeneration() const { return 0; }\n";
    runtimeHeader << "    void setCallbackInvoker(std::function<u32(u32)>) {}\n";
    runtimeHeader << "    RuntimeDebugOverlay& debugOverlay() { return m_overlay; }\n";
    runtimeHeader << "    Cop0& cop0() { return m_cop0; }\n";
    runtimeHeader << "    Gte& gte() { return m_gte; }\n";
    runtimeHeader << "    StallClassifier& stallClassifier() { return m_stallClassifier; }\n";
    runtimeHeader << "    DiagTracepointEngine& diagTracepoints() { return m_diagTracepoints; }\n";
    runtimeHeader << "    DiagWatchpointEngine& diagWatchpoints() { return m_diagWatchpoints; }\n";
    runtimeHeader
        << "    DiagCdromBankTracer& diagCdromBankTracer() { return m_diagCdromBankTracer; }\n";
    runtimeHeader << "    DiagCdromLateBufferTracker& diagCdromLateBufferTracker() { return "
                     "m_diagCdromLateBufferTracker; }\n";
    runtimeHeader << "    DiagRev2DecoderHandoffTracker& diagRev2DecoderHandoffTracker() { return "
                     "m_diagRev2DecoderHandoffTracker; }\n";
    runtimeHeader << "    DiagExplainerEngine& diagExplainers() { return m_diagExplainers; }\n";
    runtimeHeader << "    Cdrom& cdrom() { return m_cdrom; }\n";
    runtimeHeader << "    CallbackTraceEngine& callbackTrace() { return m_callbackTrace; }\n";
    runtimeHeader << "    std::string formatHookEntryIntResumeTrace() const { return {}; }\n";
    runtimeHeader << "  private:\n";
    runtimeHeader << "    u8* m_ram;\n";
    runtimeHeader << "    RuntimeDebugOverlay m_overlay;\n";
    runtimeHeader << "    Cop0 m_cop0;\n";
    runtimeHeader << "    Gte m_gte;\n";
    runtimeHeader << "    StallClassifier m_stallClassifier;\n";
    runtimeHeader << "    DiagTracepointEngine m_diagTracepoints;\n";
    runtimeHeader << "    DiagWatchpointEngine m_diagWatchpoints;\n";
    runtimeHeader << "    DiagCdromBankTracer m_diagCdromBankTracer;\n";
    runtimeHeader << "    DiagCdromLateBufferTracker m_diagCdromLateBufferTracker;\n";
    runtimeHeader << "    DiagRev2DecoderHandoffTracker m_diagRev2DecoderHandoffTracker;\n";
    runtimeHeader << "    DiagExplainerEngine m_diagExplainers;\n";
    runtimeHeader << "    Cdrom m_cdrom;\n";
    runtimeHeader << "    CallbackTraceEngine m_callbackTrace;\n";
    runtimeHeader << "    Address m_lastResumeAddress = 0;\n";
    runtimeHeader << "};\n";
    runtimeHeader << "} }\n";
    runtimeHeader.close();

    std::ofstream unalignedHeader(unalignedHeaderPath);
    unalignedHeader << "#pragma once\n";
    unalignedHeader << "#include \"psxrecomp/runtime/psx_system.h\"\n";
    unalignedHeader << "namespace psxrecomp { namespace runtime {\n";
    unalignedHeader
        << "inline u32 loadWordLeft(PsxSystem&, Address, u32 value) { return value; }\n";
    unalignedHeader
        << "inline u32 loadWordRight(PsxSystem&, Address, u32 value) { return value; }\n";
    unalignedHeader << "inline void storeWordLeft(PsxSystem&, Address, u32) {}\n";
    unalignedHeader << "inline void storeWordRight(PsxSystem&, Address, u32) {}\n";
    unalignedHeader << "} }\n";
    unalignedHeader.close();

    std::ofstream harnessFile(harnessPath);
    harnessFile << "#include \"module.h\"\n";
    harnessFile << "#include <array>\n";
    harnessFile << "#include <stdexcept>\n";
    harnessFile << "int main() {\n";
    harnessFile << "  std::array<psxrecomp::u8, psxrecomp::MemoryMap::RAM_SIZE> ram{};\n";
    harnessFile << "  psxrecomp::runtime::PsxSystem system(ram.data());\n";
    harnessFile << "  psxrecomp::recompiler::RecompiledModule::initMemory(system);\n";
    harnessFile << "  bool threw = false;\n";
    harnessFile << "  try {\n";
    harnessFile << "    psxrecomp::recompiler::RecompiledModule::run(system);\n";
    harnessFile << "  } catch (const std::runtime_error&) {\n";
    harnessFile << "    threw = true;\n";
    harnessFile << "  }\n";
    harnessFile << "#if PSXRECOMP_STRICT_ADDR_ERRORS\n";
    harnessFile << "  if (!threw) { return 1; }\n";
    harnessFile << "  const auto bad = system.cop0().mfc0(psxrecomp::runtime::Cop0::BadVAddr);\n";
    harnessFile << "  const auto cause = system.cop0().mfc0(psxrecomp::runtime::Cop0::Cause);\n";
    harnessFile << "  if (bad != 0x80010012u) { return 2; }\n";
    harnessFile << "  if ((cause & 0x7Cu) !=\n";
    harnessFile << "      (static_cast<psxrecomp::u32>(\n";
    harnessFile << "           psxrecomp::runtime::Cop0::ExceptionCode::AddressErrorLoad)\n";
    harnessFile << "       << 2)) {\n";
    harnessFile << "    return 3;\n";
    harnessFile << "  }\n";
    harnessFile << "#else\n";
    harnessFile << "  if (threw) { return 4; }\n";
    harnessFile << "#endif\n";
    harnessFile << "  return 0;\n";
    harnessFile << "}\n";
    harnessFile.close();

    const std::string baseCompileCommand = quote(compiler) + " -std=c++17 -I" +
                                           quote(outputDir / "include") + " -I" +
                                           quote(repoRoot / "include") + " -I" + quote(outputDir) +
                                           " " + quote(sourcePath) + " " + quote(harnessPath);
    std::string command = baseCompileCommand + " -o " + quote(exePath);
    int compileStatus = std::system(command.c_str());
    if (compileStatus != 0)
    {
        std::cerr << "Compile failed with status: " << compileStatus << "\n";
    }
    assert(compileStatus == 0);

    const auto strictExePath = outputDir / "harness_strict.out";
    std::string strictCommand =
        baseCompileCommand + " -DPSXRECOMP_STRICT_ADDR_ERRORS=1 -o " + quote(strictExePath);
    int strictCompileStatus = std::system(strictCommand.c_str());
    if (strictCompileStatus != 0)
    {
        std::cerr << "Strict compile failed with status: " << strictCompileStatus << "\n";
    }
    assert(strictCompileStatus == 0);

    std::string runCommand = quote(exePath);
    int runStatus = std::system(runCommand.c_str());
    if (runStatus != 0)
    {
        std::cerr << "Run failed with status: " << runStatus << "\n";
    }
    assert(runStatus == 0);

    std::string strictRunCommand = quote(strictExePath);
    int strictRunStatus = std::system(strictRunCommand.c_str());
    if (strictRunStatus != 0)
    {
        std::cerr << "Strict run failed with status: " << strictRunStatus << "\n";
    }
    assert(strictRunStatus == 0);

    runCodegenCop2GuardTest(generator, outputDir, repoRoot, compiler);
}

void runCodegenCop2GuardTest(psxrecomp::recompiler::CodeGenerator& generator,
                             const std::filesystem::path& outputDir,
                             const std::filesystem::path& repoRoot, const std::string& compiler)
{
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    auto quote = [](const std::filesystem::path& path)
    { return std::string("\"") + path.string() + "\""; };

    Program cop2Program;
    Builder cop2Builder(cop2Program);
    auto& cop2Function = cop2Builder.createFunction("cop2_guard_func", 0x80020000);
    auto& cop2Entry = cop2Builder.createBlock(cop2Function, "entry");
    cop2Entry.instructions.push_back(cop2Builder.makeInstruction(
        Opcode::MOVE, {Value::makeImmediate(0x12345678)}, {Value::makeRegister(2)}, 0x80020000));
    cop2Entry.instructions.push_back(cop2Builder.makeInstruction(
        Opcode::GTE_MTC2, {Value::makeImmediate(6), Value::makeRegister(2)}, {}, 0x80020004));
    cop2Entry.instructions.push_back(cop2Builder.makeInstruction(
        Opcode::GTE_MFC2, {Value::makeImmediate(6)}, {Value::makeRegister(3)}, 0x80020008));
    cop2Entry.instructions.push_back(cop2Builder.makeInstruction(
        Opcode::GTE_LWC2, {Value::makeImmediate(7), Value::makeAddress(0x100)}, {}, 0x8002000C));
    cop2Entry.instructions.push_back(cop2Builder.makeInstruction(
        Opcode::GTE_SWC2, {Value::makeImmediate(6), Value::makeAddress(0x104)}, {}, 0x80020010));
    cop2Entry.instructions.push_back(cop2Builder.makeInstruction(
        Opcode::GTE_SWC2, {Value::makeImmediate(7), Value::makeAddress(0x108)}, {}, 0x80020014));
    cop2Entry.instructions.push_back(
        cop2Builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80020018));

    const std::string cop2Header = generator.generateHeader(cop2Program, "cop2_module");
    const std::string cop2Source = generator.generateSource(cop2Program, "cop2_module");

    const auto cop2HeaderPath = outputDir / "cop2_module.h";
    const auto cop2SourcePath = outputDir / "cop2_module.cpp";
    const auto cop2HarnessPath = outputDir / "cop2_harness.cpp";
    const auto cop2ExePath = outputDir / "cop2_test";

    std::ofstream cop2HeaderFile(cop2HeaderPath);
    cop2HeaderFile << cop2Header;
    cop2HeaderFile.close();

    std::ofstream cop2SourceFile(cop2SourcePath);
    cop2SourceFile << cop2Source;
    cop2SourceFile.close();

    std::ofstream cop2HarnessFile(cop2HarnessPath);
    cop2HarnessFile << "#include \"cop2_module.h\"\n";
    cop2HarnessFile << "#include <array>\n";
    cop2HarnessFile << "#include <cstring>\n";
    cop2HarnessFile << "#include <stdexcept>\n";
    cop2HarnessFile << "int main() {\n";
    cop2HarnessFile << "  std::array<psxrecomp::u8, psxrecomp::MemoryMap::RAM_SIZE> ram{};\n";
    cop2HarnessFile << "  const psxrecomp::u32 sourceWord = 0x89ABCDEFu;\n";
    cop2HarnessFile << "  std::memcpy(ram.data() + 0x100, &sourceWord, sizeof(sourceWord));\n";
    cop2HarnessFile << "  psxrecomp::runtime::PsxSystem disabledSystem(ram.data());\n";
    cop2HarnessFile << "  bool disabledThrew = false;\n";
    cop2HarnessFile << "  try {\n";
    cop2HarnessFile << "    psxrecomp::recompiler::RecompiledModule::run(disabledSystem);\n";
    cop2HarnessFile << "  } catch (const std::runtime_error&) {\n";
    cop2HarnessFile << "    disabledThrew = true;\n";
    cop2HarnessFile << "  }\n";
    cop2HarnessFile << "  if (!disabledThrew) { return 1; }\n";
    cop2HarnessFile << "  const auto disabledCause = disabledSystem.cop0().mfc0(\n";
    cop2HarnessFile << "      psxrecomp::runtime::Cop0::Cause);\n";
    cop2HarnessFile << "  if ((disabledCause & 0x7Cu) !=\n";
    cop2HarnessFile << "      (static_cast<psxrecomp::u32>(\n";
    cop2HarnessFile << "           psxrecomp::runtime::Cop0::ExceptionCode::CoprocessorUnusable)\n";
    cop2HarnessFile << "       << 2)) {\n";
    cop2HarnessFile << "    return 2;\n";
    cop2HarnessFile << "  }\n";
    cop2HarnessFile << "  if (disabledSystem.gte().mfc2(6) != 0u) { return 3; }\n";
    cop2HarnessFile << "  if (disabledSystem.gte().mfc2(7) != 0u) { return 4; }\n";
    cop2HarnessFile << "  psxrecomp::runtime::PsxSystem enabledSystem(ram.data());\n";
    cop2HarnessFile << "  enabledSystem.cop0().mtc0(psxrecomp::runtime::Cop0::Status, 1u << 30);\n";
    cop2HarnessFile << "  try {\n";
    cop2HarnessFile << "    psxrecomp::recompiler::RecompiledModule::run(enabledSystem);\n";
    cop2HarnessFile << "  } catch (const std::runtime_error&) {\n";
    cop2HarnessFile << "    return 5;\n";
    cop2HarnessFile << "  }\n";
    cop2HarnessFile << "  psxrecomp::u32 storedReg6 = 0;\n";
    cop2HarnessFile << "  psxrecomp::u32 storedReg7 = 0;\n";
    cop2HarnessFile << "  std::memcpy(&storedReg6, ram.data() + 0x104, sizeof(storedReg6));\n";
    cop2HarnessFile << "  std::memcpy(&storedReg7, ram.data() + 0x108, sizeof(storedReg7));\n";
    cop2HarnessFile << "  if (enabledSystem.gte().mfc2(6) != 0x12345678u) { return 6; }\n";
    cop2HarnessFile << "  if (enabledSystem.gte().mfc2(7) != sourceWord) { return 7; }\n";
    cop2HarnessFile << "  if (storedReg6 != 0x12345678u) { return 8; }\n";
    cop2HarnessFile << "  if (storedReg7 != sourceWord) { return 9; }\n";
    cop2HarnessFile << "  return 0;\n";
    cop2HarnessFile << "}\n";
    cop2HarnessFile.close();

    const std::string cop2CompileCommand =
        quote(compiler) + " -std=c++17 -I" + quote(outputDir / "include") + " -I" +
        quote(repoRoot / "include") + " -I" + quote(outputDir) + " " + quote(cop2SourcePath) + " " +
        quote(cop2HarnessPath) + " -o " + quote(cop2ExePath);
    int cop2CompileStatus = std::system(cop2CompileCommand.c_str());
    if (cop2CompileStatus != 0)
    {
        std::cerr << "COP2 compile failed with status: " << cop2CompileStatus << "\n";
    }
    assert(cop2CompileStatus == 0);

    const std::string cop2RunCommand = quote(cop2ExePath);
    int cop2RunStatus = std::system(cop2RunCommand.c_str());
    if (cop2RunStatus != 0)
    {
        std::cerr << "COP2 run failed with status: " << cop2RunStatus << "\n";
    }
    assert(cop2RunStatus == 0);
}
