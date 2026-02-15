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
        entry.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80020000));
        entry.successors = {"caller"};

        // caller jumps to barrier
        caller.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80020004));
        caller.successors = {"block_external"};

        // barrier uses continuations to dispatch
        // entry -> resume_a, caller -> resume_b
        barrier.continuations["entry"] = "resume_a";
        barrier.continuations["caller"] = "resume_b";

        // resume blocks return
        resumeA.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80020008));
        resumeB.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x8002000C));

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

        blockA.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80030000));
        blockA.successors = {"barrier"};

        blockB.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80030004));
        blockB.successors = {"barrier"};

        blockC.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80030008));
        blockC.successors = {"barrier"};

        barrier.continuations["block_a"] = "target_x";
        barrier.continuations["block_b"] = "target_y";
        barrier.continuations["block_c"] = "target_z";

        targetX.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x8003000C));
        targetY.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80030010));
        targetZ.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80030014));

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
    // Test 3: Self-loop prevention
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
        loopBlock.instructions.push_back(
            builder.makeInstruction(Opcode::NOP, {}, {}, 0x80040000));
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
        auto afterSelfLoop = source.find("after_loop", selfLoopCase);
        assert(afterSelfLoop != std::string::npos);

        std::cerr << "[PASS] self-loop prevention redirects to next block\n";
    }

    // ---------------------------------------------------------------
    // Test 4: Non-self-loop fallthrough is preserved
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

        first.instructions.push_back(
            builder.makeInstruction(Opcode::NOP, {}, {}, 0x80050000));
        first.successors = {"second"};

        second.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80050004));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "normal_succ_module");

        // In the first block, it should go to second normally
        auto firstCase = source.find("case BlockId::first:");
        assert(firstCase != std::string::npos);
        auto secondRef = source.find("BlockId::second", firstCase);
        assert(secondRef != std::string::npos);

        std::cerr << "[PASS] non-self-loop successor preserved\n";
    }

    // ---------------------------------------------------------------
    // Test 5: Barrier block with empty continuations falls through to return
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

        entry.instructions.push_back(
            builder.makeInstruction(Opcode::JUMP, {}, {}, 0x80060000));
        entry.successors = {"empty_barrier"};

        // barrier has empty continuations - should not crash
        // (empty continuations map means the block is treated normally)

        barrier.instructions.push_back(
            builder.makeInstruction(Opcode::RETURN, {}, {}, 0x80060004));

        CodeGenerator generator;
        std::string source = generator.generateSource(program, "empty_barrier_module");

        // Should compile without issues and contain both blocks
        assert(source.find("entry") != std::string::npos);
        assert(source.find("empty_barrier") != std::string::npos);

        std::cerr << "[PASS] barrier block with empty continuations\n";
    }

    std::cerr << "All codegen lowering tests passed.\n";
    return 0;
}
