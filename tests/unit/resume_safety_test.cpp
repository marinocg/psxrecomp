/**
 * @file resume_safety_test.cpp
 * @brief Regression tests for resume-state-safety at instruction granularity.
 *
 * These tests verify that:
 * 1. The resume-safety verifier detects cross-source-address temporary
 *    dependencies within a basic block.
 * 2. The code emitter groups instructions under resume guards by source
 *    address so that continuation IR instructions (no source address) are
 *    correctly skipped during mid-block resume.
 * 3. CSE scoping prevents the optimizer from creating cross-source-address
 *    temporary references.
 */
#include "psxrecomp/ir/ir.h"
#include "psxrecomp/ir/optimizations.h"
#include "psxrecomp/ir/resume_safety.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{

[[maybe_unused]] size_t countOccurrences(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
    {
        return 0;
    }
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++count;
        pos += needle.size();
    }
    return count;
}

} // namespace

int main()
{
    using psxrecomp::Address;
    using psxrecomp::Register;
    using psxrecomp::ir::BasicBlock;
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Function;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::ir::verifyResumeSafety;

    // -----------------------------------------------------------------------
    // Test 1: Verifier detects cross-source-address temporary dependency
    //
    // Model the allocator-style shape:
    //   0x1000: temp0 = LOAD(addr)         ; load pointer
    //   0x1004: temp1 = ADD(temp0, 4)      ; transform
    //   0x1008: STORE(addr2, temp1)        ; store back
    //
    // temp0 is defined at 0x1000 and used at 0x1004 — cross-source-address.
    // temp1 is defined at 0x1004 and used at 0x1008 — cross-source-address.
    // Both should be flagged by the verifier.
    // -----------------------------------------------------------------------
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("test_cross_addr", 0x80001000);
        auto& block = builder.createBlock(function, "block_0x1000");

        Value temp0 = builder.createTemporary();
        Value temp1 = builder.createTemporary();

        block.instructions.push_back(builder.makeInstruction(
            Opcode::LOAD, {Value::makeAddress(0x80010000)}, {temp0}, 0x80001000));
        block.instructions.push_back(builder.makeInstruction(
            Opcode::ADD, {temp0, Value::makeImmediate(4)}, {temp1}, 0x80001004));
        block.instructions.push_back(builder.makeInstruction(
            Opcode::STORE, {Value::makeAddress(0x80010004), temp1}, {}, 0x80001008));
        block.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x8000100C));

        auto diagnostics = verifyResumeSafety(function);
        assert(!diagnostics.empty() && "Expected verifier to detect cross-source-address temps");
        assert(diagnostics.size() == 2);
        assert(diagnostics[0].defSourceAddress == 0x80001000);
        assert(diagnostics[0].useSourceAddress == 0x80001004);
        assert(diagnostics[0].temporaryId == temp0.temporaryId);
        assert(diagnostics[1].defSourceAddress == 0x80001004);
        assert(diagnostics[1].useSourceAddress == 0x80001008);
        assert(diagnostics[1].temporaryId == temp1.temporaryId);
        std::cerr << "  Test 1 passed: verifier detects cross-source-address temps\n";
    }

    // -----------------------------------------------------------------------
    // Test 2: Verifier passes for same-source-address temporaries
    //
    // All instructions share the same source address — this is safe because
    // they are all inside the same resume guard.
    // -----------------------------------------------------------------------
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("test_same_addr", 0x80002000);
        auto& block = builder.createBlock(function, "block_0x2000");

        Value temp0 = builder.createTemporary();
        Value temp1 = builder.createTemporary();

        block.instructions.push_back(builder.makeInstruction(
            Opcode::LOAD, {Value::makeAddress(0x80020000)}, {temp0}, 0x80002000));
        block.instructions.push_back(builder.makeInstruction(
            Opcode::ADD, {temp0, Value::makeImmediate(4)}, {temp1}, 0x80002000));
        block.instructions.push_back(builder.makeInstruction(
            Opcode::STORE, {Value::makeAddress(0x80020004), temp1}, {}, 0x80002000));
        block.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80002004));

        auto diagnostics = verifyResumeSafety(function);
        assert(diagnostics.empty() && "Same-source-address temps should be safe");
        std::cerr << "  Test 2 passed: same-source-address temps are safe\n";
    }

    // -----------------------------------------------------------------------
    // Test 3: Code emitter groups same-source-address instructions
    //
    // Generate C++ code from an IR function where two IR instructions share
    // the same source address.  The generated code should have only ONE
    // resume guard for that address (not two separate guards).
    // -----------------------------------------------------------------------
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("test_guard_merge", 0x80003000);
        auto& block = builder.createBlock(function, "block_0x3000");

        Value temp0 = builder.createTemporary();
        Value temp1 = builder.createTemporary();

        // Two instructions at the same source address
        block.instructions.push_back(builder.makeInstruction(
            Opcode::LOAD, {Value::makeAddress(0x80030000)}, {temp0}, 0x80003000));
        block.instructions.push_back(builder.makeInstruction(
            Opcode::ADD, {temp0, Value::makeImmediate(8)}, {temp1}, 0x80003000));
        // One instruction at a different source address
        block.instructions.push_back(builder.makeInstruction(
            Opcode::STORE, {Value::makeAddress(0x80030010), temp1}, {}, 0x80003004));
        block.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80003008));

        psxrecomp::recompiler::CodeGenerator generator;
        std::string source = generator.generateSource(program, "guard_merge_module");

        // The resume guard for 0x3000 should appear exactly once (merged).
        size_t guardCount = countOccurrences(source, "resumeAddress == 0x3000");
        assert(guardCount == 1 &&
               "Expected exactly one resume guard for merged same-address instructions");
        std::cerr << "  Test 3 passed: same-address instructions share one resume guard\n";
    }

    // -----------------------------------------------------------------------
    // Test 4: CSE does not create cross-source-address temporary references
    //
    // Two instructions at different source addresses compute the same
    // expression.  After CSE, the second should NOT reference the first's
    // temporary (that would create a cross-address dependency).
    // -----------------------------------------------------------------------
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("test_cse_scope", 0x80004000);
        auto& block = builder.createBlock(function, "block_0x4000");

        Value temp0 = builder.createTemporary();
        Value temp1 = builder.createTemporary();

        // Same computation at two different source addresses
        block.instructions.push_back(builder.makeInstruction(
            Opcode::ADD, {Value::makeRegister(29), Value::makeImmediate(16)}, {temp0}, 0x80004000));
        block.instructions.push_back(builder.makeInstruction(
            Opcode::ADD, {Value::makeRegister(29), Value::makeImmediate(16)}, {temp1}, 0x80004004));
        block.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80004008));

        auto stats = psxrecomp::ir::runOptimizations(function);
        // CSE should NOT merge these since they're at different source addresses.
        assert(stats.cseReplacements == 0 && "CSE must not merge across source addresses");

        // Verify the function is resume-safe after optimization.
        auto diagnostics = verifyResumeSafety(function);
        assert(diagnostics.empty() && "Function should be resume-safe after scoped CSE");
        std::cerr << "  Test 4 passed: CSE scoped per source address\n";
    }

    // -----------------------------------------------------------------------
    // Test 5: CSE still merges within the same source address
    //
    // Two identical computations at the same source address should still
    // be merged by CSE.
    // -----------------------------------------------------------------------
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("test_cse_same_addr", 0x80005000);
        auto& block = builder.createBlock(function, "block_0x5000");

        Value temp0 = builder.createTemporary();
        Value temp1 = builder.createTemporary();

        // Same computation, same source address
        block.instructions.push_back(builder.makeInstruction(
            Opcode::ADD, {Value::makeRegister(29), Value::makeImmediate(16)}, {temp0}, 0x80005000));
        block.instructions.push_back(builder.makeInstruction(
            Opcode::ADD, {Value::makeRegister(29), Value::makeImmediate(16)}, {temp1}, 0x80005000));
        block.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80005004));

        auto stats = psxrecomp::ir::runOptimizations(function);
        // CSE SHOULD merge these since they share a source address.
        assert(stats.cseReplacements == 1 && "CSE should merge within same source address");
        std::cerr << "  Test 5 passed: CSE merges within same source address\n";
    }

    // -----------------------------------------------------------------------
    // Test 6: Instructions without source addresses are guarded
    //
    // An IR function with a mix of sourced and unsourced instructions.
    // The generated code should NOT emit unsourced instructions outside
    // any resume guard (they must stay inside the preceding guard).
    // -----------------------------------------------------------------------
    {
        Program program;
        Builder builder(program);
        auto& function = builder.createFunction("test_unsourced_guard", 0x80006000);
        auto& block = builder.createBlock(function, "block_0x6000");

        Value temp0 = builder.createTemporary();
        Value temp1 = builder.createTemporary();

        // Sourced instruction
        block.instructions.push_back(builder.makeInstruction(
            Opcode::LOAD, {Value::makeAddress(0x80060000)}, {temp0}, 0x80006000));
        // Unsourced continuation (part of the same MIPS instruction)
        block.instructions.push_back(
            builder.makeInstruction(Opcode::ADD, {temp0, Value::makeImmediate(1)}, {temp1}));
        // Next sourced instruction at a different address
        block.instructions.push_back(builder.makeInstruction(
            Opcode::STORE, {Value::makeAddress(0x80060010), temp1}, {}, 0x80006004));
        block.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80006008));

        psxrecomp::recompiler::CodeGenerator generator;
        std::string source = generator.generateSource(program, "unsourced_guard_module");

        // The ADD (temp1 = temp0 + 1) should be inside the 0x6000 guard.
        // If it were outside any guard, it would always execute even during
        // resume at 0x6004, using stale temp0.  Check that the guard for
        // 0x6004 opens AFTER the ADD, not before it.
        //
        // In the generated code the pattern should be:
        //   if (resumeAddress == 0 || resumeAddress == 0x6000) {
        //     ... readMemory32 ...
        //     ... temp + 1 ...
        //   }
        //   if (resumeAddress == 0 || resumeAddress == 0x6004) {
        //     ... writeMemory32 ...
        //   }
        size_t guard6000 = source.find("resumeAddress == 0x6000");
        size_t guard6004 = source.find("resumeAddress == 0x6004");
        assert(guard6000 != std::string::npos);
        assert(guard6004 != std::string::npos);
        assert(guard6000 < guard6004);
        // The ADD operation should appear between guard6000 and guard6004.
        size_t addOp = source.find("+ 1", guard6000);
        assert(addOp != std::string::npos && addOp < guard6004 &&
               "Unsourced ADD must be inside the 0x6000 resume guard");
        std::cerr << "  Test 6 passed: unsourced instructions inside preceding guard\n";
    }

    std::cerr << "All resume safety tests passed.\n";
    return 0;
}
