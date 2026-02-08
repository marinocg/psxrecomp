#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    CodeGenerator generator;
    std::string header = generator.generateHeader(program, "module");
    std::string source = generator.generateSource(program, "module");
    std::string buildFile = generator.generateBuildFile("module");

    assert(header.find("RecompiledModule") != std::string::npos);
    assert(source.find("readMemory32") != std::string::npos);
    assert(source.find("writeMemory32") != std::string::npos);
    assert(source.find("table_data") != std::string::npos);
    assert(source.find("switch (block)") != std::string::npos);
    assert(source.find("if (") != std::string::npos);
    assert(buildFile.find("add_library") != std::string::npos);

    std::filesystem::path currentPath = std::filesystem::current_path();
    std::filesystem::path repoRoot = currentPath;
    for (int depth = 0; depth < 6; ++depth)
    {
        if (std::filesystem::exists(repoRoot / "include/psxrecomp/types.h"))
        {
            break;
        }
        repoRoot = repoRoot.parent_path();
    }
    assert(std::filesystem::exists(repoRoot / "include/psxrecomp/types.h"));

    auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path outputDir =
        std::filesystem::temp_directory_path() / ("psxrecomp_codegen_test_" + stamp);
    std::filesystem::create_directories(outputDir);

    std::filesystem::path headerPath = outputDir / "module.h";
    std::filesystem::path sourcePath = outputDir / "module.cpp";
    std::filesystem::path objectPath = outputDir / "module.o";

    std::ofstream headerFile(headerPath);
    headerFile << header;
    headerFile.close();
    std::ofstream sourceFile(sourcePath);
    sourceFile << source;
    sourceFile.close();

    auto quote = [](const std::filesystem::path& path)
    { return std::string("\"") + path.string() + "\""; };
    std::string command = "c++ -std=c++17 -I" + quote(repoRoot / "include") + " -I" +
                          quote(outputDir) + " -c " + quote(sourcePath) + " -o " +
                          quote(objectPath);
    int compileStatus = std::system(command.c_str());
    assert(compileStatus == 0);

    return 0;
}
