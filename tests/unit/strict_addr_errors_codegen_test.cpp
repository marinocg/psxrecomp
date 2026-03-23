#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{
using psxrecomp::Address;
using psxrecomp::Register;
using psxrecomp::ir::Builder;
using psxrecomp::ir::Opcode;
using psxrecomp::ir::Program;
using psxrecomp::ir::Value;
using psxrecomp::recompiler::CodeGenerator;

bool contains(const std::string& source, const std::string& needle)
{
    return source.find(needle) != std::string::npos;
}

std::string generateSingleMemorySource(Opcode opcode, Address accessAddress, Address sourcePc)
{
    Program program;
    Builder builder(program);

    auto& function = builder.createFunction("strict_addr_case", sourcePc);
    auto& entry = builder.createBlock(function, "entry");
    Value temp = builder.createTemporary();
    const Value regV0 = Value::makeRegister(static_cast<Register>(2));

    switch (opcode)
    {
    case Opcode::LOAD:
    case Opcode::LOAD16:
    case Opcode::LOAD16U:
        entry.instructions.push_back(
            builder.makeInstruction(opcode, {Value::makeAddress(accessAddress)}, {temp}, sourcePc));
        entry.instructions.push_back(
            builder.makeInstruction(Opcode::MOVE, {temp}, {regV0}, sourcePc + 4));
        break;
    case Opcode::STORE:
    case Opcode::STORE16:
        entry.instructions.push_back(builder.makeInstruction(
            opcode, {Value::makeAddress(accessAddress), Value::makeImmediate(0x12345678)}, {},
            sourcePc));
        break;
    default:
        assert(false);
    }

    entry.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, sourcePc + 8));

    CodeGenerator generator;
    return generator.generateSource(program, "strict_addr_case_module");
}

std::string generateDelaySlotMemorySource(Opcode opcode, Address accessAddress, Address branchPc,
                                          Address slotPc)
{
    Program program;
    Builder builder(program);

    auto& function = builder.createFunction("strict_addr_delay_slot_case", branchPc);
    auto& entry = builder.createBlock(function, "entry");
    Value temp = builder.createTemporary();

    entry.instructions.push_back(
        builder.makeInstruction(Opcode::JUMP, {Value::makeAddress(0x80014000)}, {}, branchPc,
                                std::string("j 0x80014000"), branchPc));
    entry.successors = {"block_external"};

    switch (opcode)
    {
    case Opcode::LOAD:
    case Opcode::LOAD16:
    case Opcode::LOAD16U:
        entry.instructions.push_back(
            builder.makeInstruction(opcode, {Value::makeAddress(accessAddress)}, {temp}, branchPc,
                                    std::string("delay_slot_load"), slotPc));
        break;
    case Opcode::STORE:
    case Opcode::STORE16:
        entry.instructions.push_back(builder.makeInstruction(
            opcode, {Value::makeAddress(accessAddress), Value::makeImmediate(0x12345678)}, {},
            branchPc, std::string("delay_slot_store"), slotPc));
        break;
    default:
        assert(false);
    }

    CodeGenerator generator;
    return generator.generateSource(program, "strict_addr_delay_slot_module");
}

void testStrictDefaultAndEnvOptOut()
{
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("strict_addr_defaults", 0x80010000);
    auto& entry = builder.createBlock(function, "entry");
    Value temp = builder.createTemporary();
    entry.instructions.push_back(builder.makeInstruction(
        Opcode::LOAD, {Value::makeAddress(0x80010002)}, {temp}, 0x80010000));
    entry.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80010004));

    CodeGenerator generator;
    const std::string source = generator.generateSource(program, "strict_addr_defaults_module");

    assert(contains(source, "#define PSXRECOMP_STRICT_ADDR_ERRORS 1"));
    assert(contains(source, "if (env[0] == '0')"));
    assert(contains(source, "if (env[0] == '1')"));
    assert(contains(source, "return true;"));
    assert(contains(source, "return false;"));

    std::cerr << "[PASS] strict address errors default on with env opt-out helper\n";
}

void testMisalignedLoadAndStoreHelpers()
{
    const std::string lhSource =
        generateSingleMemorySource(Opcode::LOAD16, 0x80010001u, 0x80012000u);
    assert(contains(lhSource, "readMemory16s(context, 0x80010001, 0x80012000, false);"));

    const std::string lhuSource =
        generateSingleMemorySource(Opcode::LOAD16U, 0x80010001u, 0x80012010u);
    assert(contains(lhuSource, "readMemory16(context, 0x80010001, 0x80012010, false);"));

    const std::string lwSource = generateSingleMemorySource(Opcode::LOAD, 0x80010002u, 0x80012020u);
    assert(contains(lwSource, "readMemory32(context, 0x80010002, 0x80012020, false);"));

    const std::string shSource =
        generateSingleMemorySource(Opcode::STORE16, 0x80010001u, 0x80012030u);
    assert(contains(shSource, "writeMemory16(context, 0x80010001, "));
    assert(contains(shSource, ", 0x80012030, false);"));

    const std::string swSource =
        generateSingleMemorySource(Opcode::STORE, 0x80010002u, 0x80012040u);
    assert(contains(swSource, "writeMemory32(context, 0x80010002, "));
    assert(contains(swSource, ", 0x80012040, false);"));

    assert(contains(swSource,
                    "raiseAddressError(context, runtime::Cop0::ExceptionCode::AddressErrorStore,"));
    assert(contains(lwSource,
                    "raiseAddressError(context, runtime::Cop0::ExceptionCode::AddressErrorLoad,"));

    std::cerr << "[PASS] misaligned LH/LHU/LW/SH/SW route through strict helpers\n";
}

void testDelaySlotMetadataPlumbing()
{
    const std::string loadSource =
        generateDelaySlotMemorySource(Opcode::LOAD16U, 0x80010001u, 0x80013000u, 0x80013004u);
    assert(contains(loadSource, "readMemory16(context, 0x80010001, 0x80013000, true);"));

    const std::string storeSource =
        generateDelaySlotMemorySource(Opcode::STORE, 0x80010002u, 0x80013100u, 0x80013104u);
    assert(contains(storeSource, "writeMemory32(context, 0x80010002, "));
    assert(contains(storeSource, ", 0x80013100, true);"));

    std::cerr << "[PASS] delay-slot address faults keep source PC and BD metadata\n";
}

} // namespace

int main()
{
    testStrictDefaultAndEnvOptOut();
    testMisalignedLoadAndStoreHelpers();
    testDelaySlotMetadataPlumbing();
    return 0;
}
