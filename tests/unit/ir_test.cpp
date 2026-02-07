#include "psxrecomp/ir/ir.h"

#include <cassert>
#include <string>

int main()
{
    using psxrecomp::Address;
    using psxrecomp::Register;
    using psxrecomp::ir::BasicBlock;
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Instruction;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::ir::ValueKind;

    Program program;
    Builder builder(program);

    auto& function = builder.createFunction("boot", 0x80010000);
    BasicBlock& entry = builder.createBlock(function, "entry");
    BasicBlock* entryPtr = &entry;

    Value temp0 = builder.createTemporary();
    Value temp1 = builder.createTemporary();
    Value regA0 = Value::makeRegister(static_cast<Register>(4));
    Value imm16 = Value::makeImmediate(16);
    Value negImm = Value::makeImmediate(-8);
    Value addr = Value::makeAddress(0x80010000);
    Value invalid = Value::invalid();

    Instruction add = builder.makeInstruction(Opcode::ADD, {regA0, imm16}, {temp0}, 0x80010000);
    Instruction move = builder.makeInstruction(Opcode::MOVE, {temp0}, {temp1}, std::nullopt);
    entry.instructions.push_back(add);
    entry.instructions.push_back(move);
    entry.successors.push_back("exit");

    BasicBlock& exitBlock = builder.createBlock(function, "exit");
    exitBlock.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80010008));
    BasicBlock& lateBlock = builder.createBlock(function, "late");
    lateBlock.instructions.push_back(builder.makeInstruction(Opcode::NOP, {}, {}, std::nullopt));

    assert(program.functions.size() == 1);
    assert(program.functions[0].blocks.size() == 3);
    assert(program.functions[0].blocks[0].name == "entry");
    assert(program.functions[0].blocks[1].name == "exit");
    assert(program.functions[0].blocks[2].name == "late");
    assert(entryPtr == &program.functions[0].blocks[0]);

    assert(temp0.kind == ValueKind::TEMPORARY);
    assert(temp1.temporaryId == temp0.temporaryId + 1);
    assert(invalid.toString() == "<invalid>");
    assert(negImm.toString() == "-8");
    assert(addr.toString() == "0x80010000");

    assert(entry.instructions[0].opcode == Opcode::ADD);
    assert(entry.instructions[0].inputs.size() == 2);
    assert(entry.instructions[0].outputs.size() == 1);
    assert(entry.instructions[0].outputs[0] == temp0);

    assert(entry.instructions[0].toString() == "add t0 <- r4, 16 @0x80010000");
    assert(entry.instructions[1].toString() == "move t1 <- t0");
    assert(exitBlock.instructions[0].toString() == "return @0x80010008");
    assert(entry.instructions[0].toString().find("0x80010000") != std::string::npos);
    assert(lateBlock.instructions[0].toString() == "nop");

    assert(entry.toString().find("entry") != std::string::npos);
    assert(lateBlock.toString().find("nop") != std::string::npos);

    return 0;
}
