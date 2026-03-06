#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    using psxrecomp::Address;
    using psxrecomp::Register;
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::CodeGenerator;

    Program program;
    program.addGlobal("table_data", {0x1, 0x2, 0x3});

    Builder builder(program);
    auto& function = builder.createFunction("main_func", 0x80010000);
    auto& entry = builder.createBlock(function, "entry");
    auto& thenBlock = builder.createBlock(function, "then");
    auto& elseBlock = builder.createBlock(function, "else");

    Value temp0 = builder.createTemporary();
    Value temp1 = builder.createTemporary();
    Value temp2 = builder.createTemporary();
    Value mulHi0 = builder.createTemporary();
    Value mulLo0 = builder.createTemporary();
    Value mulHi1 = builder.createTemporary();
    Value mulLo1 = builder.createTemporary();
    Value divHi0 = builder.createTemporary();
    Value divLo0 = builder.createTemporary();
    Value regA0 = Value::makeRegister(static_cast<Register>(4));

    entry.instructions.push_back(
        builder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(1)}, {temp0}, 0x80010000));
    entry.instructions.push_back(builder.makeInstruction(
        Opcode::ADD, {temp0, Value::makeImmediate(4)}, {temp1}, 0x80010004));
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::MUL, {Value::makeImmediate(7), Value::makeImmediate(3)},
                                {mulHi0, mulLo0}, 0x80010006));
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::MULU, {Value::makeImmediate(5), Value::makeImmediate(2)},
                                {mulHi1, mulLo1}, 0x80010007));
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::DIV, {Value::makeImmediate(21), Value::makeImmediate(4)},
                                {divHi0, divLo0}, 0x80010008));
    entry.instructions.push_back(builder.makeInstruction(Opcode::BRANCH, {temp1}, {}, 0x8001000C));
    entry.successors = {"then", "else"};

    thenBlock.instructions.push_back(
        builder.makeInstruction(Opcode::LOAD, {Value::makeAddress(0x80010012)}, {temp2}));
    thenBlock.instructions.push_back(
        builder.makeInstruction(Opcode::STORE, {Value::makeAddress(0x80010014), temp2}, {}));
    thenBlock.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}));

    elseBlock.instructions.push_back(builder.makeInstruction(Opcode::MOVE, {regA0}, {temp2}));
    elseBlock.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}));

    auto& duplicateNameFunction = builder.createFunction("duplicate_block_names", 0x80012000);
    auto& duplicateEntry = builder.createBlock(duplicateNameFunction, "loop");
    auto& duplicateLoop = builder.createBlock(duplicateNameFunction, "loop");

    duplicateEntry.instructions.push_back(
        builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80012000));
    duplicateEntry.successors = {"loop"};

    duplicateLoop.instructions.push_back(
        builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80012004));

    CodeGenerator generator;
    std::string header = generator.generateHeader(program, "module");
    std::string source = generator.generateSource(program, "module");
    std::string buildFile = generator.generateBuildFile("module");
    std::string runner = generator.generateRunnerSource("module");
    auto countOccurrences = [](const std::string& haystack, const std::string& needle) -> size_t
    {
        if (needle.empty())
        {
            return 0;
        }
        size_t count = 0;
        size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }
        return count;
    };

    assert(header.find("RecompiledModule") != std::string::npos);
    assert(header.find("bool main_func(RecompilerContext& context, Address startAddress = 0);") !=
           std::string::npos);
    assert(source.find("readMemory32") != std::string::npos);
    assert(source.find("writeMemory32") != std::string::npos);
    assert(source.find("table_data") != std::string::npos);
    assert(source.find("switch (block)") != std::string::npos);
    assert(source.find("case BlockId::loop_1:") != std::string::npos);
    assert(source.find("static constexpr Address kBlockStarts[]") != std::string::npos);
    assert(source.find("static constexpr BlockId kBlockIds[]") != std::string::npos);
    assert(source.find("static constexpr Address kResumeRangeStarts[]") != std::string::npos);
    assert(source.find("static constexpr Address kResumeRangeEnds[]") != std::string::npos);
    assert(source.find("std::upper_bound(rangeStartsBegin, rangeStartsEnd, physical)") !=
           std::string::npos);
    assert(source.find("(physical & 0x3u) != 0 || physical >= kResumeRangeEnds[rangeIdx]") !=
           std::string::npos);
    assert(source.find("std::upper_bound(startsBegin, startsEnd, physical)") != std::string::npos);
    assert(source.find("case 0x10004: block = BlockId::") == std::string::npos);
    assert(source.find("if (") != std::string::npos);
    assert(source.find("callRecompiledFunction") != std::string::npos);
    assert(source.find("restoreCalleeSaved") != std::string::npos);
    assert(source.find("context.regs[Registers::S0] = preservedS0;") != std::string::npos);
    assert(source.find("failUnsupportedCall") != std::string::npos);
    assert(source.find("struct FnRange") != std::string::npos);
    assert(source.find("bool (*cachedRangeFn)(RecompilerContext&, Address) = nullptr;") !=
           std::string::npos);
    assert(source.find("bool (*fn)(RecompilerContext&, Address);") != std::string::npos);
    assert(source.find("static constexpr FnRange kFnRanges[]") != std::string::npos);
    assert(source.find("const size_t rangeCount = sizeof(kFnRanges) / sizeof(kFnRanges[0]);") !=
           std::string::npos);
    assert(source.find("if (context.cachedRangeFn != nullptr)") != std::string::npos);
    assert(source.find("if (context.cachedRangeFn(context, physical))") != std::string::npos);
    assert(source.find("context.cachedRangeStart = range.start;") != std::string::npos);
    assert(source.find("context.cachedRangeEndExclusive = range.endExclusive;") !=
           std::string::npos);
    assert(source.find("context.cachedRangeFn = range.fn;") != std::string::npos);
    assert(source.find("if (range.fn(context, physical))") != std::string::npos);
    assert(source.find("main_func(context, 0x10004);") == std::string::npos);
    assert(source.find("main_func(context, 0x10007);") == std::string::npos);
    assert(source.find("main_func(context, 0x10006);") == std::string::npos);
    assert(source.find("main_func(context, 0x10008);") == std::string::npos);
    assert(source.find("main_func(context, 0x1000c);") == std::string::npos);
    assert(source.find("kModuleEntryAddress") != std::string::npos);
    assert(source.find("callRecompiledFunction(context, kModuleEntryAddress)") !=
           std::string::npos);
    // Runtime shim policy: emit only the callback invoker bridge in run().
    assert(countOccurrences(source, "setCallbackInvoker(") == 1);
    assert(source.find("VSync") == std::string::npos);
    assert(source.find("DrawSync") == std::string::npos);
    assert(source.find("PSXRECOMP_AUTO_FRAME_PROGRESS") == std::string::npos);
    assert(source.find("setAutoFrameProgress") == std::string::npos);
    assert(source.find("triggerTrap") != std::string::npos);
    assert(buildFile.find("add_library") != std::string::npos);
    assert(buildFile.find("add_executable") != std::string::npos);
    assert(buildFile.find("_runner.cpp") != std::string::npos);
    assert(buildFile.find("if(WIN32 AND NOT MSVC)") != std::string::npos);
    assert(buildFile.find("-static -static-libgcc -static-libstdc++") != std::string::npos);
    assert(buildFile.find("PSXRECOMP_ENABLE_SDL_PRESENTER") != std::string::npos);
    assert(buildFile.find("PSXRECOMP_HAS_SDL2") != std::string::npos);
    assert(buildFile.find("user32 gdi32") != std::string::npos);
    assert(runner.find("PSXRECOMP_DUMP_FRAMEBUFFER") != std::string::npos);
    assert(runner.find("PSXRECOMP_DUMP_FULL_FRAMEBUFFER") != std::string::npos);
    assert(runner.find("PSXRECOMP_PRESENT_FRAMEBUFFER") != std::string::npos);
    assert(runner.find("PSXRECOMP_DISC_IMAGE") != std::string::npos);
    assert(runner.find("PSXRECOMP_AUTO_FRAME_PROGRESS") == std::string::npos);
    assert(runner.find("dumpFramebufferToPpm") != std::string::npos);
    assert(runner.find("extractDisplayPixels") != std::string::npos);
    assert(runner.find("appendSuffixBeforeExtension") != std::string::npos);
    assert(runner.find("countNonZeroPixels") != std::string::npos);
    assert(runner.find("captureBestDisplayPixels") != std::string::npos);
    assert(runner.find("presentFramebufferLive") != std::string::npos);
    assert(runner.find("displayWindow()") != std::string::npos);
    assert(runner.find("SDL_TEXTUREACCESS_STREAMING, static_cast<int>(displayWidth)") !=
           std::string::npos);
    assert(runner.find("auto framebuffer = captureBestDisplayPixels(system.gpu(),") !=
           std::string::npos);
    assert(runner.find("using alternate display page") != std::string::npos);
    assert(runner.find("std::thread presenterThread") != std::string::npos);
    assert(runner.find("#if defined(_WIN32)") != std::string::npos);
    assert(runner.find("Debug overlay") != std::string::npos);
    assert(runner.find("Last PC") != std::string::npos);
    assert(runner.find("envFlagEnabled(presentEnv, defaultPresent)") != std::string::npos);
    assert(runner.find("PSXRECOMP_RENDER_DEBUG_OVERLAY") != std::string::npos);
    assert(runner.find("resourcesDir / \"disc\" / \"data_track.bin\"") != std::string::npos);
    assert(runner.find("resourcesDir / \"disc\" / \"disc_layout.json\"") != std::string::npos);
    assert(runner.find("extractJsonU64Field(*layoutText, \"sectorSize\", &sectorSize)") !=
           std::string::npos);
    assert(runner.find("system.discSwapInfo()") == std::string::npos);
    assert(runner.find("system.setDisc(disc);") != std::string::npos);
    assert(runner.find("disc mounted") != std::string::npos);
    assert(runner.find("Runtime disc blob not found") != std::string::npos);
    assert(runner.find("decodeLogLevel") != std::string::npos);
    assert(runner.find("const auto& exVramWords = system.gpu().vramWords();") != std::string::npos);
    assert(runner.find("exPixels[y * exWidth + x] = pixel;") != std::string::npos);

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

    bool overlapDetected = false;
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

#if defined(_MSC_VER)
    std::cerr << "Skipping compile-and-run check on MSVC toolchain.\n";
    return 0;
#endif

#if !defined(PSXRECOMP_SOURCE_DIR)
#define PSXRECOMP_SOURCE_DIR ""
#endif
#if !defined(PSXRECOMP_TEST_CXX)
#define PSXRECOMP_TEST_CXX ""
#endif
    std::filesystem::path repoRoot = PSXRECOMP_SOURCE_DIR;
    assert(!repoRoot.empty());
    assert(std::filesystem::exists(repoRoot / "include/psxrecomp/types.h"));

    auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path outputDir =
        std::filesystem::temp_directory_path() / ("psxrecomp_codegen_test_" + stamp);
    std::filesystem::create_directories(outputDir);
    struct TempDirGuard
    {
        std::filesystem::path path;
        ~TempDirGuard()
        {
            std::filesystem::remove_all(path);
        }
    };
    TempDirGuard tempDirGuard{outputDir};

    std::filesystem::path includeDir = outputDir / "include/psxrecomp/runtime";
    std::filesystem::create_directories(includeDir);

    std::filesystem::path headerPath = outputDir / "module.h";
    std::filesystem::path sourcePath = outputDir / "module.cpp";
    std::filesystem::path harnessPath = outputDir / "harness.cpp";
    std::filesystem::path runtimeHeaderPath = includeDir / "psx_system.h";
    std::filesystem::path exePath = outputDir / "module_test";

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
    runtimeHeader << "      CoprocessorUnusable = 11\n";
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
    runtimeHeader << "    void serviceInterrupts() {}\n";
    runtimeHeader
        << "    bool consumePendingCallbackRegisters(std::array<u32, 32>&) { return false; }\n";
    runtimeHeader << "    void setCallbackInvoker(std::function<u32(u32)>) {}\n";
    runtimeHeader << "    RuntimeDebugOverlay& debugOverlay() { return m_overlay; }\n";
    runtimeHeader << "    Cop0& cop0() { return m_cop0; }\n";
    runtimeHeader << "    Gte& gte() { return m_gte; }\n";
    runtimeHeader << "  private:\n";
    runtimeHeader << "    u8* m_ram;\n";
    runtimeHeader << "    RuntimeDebugOverlay m_overlay;\n";
    runtimeHeader << "    Cop0 m_cop0;\n";
    runtimeHeader << "    Gte m_gte;\n";
    runtimeHeader << "};\n";
    runtimeHeader << "} }\n";
    runtimeHeader.close();

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

    auto quote = [](const std::filesystem::path& path)
    { return std::string("\"") + path.string() + "\""; };
    std::string compiler = PSXRECOMP_TEST_CXX;
    if (compiler.empty())
    {
        compiler = "c++";
    }
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

    return 0;
}
