#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{
using psxrecomp::Register;
using psxrecomp::ir::Builder;
using psxrecomp::ir::Opcode;
using psxrecomp::ir::Program;
using psxrecomp::ir::Value;
using psxrecomp::recompiler::CodeGenerator;

bool containsEitherHexCase(const std::string& source, const std::string& lowerNeedle,
                           const std::string& upperNeedle)
{
    return source.find(lowerNeedle) != std::string::npos ||
           source.find(upperNeedle) != std::string::npos;
}

void testExternalAddressJumpUsesJumpSemantics()
{
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("jump_external", 0x80020000);
    auto& entry = builder.createBlock(function, "entry");
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::JUMP, {Value::makeAddress(0x80020100)}, {}, 0x80020000));
    entry.successors = {"block_external"};

    CodeGenerator generator;
    const std::string source = generator.generateSource(program, "jump_external_module");
    const auto retirePos = source.find("finishLoadDelayCycle(context, instructionGroupWriteMask);");
    const auto jumpPos = source.find("if (!jumpRecompiledFunction(context, 0x80020100))");

    assert(jumpPos != std::string::npos);
    assert(containsEitherHexCase(source,
                                 "if (jumpIntrinsic(context.system, 0x80020100, context.regs))",
                                 "if (jumpIntrinsic(context.system, 0x80020100, context.regs))"));
    assert(source.find("callIntrinsic(context.system, 0x80020100, context.regs)") ==
           std::string::npos);
    assert(source.find("callRecompiledFunction(context, 0x80020100)") == std::string::npos);
    assert(source.find("traceInterestingCallsite(context, 0x80020100") == std::string::npos);
    assert(source.find("failUnsupportedJump(0x80020100, 0x80020000);") != std::string::npos);
    assert(source.find("failUnsupportedCall(context, 0x80020100") == std::string::npos);
    assert(retirePos != std::string::npos);
    assert(retirePos < jumpPos);

    std::cerr << "[PASS] external J uses jump transfer, not call dispatch\n";
}

void testRegisterJumpUsesJumpSemantics()
{
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("jump_register", 0x80021000);
    auto& entry = builder.createBlock(function, "entry");
    entry.instructions.push_back(builder.makeInstruction(
        Opcode::JUMP, {Value::makeRegister(static_cast<Register>(9))}, {}, 0x80021000));

    CodeGenerator generator;
    const std::string source = generator.generateSource(program, "jump_register_module");

    assert(source.find(
               "if (jumpIntrinsic(context.system, context.regs[Registers::T1], context.regs))") !=
           std::string::npos);
    assert(source.find("if (!jumpRecompiledFunction(context, context.regs[Registers::T1]))") !=
           std::string::npos);
    assert(
        source.find("callIntrinsic(context.system, context.regs[Registers::T1], context.regs)") ==
        std::string::npos);
    assert(source.find("callRecompiledFunction(context, context.regs[Registers::T1])") ==
           std::string::npos);
    assert(source.find("traceInterestingCallsite(context, context.regs[Registers::T1]") ==
           std::string::npos);
    assert(source.find("failUnsupportedJump(context.regs[Registers::T1], 0x80021000);") !=
           std::string::npos);

    std::cerr << "[PASS] register J uses jump fallback without call semantics\n";
}

void testBiosVectorAddressJumpUsesIntrinsicJumpPath()
{
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("jump_bios_vector", 0x80023000);
    auto& entry = builder.createBlock(function, "entry");
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::JUMP, {Value::makeAddress(0xA0)}, {}, 0x80023000));
    entry.successors = {"block_external"};

    CodeGenerator generator;
    const std::string source = generator.generateSource(program, "jump_bios_vector_module");

    assert(containsEitherHexCase(source, "if (jumpIntrinsic(context.system, 0xa0, context.regs))",
                                 "if (jumpIntrinsic(context.system, 0xA0, context.regs))"));
    assert(containsEitherHexCase(source, "if (!jumpRecompiledFunction(context, 0xa0))",
                                 "if (!jumpRecompiledFunction(context, 0xA0))"));
    assert(source.find("callIntrinsic(context.system, 0xA0, context.regs)") == std::string::npos);
    assert(source.find("callIntrinsic(context.system, 0xa0, context.regs)") == std::string::npos);
    assert(source.find("traceInterestingCallsite(context, 0xA0") == std::string::npos);
    assert(source.find("traceInterestingCallsite(context, 0xa0") == std::string::npos);

    std::cerr << "[PASS] BIOS vector address jump uses narrowed intrinsic jump path\n";
}

void testBiosVectorB0AddressJumpUsesIntrinsicJumpPath()
{
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("jump_bios_vector_b0", 0x80023008);
    auto& entry = builder.createBlock(function, "entry");
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::JUMP, {Value::makeAddress(0xB0)}, {}, 0x80023008));
    entry.successors = {"block_external"};

    CodeGenerator generator;
    const std::string source = generator.generateSource(program, "jump_bios_vector_b0_module");

    assert(containsEitherHexCase(source, "if (jumpIntrinsic(context.system, 0xb0, context.regs))",
                                 "if (jumpIntrinsic(context.system, 0xB0, context.regs))"));
    assert(containsEitherHexCase(source, "if (!jumpRecompiledFunction(context, 0xb0))",
                                 "if (!jumpRecompiledFunction(context, 0xB0))"));
    assert(source.find("callIntrinsic(context.system, 0xB0, context.regs)") == std::string::npos);
    assert(source.find("callIntrinsic(context.system, 0xb0, context.regs)") == std::string::npos);

    std::cerr << "[PASS] BIOS vector B0 jump uses narrowed intrinsic jump path\n";
}

void testBiosVectorRegisterJumpUsesIntrinsicJumpPath()
{
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("jump_bios_vector_register", 0x80023010);
    auto& entry = builder.createBlock(function, "entry");
    entry.instructions.push_back(builder.makeInstruction(
        Opcode::JUMP, {Value::makeRegister(static_cast<Register>(8))}, {}, 0x80023010));

    CodeGenerator generator;
    const std::string source =
        generator.generateSource(program, "jump_bios_vector_register_module");

    assert(source.find(
               "if (jumpIntrinsic(context.system, context.regs[Registers::T0], context.regs))") !=
           std::string::npos);
    assert(source.find("if (!jumpRecompiledFunction(context, context.regs[Registers::T0]))") !=
           std::string::npos);
    assert(
        source.find("callIntrinsic(context.system, context.regs[Registers::T0], context.regs)") ==
        std::string::npos);

    std::cerr << "[PASS] BIOS vector register jump uses narrowed intrinsic jump path\n";
}

void testCallStillUsesCallSemantics()
{
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("call_external", 0x80022000);
    auto& entry = builder.createBlock(function, "entry");
    entry.instructions.push_back(builder.makeInstruction(
        Opcode::CALL, {Value::makeRegister(static_cast<Register>(4))}, {}, 0x80022000));

    CodeGenerator generator;
    const std::string source = generator.generateSource(program, "call_external_module");
    const auto tracePos = source.find(
        "traceInterestingCallsite(context, context.regs[Registers::A0], 0x80022000, false);");
    const auto retirePos =
        source.find("finishLoadDelayCycle(context, instructionGroupWriteMask);", tracePos);
    const auto intrinsicPos = source.find(
        "if (!callIntrinsic(context.system, context.regs[Registers::A0], context.regs))");

    assert(tracePos != std::string::npos);
    assert(retirePos != std::string::npos);
    assert(intrinsicPos != std::string::npos);
    assert(retirePos < intrinsicPos);
    assert(source.find("if (!callRecompiledFunction(context, context.regs[Registers::A0]))") !=
           std::string::npos);
    assert(source.find("failUnsupportedCall(context, context.regs[Registers::A0], 0x80022000);") !=
           std::string::npos);
    assert(
        source.find(
            "traceInterestingCallsite(context, context.regs[Registers::A0], 0x80022000, true);") !=
        std::string::npos);

    std::cerr << "[PASS] CALL keeps intrinsic and recompiled call dispatch\n";
}

} // namespace

int main()
{
    testExternalAddressJumpUsesJumpSemantics();
    testRegisterJumpUsesJumpSemantics();
    testBiosVectorAddressJumpUsesIntrinsicJumpPath();
    testBiosVectorB0AddressJumpUsesIntrinsicJumpPath();
    testBiosVectorRegisterJumpUsesIntrinsicJumpPath();
    testCallStillUsesCallSemantics();
    return 0;
}
