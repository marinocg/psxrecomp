#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    Value regA0 = Value::makeRegister(static_cast<Register>(4));

    entry.instructions.push_back(
        builder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(1)}, {temp0}, 0x80010000));
    entry.instructions.push_back(builder.makeInstruction(
        Opcode::ADD, {temp0, Value::makeImmediate(4)}, {temp1}, 0x80010004));
    entry.instructions.push_back(builder.makeInstruction(Opcode::BRANCH, {temp1}, {}, 0x80010008));
    entry.successors = {"then", "else"};

    thenBlock.instructions.push_back(
        builder.makeInstruction(Opcode::LOAD, {Value::makeAddress(0x80010010)}, {temp2}));
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

    assert(header.find("RecompiledModule") != std::string::npos);
    assert(source.find("readMemory32") != std::string::npos);
    assert(source.find("writeMemory32") != std::string::npos);
    assert(source.find("table_data") != std::string::npos);
    assert(source.find("switch (block)") != std::string::npos);
    assert(source.find("case BlockId::loop_1:") != std::string::npos);
    assert(source.find("if (") != std::string::npos);
    assert(buildFile.find("add_library") != std::string::npos);

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
    runtimeHeader << "#include <cstddef>\n";
    runtimeHeader << "#include <string>\n";
    runtimeHeader << "#include <vector>\n";
    runtimeHeader << "namespace psxrecomp { namespace runtime {\n";
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
    runtimeHeader << "    template <typename T> T read(Address) { return {}; }\n";
    runtimeHeader << "    template <typename T> void write(Address, T) {}\n";
    runtimeHeader << "    template <typename T> T readMmioExplicit(Address) { return {}; }\n";
    runtimeHeader << "    template <typename T> void writeMmioExplicit(Address, T) {}\n";
    runtimeHeader << "    void callBiosSyscall(u32, const u32*, std::size_t) {}\n";
    runtimeHeader << "    void callGpuIntrinsic(Address) {}\n";
    runtimeHeader << "    void callSpuIntrinsic(Address) {}\n";
    runtimeHeader << "    void callCdromIntrinsic(Address) {}\n";
    runtimeHeader << "    void setDiscSwapInfo(const DiscSwapInfo&) {}\n";
    runtimeHeader << "  private:\n";
    runtimeHeader << "    u8* m_ram;\n";
    runtimeHeader << "};\n";
    runtimeHeader << "} }\n";
    runtimeHeader.close();

    std::ofstream harnessFile(harnessPath);
    harnessFile << "#include \"module.h\"\n";
    harnessFile << "#include <array>\n";
    harnessFile << "int main() {\n";
    harnessFile << "  std::array<psxrecomp::u8, psxrecomp::MemoryMap::RAM_SIZE> ram{};\n";
    harnessFile << "  psxrecomp::runtime::PsxSystem system(ram.data());\n";
    harnessFile << "  psxrecomp::recompiler::RecompiledModule::run(system);\n";
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
    std::string command = quote(compiler) + " -std=c++17 -I" + quote(outputDir / "include") +
                          " -I" + quote(repoRoot / "include") + " -I" + quote(outputDir) + " " +
                          quote(sourcePath) + " " + quote(harnessPath) + " -o " + quote(exePath);
    int compileStatus = std::system(command.c_str());
    if (compileStatus != 0)
    {
        std::cerr << "Compile failed with status: " << compileStatus << "\n";
    }
    assert(compileStatus == 0);

    std::string runCommand = quote(exePath);
    int runStatus = std::system(runCommand.c_str());
    if (runStatus != 0)
    {
        std::cerr << "Run failed with status: " << runStatus << "\n";
    }
    assert(runStatus == 0);

    return 0;
}
