/**
 * @file codegen_metadata_test.cpp
 * @brief Tests for initial register state (SP/GP/FP/RA) and RAM init image
 *        emission in generated source output.
 */
#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    using psxrecomp::Address;
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::CodeGenOptions;
    using psxrecomp::recompiler::CodeGenerator;
    using psxrecomp::recompiler::ModuleMetadata;

    // Helper: create a minimal program with one function that returns.
    auto makeMinimalProgram = []()
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("main_func", 0x80010000);
        auto& entry = builder.createBlock(function, "entry");
        entry.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80010000));
        return program;
    };

    // ---------------------------------------------------------------
    // Test 1: Stack pointer from header (stackAddress + stackSize)
    //
    // When metadata has a non-zero stackAddress and stackSize, SP
    // should be set to stackAddress + stackSize.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;
        metadata.stackAddress = 0x801F0000;
        metadata.stackSize = 0x1000;
        metadata.initialGp = 0;

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "sp_test", metadata);

        // SP should be stackAddress + stackSize = 0x801F1000
        assert(source.find("Registers::SP") != std::string::npos);
        assert(source.find("801f1000") != std::string::npos);

        std::cerr << "[PASS] SP set to stackAddress + stackSize\n";
    }

    // ---------------------------------------------------------------
    // Test 2: Stack pointer default when header has zero stackAddress
    //
    // When stackAddress is 0, SP should default to 0x801FFF00.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;
        metadata.stackAddress = 0;
        metadata.stackSize = 0;
        metadata.initialGp = 0;

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "sp_default", metadata);

        assert(source.find("801fff00") != std::string::npos);

        std::cerr << "[PASS] SP defaults to 0x801FFF00 when stackAddress is 0\n";
    }

    // ---------------------------------------------------------------
    // Test 3: Stack pointer with stackAddress but zero stackSize
    //
    // When stackAddress is set but stackSize is 0, SP should be
    // the stackAddress itself (no addition).
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;
        metadata.stackAddress = 0x801E0000;
        metadata.stackSize = 0;
        metadata.initialGp = 0;

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "sp_nosize", metadata);

        assert(source.find("801e0000") != std::string::npos);

        std::cerr << "[PASS] SP uses stackAddress when stackSize is 0\n";
    }

    // ---------------------------------------------------------------
    // Test 4: Global pointer from PSX-EXE header
    //
    // The generated source should set GP from metadata.initialGp.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;
        metadata.stackAddress = 0;
        metadata.stackSize = 0;
        metadata.initialGp = 0x80018000;

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "gp_test", metadata);

        assert(source.find("Registers::GP") != std::string::npos);
        assert(source.find("80018000") != std::string::npos);

        std::cerr << "[PASS] GP set from metadata.initialGp\n";
    }

    // ---------------------------------------------------------------
    // Test 5: Frame pointer initialized to SP
    //
    // Generated source should include FP = SP assignment.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "fp_test", metadata);

        assert(source.find("Registers::FP") != std::string::npos);
        assert(source.find("context.regs[Registers::SP]") != std::string::npos);

        std::cerr << "[PASS] FP initialized to SP\n";
    }

    // ---------------------------------------------------------------
    // Test 6: Return address initialized to 0
    //
    // Generated source should set RA = 0.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "ra_test", metadata);

        assert(source.find("Registers::RA") != std::string::npos);
        assert(source.find("= 0;") != std::string::npos);

        std::cerr << "[PASS] RA initialized to 0\n";
    }

    // ---------------------------------------------------------------
    // Test 7: RAM init image emission with program data
    //
    // When metadata has programData, the generated source should
    // contain the kRamInitData array and initMemory function.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;
        metadata.loadAddress = 0x80010000;
        metadata.loadSize = 4;
        metadata.programData = {0xDE, 0xAD, 0xBE, 0xEF};

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "ram_init", metadata);

        // Should contain the data array
        assert(source.find("kRamInitData") != std::string::npos);
        assert(source.find("kRamInitLoadAddress") != std::string::npos);
        assert(source.find("kRamInitLoadSize") != std::string::npos);
        // Data bytes should be present (as decimal)
        assert(source.find("222") != std::string::npos); // 0xDE = 222
        assert(source.find("173") != std::string::npos); // 0xAD = 173
        assert(source.find("190") != std::string::npos); // 0xBE = 190
        assert(source.find("239") != std::string::npos); // 0xEF = 239
        // initMemory should use memcpy
        assert(source.find("initMemory") != std::string::npos);
        assert(source.find("memcpy") != std::string::npos);

        std::cerr << "[PASS] RAM init image emission with program data\n";
    }

    // ---------------------------------------------------------------
    // Test 8: RAM init image skipped when programData is empty
    //
    // When metadata has no programData, the generated source should
    // set kRamInitData to nullptr and kRamInitLoadSize to 0.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;
        metadata.programData = {};

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "no_ram_init", metadata);

        // Should indicate no data
        assert(source.find("kRamInitData = nullptr") != std::string::npos);
        assert(source.find("kRamInitLoadSize = 0") != std::string::npos);

        std::cerr << "[PASS] RAM init image skipped when programData empty\n";
    }

    // ---------------------------------------------------------------
    // Test 9: Header declares initMemory
    //
    // The generated header should declare initMemory as a static
    // method of RecompiledModule.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        CodeGenerator generator;
        std::string header = generator.generateHeader(program, "header_test");

        assert(header.find("initMemory") != std::string::npos);
        assert(header.find("configure") != std::string::npos);
        assert(header.find("run") != std::string::npos);

        std::cerr << "[PASS] header declares initMemory\n";
    }

    // ---------------------------------------------------------------
    // Test 10: Load address embedded in generated constants
    //
    // kRamInitLoadAddress should reflect metadata.loadAddress.
    // ---------------------------------------------------------------
    {
        Program program = makeMinimalProgram();
        ModuleMetadata metadata;
        metadata.entryAddress = 0x80010000;
        metadata.loadAddress = 0x80020000;
        metadata.loadSize = 2;
        metadata.programData = {0x01, 0x02};

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "load_addr", metadata);

        assert(source.find("80020000") != std::string::npos);

        std::cerr << "[PASS] load address embedded in generated constants\n";
    }

    std::cerr << "All codegen metadata tests passed.\n";
    return 0;
}
