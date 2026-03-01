#include "codegen_lowering_source_comments_case.h"

#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <string>

void runCodegenLoweringSourceCommentsCase()
{
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::CodeGenerator;

    // ---------------------------------------------------------------
    // Test 11: Source MIPS comments are emitted only when enabled
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("test_source_asm_comments", 0x8001234C);
        auto& entry = builder.createBlock(function, "entry");
        Value temp = builder.createTemporary();

        entry.instructions.push_back(
            builder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(1)}, {temp}, 0x8001234C,
                                    std::string("addiu sp, sp, -0x20")));
        entry.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80012350));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "source_asm_comments_module");
        assert(source.find("// 0x8001234C: addiu sp, sp, -0x20") != std::string::npos);

        Program asmOnlyProgram;
        Builder asmOnlyBuilder(asmOnlyProgram);
        auto& asmOnlyFunction =
            asmOnlyBuilder.createFunction("test_source_asm_only_comments", 0x80020000);
        auto& asmOnlyEntry = asmOnlyBuilder.createBlock(asmOnlyFunction, "entry");
        Value asmOnlyTemp = asmOnlyBuilder.createTemporary();
        asmOnlyEntry.instructions.push_back(
            asmOnlyBuilder.makeInstruction(Opcode::MOVE, {Value::makeImmediate(2)}, {asmOnlyTemp},
                                           std::nullopt, std::string("move t0, t1")));
        asmOnlyEntry.instructions.push_back(
            asmOnlyBuilder.makeInstruction(Opcode::RETURN, {}, {}, 0x80020004));

        std::string asmOnlySource =
            generator.generateSource(asmOnlyProgram, "source_asm_only_comments_module");
        assert(asmOnlySource.find("// move t0, t1") != std::string::npos);

        Program asmAddressProgram;
        Builder asmAddressBuilder(asmAddressProgram);
        auto& asmAddressFunction =
            asmAddressBuilder.createFunction("test_source_asm_address_comments", 0x80030000);
        auto& asmAddressEntry = asmAddressBuilder.createBlock(asmAddressFunction, "entry");
        Value asmAddressTemp = asmAddressBuilder.createTemporary();
        asmAddressEntry.instructions.push_back(asmAddressBuilder.makeInstruction(
            Opcode::MOVE, {Value::makeImmediate(3)}, {asmAddressTemp}, 0x80030000,
            std::string("nop"), 0x80030004));
        asmAddressEntry.instructions.push_back(
            asmAddressBuilder.makeInstruction(Opcode::RETURN, {}, {}, 0x80030008));

        std::string asmAddressSource =
            generator.generateSource(asmAddressProgram, "source_asm_address_comments_module");
        assert(asmAddressSource.find("// 0x80030004: nop") != std::string::npos);
        assert(asmAddressSource.find("// 0x80030000: nop") == std::string::npos);

        psxrecomp::recompiler::CodeGenOptions noCommentOptions;
        noCommentOptions.generateComments = false;
        CodeGenerator noCommentGenerator(noCommentOptions);
        std::string sourceNoComments =
            noCommentGenerator.generateSource(program, "source_asm_comments_module_no_comments");
        assert(sourceNoComments.find("// 0x8001234C: addiu sp, sp, -0x20") == std::string::npos);

        std::cerr << "[PASS] source MIPS comments respect generateComments option\n";
    }
}
