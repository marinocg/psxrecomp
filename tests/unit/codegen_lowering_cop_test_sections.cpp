#include "codegen_lowering_cop_test_sections.h"

#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <string>

void runCodegenLoweringCopTests()
{
    using psxrecomp::Address;
    using psxrecomp::ir::BasicBlock;
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::CodeGenerator;

    // Test 10: COP0 lowering emits runtime COP0 helpers
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_cop0_lowering", 0x800A0000);
        auto& entry = builder.createBlock(function, "entry");

        entry.instructions.push_back(builder.makeInstruction(
            Opcode::COP0_MTC, {Value::makeImmediate(12), Value::makeRegister(2)}, {}, 0x800A0000));
        entry.instructions.push_back(builder.makeInstruction(
            Opcode::COP0_MFC, {Value::makeImmediate(12)}, {Value::makeRegister(3)}, 0x800A0004));
        entry.instructions.push_back(builder.makeInstruction(Opcode::COP0_RFE, {}, {}, 0x800A0008));
        entry.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x800A000C));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "cop0_lowering_module");

        assert(source.find("context.system.cop0().mtc0") != std::string::npos);
        assert(source.find("const u32 loadResult = context.system.cop0().mfc0") !=
               std::string::npos);
        assert(source.find("stagePendingLoad(context, static_cast<Register>(3), loadResult);") !=
               std::string::npos);
        assert(source.find("context.system.cop0().rfe()") != std::string::npos);

        std::cerr << "[PASS] COP0 lowering emits runtime COP0 helpers\n";
    }

    // ---------------------------------------------------------------
    // Test 11: GTE transfer lowering emits COP2 guard + runtime helpers
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_gte_lowering", 0x800A0800);
        auto& entry = builder.createBlock(function, "entry");

        entry.instructions.push_back(builder.makeInstruction(
            Opcode::GTE_MTC2, {Value::makeImmediate(6), Value::makeRegister(2)}, {}, 0x800A0800));
        entry.instructions.push_back(builder.makeInstruction(
            Opcode::GTE_MFC2, {Value::makeImmediate(6)}, {Value::makeRegister(3)}, 0x800A0804));
        entry.instructions.push_back(builder.makeInstruction(
            Opcode::GTE_CTC2, {Value::makeImmediate(7), Value::makeRegister(4)}, {}, 0x800A0808));
        entry.instructions.push_back(builder.makeInstruction(
            Opcode::GTE_CFC2, {Value::makeImmediate(7)}, {Value::makeRegister(5)}, 0x800A080C));
        entry.instructions.push_back(builder.makeInstruction(
            Opcode::GTE_LWC2, {Value::makeImmediate(8), Value::makeAddress(0x80011000)}, {},
            0x800A0810));
        entry.instructions.push_back(builder.makeInstruction(
            Opcode::GTE_SWC2, {Value::makeImmediate(8), Value::makeAddress(0x80011004)}, {},
            0x800A0814));
        entry.instructions.push_back(builder.makeInstruction(
            Opcode::GTE_EXEC, {Value::makeImmediate(static_cast<psxrecomp::s32>(0x4A280030u))}, {},
            0x800A0818));
        entry.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x800A081C));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "gte_lowering_module");

        assert(source.find("context.system.cop0().cop2Enabled()") != std::string::npos);
        assert(source.find("runtime::Cop0::ExceptionCode::CoprocessorUnusable") !=
               std::string::npos);
        assert(source.find("context.system.gte().mtc2") != std::string::npos);
        assert(source.find("const u32 loadResult = context.system.gte().mfc2") !=
               std::string::npos);
        assert(source.find("stagePendingLoad(context, static_cast<Register>(3), loadResult);") !=
               std::string::npos);
        assert(source.find("context.system.gte().ctc2") != std::string::npos);
        assert(source.find("const u32 loadResult = context.system.gte().cfc2") !=
               std::string::npos);
        assert(source.find("stagePendingLoad(context, static_cast<Register>(5), loadResult);") !=
               std::string::npos);
        assert(source.find("readMemory32(context, 0x80011000)") != std::string::npos);
        assert(source.find("writeMemory32(context, 0x80011004, "
                           "context.system.gte().mfc2(static_cast<u8>(8)))") != std::string::npos);
        assert(source.find("context.system.gte().exec(static_cast<u32>(" +
                           std::to_string(static_cast<psxrecomp::s32>(0x4A280030u)) + "))") !=
               std::string::npos);

        std::cerr << "[PASS] GTE transfer lowering emits COP2 guard + runtime helpers\n";
    }

    // ---------------------------------------------------------------
    // Test 12: CPU_EXCEPTION lowering emits runtime exception helper
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_cpu_exception_lowering", 0x800A1000);
        auto& entry = builder.createBlock(function, "entry");

        entry.instructions.push_back(builder.makeInstruction(
            Opcode::CPU_EXCEPTION, {Value::makeImmediate(10), Value::makeImmediate(0)}, {},
            0x800A1000));
        entry.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x800A1004));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "cpu_exception_lowering_module");

        assert(source.find("raiseCpuException(context, 10, 0x800a1000, (0 != 0));") !=
               std::string::npos);
        assert(source.find("context.system.cop0().exceptionEnter(") != std::string::npos);

        std::cerr << "[PASS] CPU_EXCEPTION lowering emits runtime helper\n";
    }

    // Test 13 moved to a dedicated companion translation unit.
}
