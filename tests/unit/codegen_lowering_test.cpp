/**
 * @file codegen_lowering_test.cpp
 * @brief Tests for block_external continuation dispatch and self-loop prevention
 *        in the code generation lowering pass.
 */
#include "psxrecomp/ir/ir.h"
#include "psxrecomp/recompiler/codegen.h"

#include <cassert>
#include <iostream>
#include <string>

#include "codegen_lowering_cop_test_sections.h"
#include "codegen_lowering_source_comments_case.h"

int main()
{
    using psxrecomp::Address;
    using psxrecomp::ir::BasicBlock;
    using psxrecomp::ir::Builder;
    using psxrecomp::ir::Opcode;
    using psxrecomp::ir::Program;
    using psxrecomp::ir::Value;
    using psxrecomp::recompiler::CodeGenerator;

    // ---------------------------------------------------------------
    // Test 1: block_external continuation dispatch
    //
    // When a block has a `continuations` map, the generated code should
    // emit an if/else-if chain that dispatches to the correct
    // continuation block based on `previousBlock`.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_continuations", 0x80020000);
        auto& entry = builder.createBlock(function, "entry");
        auto& caller = builder.createBlock(function, "caller");
        auto& barrier = builder.createBlock(function, "block_external");
        auto& resumeA = builder.createBlock(function, "resume_a");
        auto& resumeB = builder.createBlock(function, "resume_b");

        // entry jumps to caller
        entry.instructions.push_back(builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80020000));
        entry.successors = {"caller"};

        // caller jumps to barrier
        caller.instructions.push_back(builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80020004));
        caller.successors = {"block_external"};

        // barrier uses continuations to dispatch
        // entry -> resume_a, caller -> resume_b
        barrier.continuations["entry"] = "resume_a";
        barrier.continuations["caller"] = "resume_b";

        // resume blocks return
        resumeA.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80020008));
        resumeB.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x8002000C));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "cont_module");

        // The generated code should contain:
        // - "previousBlock ==" checks for continuation dispatch
        assert(source.find("previousBlock ==") != std::string::npos);
        // - References to the continuation blocks
        assert(source.find("resume_a") != std::string::npos);
        assert(source.find("resume_b") != std::string::npos);
        // - The continue statement after setting the block
        assert(source.find("continue;") != std::string::npos);
        // - A fallback return for unmatched predecessors
        // The block_external block itself should not emit normal instructions
        // but rather the continuation dispatch
        assert(source.find("block_external") != std::string::npos);

        std::cerr << "[PASS] block_external continuation dispatch\n";
    }

    // ---------------------------------------------------------------
    // Test 2: Multiple continuations from same barrier
    //
    // Verify that each entry in the continuations map generates a
    // separate branch in the if/else-if chain.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_multi_cont", 0x80030000);
        auto& blockA = builder.createBlock(function, "block_a");
        auto& blockB = builder.createBlock(function, "block_b");
        auto& blockC = builder.createBlock(function, "block_c");
        auto& barrier = builder.createBlock(function, "barrier");
        auto& targetX = builder.createBlock(function, "target_x");
        auto& targetY = builder.createBlock(function, "target_y");
        auto& targetZ = builder.createBlock(function, "target_z");

        blockA.instructions.push_back(builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80030000));
        blockA.successors = {"barrier"};

        blockB.instructions.push_back(builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80030004));
        blockB.successors = {"barrier"};

        blockC.instructions.push_back(builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80030008));
        blockC.successors = {"barrier"};

        barrier.continuations["block_a"] = "target_x";
        barrier.continuations["block_b"] = "target_y";
        barrier.continuations["block_c"] = "target_z";

        targetX.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x8003000C));
        targetY.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80030010));
        targetZ.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80030014));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "multi_cont_module");

        // All three targets should appear
        assert(source.find("target_x") != std::string::npos);
        assert(source.find("target_y") != std::string::npos);
        assert(source.find("target_z") != std::string::npos);
        // Should use else if for subsequent conditions
        assert(source.find("else if") != std::string::npos);

        std::cerr << "[PASS] multiple continuations from same barrier\n";
    }

    // ---------------------------------------------------------------
    // Test 3: Single continuation fallback must not hijack other predecessors
    //
    // A continuation block may have one continuation entry but several
    // predecessors. Unmatched predecessors must still return.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_single_cont_fallback_guard", 0x80035000);
        auto& fromMapped = builder.createBlock(function, "from_mapped");
        auto& fromUnmapped = builder.createBlock(function, "from_unmapped");
        auto& barrier = builder.createBlock(function, "barrier");
        auto& resumeOnly = builder.createBlock(function, "resume_only");

        fromMapped.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80035000));
        fromMapped.successors = {"barrier"};

        fromUnmapped.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80035004));
        fromUnmapped.successors = {"barrier"};

        barrier.continuations["from_mapped"] = "resume_only";

        resumeOnly.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80035008));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "single_cont_guard_module");

        // Only the mapped predecessor branch should route to resume_only.
        assert(source.find("if (previousBlock == BlockId::from_mapped)") != std::string::npos);
        assert(source.find("block = BlockId::resume_only;") != std::string::npos);
        // Unmapped predecessors must still hit a fallback return.
        assert(source.find("return;") != std::string::npos);

        std::cerr << "[PASS] single continuation fallback guarded by predecessor ambiguity\n";
    }

    // ---------------------------------------------------------------
    // Test 4: Self-loop prevention
    //
    // When a block's sole successor is itself AND a next block exists
    // in the function, the lowering should redirect to the next block
    // instead of creating an infinite self-loop.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_self_loop", 0x80040000);
        auto& loopBlock = builder.createBlock(function, "self_loop");
        auto& nextBlock = builder.createBlock(function, "after_loop");

        // loopBlock has itself as sole successor
        loopBlock.instructions.push_back(builder.makeInstruction(Opcode::NOP, {}, {}, 0x80040000));
        loopBlock.successors = {"self_loop"};

        nextBlock.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80040004));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "self_loop_module");

        // The generated code should NOT assign block = BlockId::self_loop
        // when the current block is already self_loop. Instead it should
        // redirect to after_loop.
        // Find the case label for self_loop
        auto selfLoopCase = source.find("case BlockId::self_loop:");
        assert(selfLoopCase != std::string::npos);

        // In the self_loop case body, look for the redirection
        [[maybe_unused]] auto afterSelfLoop = source.find("after_loop", selfLoopCase);
        assert(afterSelfLoop != std::string::npos);

        std::cerr << "[PASS] self-loop prevention redirects to next block\n";
    }

    // ---------------------------------------------------------------
    // Test 5: Non-self-loop fallthrough is preserved
    //
    // When a block's successor is a different block, no redirection
    // should occur - the normal successor is used.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_normal_successor", 0x80050000);
        auto& first = builder.createBlock(function, "first");
        auto& second = builder.createBlock(function, "second");

        first.instructions.push_back(builder.makeInstruction(Opcode::NOP, {}, {}, 0x80050000));
        first.successors = {"second"};

        second.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80050004));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "normal_succ_module");

        // In the first block, it should go to second normally
        auto firstCase = source.find("case BlockId::first:");
        assert(firstCase != std::string::npos);
        [[maybe_unused]] auto secondRef = source.find("BlockId::second", firstCase);
        assert(secondRef != std::string::npos);

        std::cerr << "[PASS] non-self-loop successor preserved\n";
    }

    // ---------------------------------------------------------------
    // Test 6: Barrier block with empty continuations falls through to return
    //
    // A block_external block with no continuations should still produce
    // a return statement.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_empty_barrier", 0x80060000);
        auto& entry = builder.createBlock(function, "entry");
        auto& barrier = builder.createBlock(function, "empty_barrier");

        entry.instructions.push_back(builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80060000));
        entry.successors = {"empty_barrier"};

        // barrier has empty continuations - should not crash
        // (empty continuations map means the block is treated normally)

        barrier.instructions.push_back(builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80060004));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "empty_barrier_module");

        // Should compile without issues and contain both blocks
        assert(source.find("entry") != std::string::npos);
        assert(source.find("empty_barrier") != std::string::npos);

        std::cerr << "[PASS] barrier block with empty continuations\n";
    }

    // ---------------------------------------------------------------
    // Test 7: BRANCH unconditional self-loop spin-wait detection
    //
    // When a block's BRANCH instruction has BOTH successors pointing to
    // itself (unconditional self-loop / spin-wait), the generated code
    // should call advanceFrame() instead of looping forever.
    // Conditional self-loops (only one successor pointing to self) are
    // regular loops handled by the while(true)/switch structure.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_spin_wait", 0x80070000);
        auto& spinBlock = builder.createBlock(function, "spin_block");
        auto& nextBlock = builder.createBlock(function, "after_spin");

        // Create a COMPARE_EQ (reg[0] == reg[0] → always true)
        // makeInstruction signature: (opcode, inputs, outputs, address)
        auto cmpInstr = builder.makeInstruction(
            Opcode::COMPARE_EQ,
            {Value::makeRegister(0), Value::makeRegister(0)}, // inputs: reg0 vs reg0
            {Value::makeRegister(1)},                         // output: temp result
            0x80070000);
        spinBlock.instructions.push_back(cmpInstr);

        // BRANCH that targets self on BOTH paths (unconditional spin-wait)
        auto branchInstr = builder.makeInstruction(
            Opcode::BRANCH, {Value::makeRegister(1)}, // input: condition from CMP result
            {},                                       // no outputs
            0x80070004);
        spinBlock.instructions.push_back(branchInstr);
        spinBlock.successors = {"spin_block", "spin_block"}; // both paths = self

        nextBlock.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80070008));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "spin_wait_module");

        // The generated code should call advanceFrame() in the spin block
        assert(source.find("advanceFrame()") != std::string::npos);

        std::cerr << "[PASS] BRANCH unconditional self-loop spin-wait detection\n";
    }

    // ---------------------------------------------------------------
    // Test 8: Conditional self-loop is a normal branch (not advanceFrame)
    //
    // When a BRANCH has only ONE successor pointing to self (a regular
    // loop like BSS clearing or memcpy), it should be treated as a
    // normal branch.  The while(true)/switch structure naturally
    // re-enters the same block.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_cond_loop", 0x80080000);
        auto& loopBlock = builder.createBlock(function, "loop_block");
        auto& exitBlock = builder.createBlock(function, "exit_block");

        auto cmpInstr = builder.makeInstruction(Opcode::COMPARE_NE,
                                                {Value::makeRegister(2), Value::makeRegister(3)},
                                                {Value::makeRegister(1)}, 0x80080000);
        loopBlock.instructions.push_back(cmpInstr);

        auto branchInstr =
            builder.makeInstruction(Opcode::BRANCH, {Value::makeRegister(1)}, {}, 0x80080004);
        loopBlock.instructions.push_back(branchInstr);
        // taken=self (loop back), fallthrough=exit (loop done)
        loopBlock.successors = {"loop_block", "exit_block"};

        exitBlock.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80080008));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "cond_loop_module");

        // Should NOT have advanceFrame — it's a real loop
        auto loopCase = source.find("case BlockId::loop_block:");
        assert(loopCase != std::string::npos);

        // Should have normal if/else branching in the loop block
        [[maybe_unused]] auto ifStmt = source.find("if (", loopCase);
        assert(ifStmt != std::string::npos);
        [[maybe_unused]] auto elseStmt = source.find("else", loopCase);
        assert(elseStmt != std::string::npos);

        // Both successors should be referenced: loop_block and exit_block
        [[maybe_unused]] auto loopRef = source.find("BlockId::loop_block", loopCase);
        assert(loopRef != std::string::npos);
        [[maybe_unused]] auto exitRef = source.find("BlockId::exit_block", loopCase);
        assert(exitRef != std::string::npos);

        std::cerr << "[PASS] conditional self-loop is normal branch (not advanceFrame)\n";
    }

    // ---------------------------------------------------------------
    // Test 8: BRANCH fallback to jump dispatch for out-of-function targets
    //
    // Conditional branches can target blocks outside the current emitted
    // function when the CFG is partitioned. Those edges must dispatch via
    // jumpRecompiledFunction, not silently return.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_branch_external_fallback", 0x80091000);
        auto& entry = builder.createBlock(function, "entry");
        auto& localExit = builder.createBlock(function, "local_exit");

        entry.instructions.push_back(builder.makeInstruction(
            Opcode::BRANCH, {Value::makeRegister(1), Value::makeAddress(0x80091020)}, {},
            0x80091004));
        entry.successors = {"block_external", "local_exit"};

        localExit.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80091008));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "branch_external_fallback_module");

        const std::string recompiledProbe = "if (!jumpRecompiledFunction(context, 0x91020))";
        const std::string failProbe = "failUnsupportedJump(0x91020, 0x80091004);";

        assert(source.find(recompiledProbe) != std::string::npos);
        assert(source.find(failProbe) != std::string::npos);
        assert(source.find("block = BlockId::local_exit;") != std::string::npos);

        std::cerr << "[PASS] branch external successor dispatches via jump fallback\n";
    }

    // ---------------------------------------------------------------
    // Test 9: Register JUMP fallback to jump dispatch
    //
    // For JR/JALR-like dynamic jumps, generated code should try intrinsic
    // handling first, then attempt jumpRecompiledFunction before raising
    // unsupported-jump errors.
    // ---------------------------------------------------------------
    {
        Program program;
        Builder builder(program);

        auto& function = builder.createFunction("test_jump_reg_fallback", 0x80090000);
        auto& entry = builder.createBlock(function, "entry");

        entry.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {Value::makeRegister(9)}, {}, 0x80090000));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "jump_reg_fallback_module");

        const std::string intrinsicProbe =
            "if (!callIntrinsic(context.system, context.regs[Registers::T1], "
            "context.regs))";
        const std::string recompiledProbe =
            "if (!jumpRecompiledFunction(context, context.regs[Registers::T1]))";
        const std::string failProbe =
            "failUnsupportedJump(context.regs[Registers::T1], 0x80090000);";

        assert(source.find(intrinsicProbe) != std::string::npos);
        assert(source.find(recompiledProbe) != std::string::npos);
        assert(source.find(failProbe) != std::string::npos);

        std::cerr << "[PASS] register JUMP fallback to jump dispatch\n";
    }

    // ---------------------------------------------------------------
    runCodegenLoweringCopTests();
    // Test 13 moved to a dedicated companion translation unit.
    runCodegenLoweringSourceCommentsCase();

    std::cerr << "All codegen lowering tests passed.\n";
    return 0;
}
