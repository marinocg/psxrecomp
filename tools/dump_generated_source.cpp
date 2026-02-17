#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"
#include <iostream>

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
    std::string source = generator.generateSource(program, "module");
    std::cout << source;

    return 0;
}
