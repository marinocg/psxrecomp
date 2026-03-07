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
