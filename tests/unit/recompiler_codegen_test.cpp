#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>

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

    return 0;
}
