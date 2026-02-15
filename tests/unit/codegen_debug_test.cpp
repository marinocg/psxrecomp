/**
 * @file codegen_debug_test.cpp
 * @brief Tests that debug/diagnostic tooling is present in generated source.
 *
 * Verifies that PSXRECOMP_MAX_STEPS, PSXRECOMP_BREAK_PC,
 * PSXRECOMP_TRACE_MMIO, PSXRECOMP_TRACE_CALLS, and the BIOS
 * vector intrinsic dispatch are emitted in the generated code.
 */
#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::CodeGenerator;
    using psxrecomp::recompiler::ModuleMetadata;

    // Build a minimal program with one function.
    Program program;
    Builder builder(program);
    auto& function = builder.createFunction("main_func", 0x80010000);
    auto& entry = builder.createBlock(function, "entry");
    entry.instructions.push_back(
        builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80010000));

    ModuleMetadata metadata;
    metadata.entryAddress = 0x80010000;

    CodeGenerator generator;
    std::string source = generator.generateSource(program, "debug_module", metadata);

    // ---------------------------------------------------------------
    // Test 1: PSXRECOMP_MAX_STEPS environment variable check
    //
    // The generated setProgramCounter helper should check for
    // PSXRECOMP_MAX_STEPS to enforce a step budget.
    // ---------------------------------------------------------------
    assert(source.find("PSXRECOMP_MAX_STEPS") != std::string::npos);
    assert(source.find("Step budget exhausted") != std::string::npos);
    std::cerr << "[PASS] PSXRECOMP_MAX_STEPS present\n";

    // ---------------------------------------------------------------
    // Test 2: PSXRECOMP_BREAK_PC environment variable check
    //
    // The generated setProgramCounter helper should check for
    // PSXRECOMP_BREAK_PC to provide a PC breakpoint.
    // ---------------------------------------------------------------
    assert(source.find("PSXRECOMP_BREAK_PC") != std::string::npos);
    assert(source.find("Breakpoint hit at PC") != std::string::npos);
    std::cerr << "[PASS] PSXRECOMP_BREAK_PC present\n";

    // ---------------------------------------------------------------
    // Test 3: PSXRECOMP_TRACE_MMIO environment variable check
    //
    // The generated readMmio32/writeMmio32 helpers should check
    // PSXRECOMP_TRACE_MMIO for MMIO access tracing.
    // ---------------------------------------------------------------
    assert(source.find("PSXRECOMP_TRACE_MMIO") != std::string::npos);
    assert(source.find("[mmio]") != std::string::npos);
    std::cerr << "[PASS] PSXRECOMP_TRACE_MMIO present\n";

    // ---------------------------------------------------------------
    // Test 4: PSXRECOMP_TRACE_CALLS environment variable check
    //
    // The generated callRecompiledFunction helper should check
    // PSXRECOMP_TRACE_CALLS for function call tracing.
    // ---------------------------------------------------------------
    assert(source.find("PSXRECOMP_TRACE_CALLS") != std::string::npos);
    assert(source.find("[call]") != std::string::npos);
    std::cerr << "[PASS] PSXRECOMP_TRACE_CALLS present\n";

    // ---------------------------------------------------------------
    // Test 5: BIOS vector dispatch in callIntrinsic
    //
    // The generated callIntrinsic helper should handle BIOS vectors
    // at addresses A0h, B0h, C0h by calling callBiosVector.
    // ---------------------------------------------------------------
    assert(source.find("callBiosVector") != std::string::npos);
    assert(source.find("0xA0") != std::string::npos);
    assert(source.find("0xB0") != std::string::npos);
    assert(source.find("0xC0") != std::string::npos);
    std::cerr << "[PASS] BIOS vector dispatch in callIntrinsic\n";

    // ---------------------------------------------------------------
    // Test 6: GPU intrinsic dispatch
    //
    // callIntrinsic should dispatch GPU addresses to callGpuIntrinsic.
    // ---------------------------------------------------------------
    assert(source.find("callGpuIntrinsic") != std::string::npos);
    assert(source.find("1F801810") != std::string::npos ||
           source.find("0x1F801810") != std::string::npos ||
           source.find("0x1f801810") != std::string::npos);
    std::cerr << "[PASS] GPU intrinsic dispatch\n";

    // ---------------------------------------------------------------
    // Test 7: CD-ROM intrinsic dispatch
    //
    // callIntrinsic should dispatch CDROM addresses to callCdromIntrinsic.
    // ---------------------------------------------------------------
    assert(source.find("callCdromIntrinsic") != std::string::npos);
    std::cerr << "[PASS] CDROM intrinsic dispatch\n";

    // ---------------------------------------------------------------
    // Test 8: SPU intrinsic dispatch
    //
    // callIntrinsic should dispatch SPU addresses to callSpuIntrinsic.
    // ---------------------------------------------------------------
    assert(source.find("callSpuIntrinsic") != std::string::npos);
    std::cerr << "[PASS] SPU intrinsic dispatch\n";

    // ---------------------------------------------------------------
    // Test 9: Debug overlay integration
    //
    // setProgramCounter should update the debug overlay's last PC.
    // ---------------------------------------------------------------
    assert(source.find("debugOverlay") != std::string::npos);
    assert(source.find("setLastProgramCounter") != std::string::npos);
    std::cerr << "[PASS] debug overlay integration\n";

    // ---------------------------------------------------------------
    // Test 10: Pipeline warnings vector
    //
    // The generated source should include a kPipelineWarnings vector.
    // ---------------------------------------------------------------
    assert(source.find("kPipelineWarnings") != std::string::npos);
    std::cerr << "[PASS] pipeline warnings vector present\n";

    // ---------------------------------------------------------------
    // Test 11: Pipeline warnings with actual content
    //
    // When metadata includes warnings, they should appear in the
    // generated kPipelineWarnings vector.
    // ---------------------------------------------------------------
    {
        ModuleMetadata metadataWithWarnings;
        metadataWithWarnings.entryAddress = 0x80010000;
        metadataWithWarnings.warnings = {"Test warning message"};

        std::string warningSource =
            generator.generateSource(program, "warn_module", metadataWithWarnings);
        assert(warningSource.find("Test warning message") != std::string::npos);
    }
    std::cerr << "[PASS] pipeline warnings with content\n";

    // ---------------------------------------------------------------
    // Test 12: Disc swap info in configure()
    //
    // The configure method should emit disc swap info setup.
    // ---------------------------------------------------------------
    {
        ModuleMetadata discMeta;
        discMeta.entryAddress = 0x80010000;
        discMeta.discSetName = "TestDisc";
        discMeta.activeDiscIndex = 0;
        ModuleMetadata::DiscEntry disc;
        disc.index = 0;
        disc.label = "DISC_1";
        disc.path = "disc1.bin";
        discMeta.discs.push_back(disc);

        std::string discSource =
            generator.generateSource(program, "disc_module", discMeta);
        assert(discSource.find("TestDisc") != std::string::npos);
        assert(discSource.find("DISC_1") != std::string::npos);
        assert(discSource.find("disc1.bin") != std::string::npos);
        assert(discSource.find("setDiscSwapInfo") != std::string::npos);
    }
    std::cerr << "[PASS] disc swap info in configure()\n";

    std::cerr << "All codegen debug tests passed.\n";
    return 0;
}
