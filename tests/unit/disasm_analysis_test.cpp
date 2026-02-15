#include "psxrecomp/disasm/analysis.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace
{

uint32_t encodeR(uint8_t rs, uint8_t rt, uint8_t rd, uint8_t shamt, uint8_t funct)
{
    return (static_cast<uint32_t>(rs) << 21) | (static_cast<uint32_t>(rt) << 16) |
           (static_cast<uint32_t>(rd) << 11) | (static_cast<uint32_t>(shamt) << 6) | funct;
}

uint32_t encodeI(uint8_t op, uint8_t rs, uint8_t rt, int16_t imm)
{
    return (static_cast<uint32_t>(op) << 26) | (static_cast<uint32_t>(rs) << 21) |
           (static_cast<uint32_t>(rt) << 16) | static_cast<uint16_t>(imm);
}

uint32_t encodeJ(uint8_t op, uint32_t target)
{
    return (static_cast<uint32_t>(op) << 26) | (target & 0x03FFFFFFu);
}

void appendLe32(std::vector<uint8_t>& buffer, uint32_t value)
{
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

} // namespace

int main()
{
    using psxrecomp::Address;
    using psxrecomp::disasm::buildCallGraph;
    using psxrecomp::disasm::CodeDataSegmentation;
    using psxrecomp::disasm::findFunctionBoundaries;
    using psxrecomp::disasm::findIndirectBranchTargets;
    using psxrecomp::disasm::findJumpTables;
    using psxrecomp::disasm::Instruction;
    using psxrecomp::disasm::InstructionType;
    using psxrecomp::disasm::JumpTableInfo;
    using psxrecomp::disasm::MipsDisassembler;
    using psxrecomp::disasm::Opcode;
    using psxrecomp::disasm::segmentCodeAndData;

    const Address baseAddress = 0x80010000;
    std::vector<uint8_t> buffer;

    appendLe32(buffer, encodeI(0x09, 29, 29, -16));        // addiu sp, sp, -16
    appendLe32(buffer, encodeI(0x2B, 29, 31, 12));         // sw ra, 12(sp)
    appendLe32(buffer, encodeJ(0x03, (0x80010020u >> 2))); // jal 0x80010020
    appendLe32(buffer, encodeR(0, 0, 0, 0, 0x00));         // nop
    appendLe32(buffer, encodeI(0x23, 29, 31, 12));         // lw ra, 12(sp)
    appendLe32(buffer, encodeI(0x09, 29, 29, 16));         // addiu sp, sp, 16
    appendLe32(buffer, encodeR(31, 0, 0, 0, 0x08));        // jr ra
    appendLe32(buffer, encodeR(0, 0, 0, 0, 0x00));         // nop

    appendLe32(buffer, encodeI(0x09, 29, 29, -8));  // addiu sp, sp, -8
    appendLe32(buffer, encodeI(0x2B, 29, 31, 4));   // sw ra, 4(sp)
    appendLe32(buffer, encodeR(31, 0, 0, 0, 0x08)); // jr ra
    appendLe32(buffer, encodeR(0, 0, 0, 0, 0x00));  // nop

    appendLe32(buffer, encodeI(0x0F, 0, 8, static_cast<int16_t>(0x8001))); // lui t0, 0x8001
    appendLe32(buffer, encodeI(0x09, 8, 8, 0x0040));                       // addiu t0, t0, 0x0040
    appendLe32(buffer, encodeR(0, 4, 9, 2, 0x00));                         // sll t1, a0, 2
    appendLe32(buffer, encodeR(8, 9, 8, 0, 0x21));                         // addu t0, t0, t1
    appendLe32(buffer, encodeI(0x23, 8, 10, 0));                           // lw t2, 0(t0)
    appendLe32(buffer, encodeR(10, 0, 0, 0, 0x08));                        // jr t2
    appendLe32(buffer, encodeR(0, 0, 0, 0, 0x00));                         // nop

    appendLe32(buffer, 0xFFFFFFFFu); // data word

    std::vector<Instruction> instructions =
        MipsDisassembler::disassemble(buffer.data(), buffer.size(), baseAddress);

    auto boundaries = findFunctionBoundaries(instructions);
    assert(boundaries.size() == 2);
    assert(boundaries[0].start == 0x80010000);
    assert(boundaries[0].end == 0x8001001C);
    assert(boundaries[0].hasPrologue);
    assert(boundaries[0].hasEpilogue);
    assert(boundaries[1].start == 0x80010020);
    assert(boundaries[1].end == 0x8001002C);

    auto callGraph = buildCallGraph(instructions, boundaries);
    assert(callGraph.functions.size() == 2);
    assert(callGraph.functions[0] == 0x80010000);
    assert(callGraph.functions[1] == 0x80010020);
    assert(callGraph.edges.size() == 1);
    assert(callGraph.edges[0].caller == 0x80010000);
    assert(callGraph.edges[0].callSite == 0x80010008);
    assert(callGraph.edges[0].callee == 0x80010020);

    auto indirectTargets = findIndirectBranchTargets(instructions);
    assert(indirectTargets.size() == 1);
    assert(indirectTargets[0].address == 0x80010044);

    std::vector<JumpTableInfo> jumpTables = findJumpTables(instructions);
    assert(jumpTables.size() == 1);
    assert(jumpTables[0].jumpAddress == 0x80010044);
    assert(jumpTables[0].tableBaseAddress.has_value());
    assert(*jumpTables[0].tableBaseAddress == 0x80010040);

    CodeDataSegmentation segmentation =
        segmentCodeAndData(instructions, {0x80010000, 0x80010030}, jumpTables);

    assert(segmentation.codeRanges.size() == 1);
    assert(segmentation.codeRanges[0].start == 0x80010000);
    assert(segmentation.codeRanges[0].end == 0x80010048);

    assert(segmentation.dataRanges.size() == 1);
    assert(segmentation.dataRanges[0].start == 0x8001004C);
    assert(segmentation.dataRanges[0].end == 0x8001004C);

    std::vector<Instruction> duplicateCallInstructions;
    duplicateCallInstructions.push_back(Instruction{0x80020008, encodeJ(0x03, (0x80030000u >> 2)),
                                                    Opcode::JAL, InstructionType::J_TYPE, 0, 0, 0,
                                                    0, (0x80030000u >> 2), 0, false, std::nullopt});
    duplicateCallInstructions.push_back(Instruction{0x80020008, encodeJ(0x03, (0x80030100u >> 2)),
                                                    Opcode::JAL, InstructionType::J_TYPE, 0, 0, 0,
                                                    0, (0x80030100u >> 2), 0, false, std::nullopt});

    auto duplicateCallGraph =
        buildCallGraph(duplicateCallInstructions, {{0x80020000, 0x80020020, false, false}});
    assert(duplicateCallGraph.functions.size() == 3);
    assert(duplicateCallGraph.edges.size() == 2);
    assert(duplicateCallGraph.edges[0].callee == 0x80030000);
    assert(duplicateCallGraph.edges[1].callee == 0x80030100);

    // ---------------------------------------------------------------
    // Entry-function fall-through merge
    //
    // When the entry point has no control-flow terminator before the
    // next detected prologue, the two regions must be merged into a
    // single function.
    //
    // Layout:
    //   0x80010000  lui   t0, 0x8002          (entry, no prologue)
    //   0x80010004  ori   t0, t0, 0x0000
    //   0x80010008  addiu sp, sp, -16         <-- prologue detected
    //   0x8001000C  sw    ra, 12(sp)
    //   0x80010010  jr    ra
    //   0x80010014  nop
    // Without merge: two boundaries [0x80010000..0x80010004]
    //                               [0x80010008..0x80010014]
    // With merge:    one boundary   [0x80010000..0x80010014]
    // ---------------------------------------------------------------
    {
        const Address mergeBase = 0x80010000;
        std::vector<uint8_t> mergeBuffer;

        // Entry stub – no prologue, no terminator, falls through.
        appendLe32(mergeBuffer,
                   encodeI(0x0F, 0, 8, static_cast<int16_t>(0x8002))); // lui t0, 0x8002
        appendLe32(mergeBuffer, encodeI(0x0D, 8, 8, 0x0000));          // ori t0, t0, 0

        // Prologue of the "real" function that follows.
        appendLe32(mergeBuffer, encodeI(0x09, 29, 29, -16)); // addiu sp, sp, -16
        appendLe32(mergeBuffer, encodeI(0x2B, 29, 31, 12));  // sw ra, 12(sp)
        appendLe32(mergeBuffer, encodeI(0x23, 29, 31, 12));  // lw ra, 12(sp)
        appendLe32(mergeBuffer, encodeI(0x09, 29, 29, 16));  // addiu sp, sp, 16
        appendLe32(mergeBuffer, encodeR(31, 0, 0, 0, 0x08)); // jr ra
        appendLe32(mergeBuffer, encodeR(0, 0, 0, 0, 0x00));  // nop

        auto mergeInstructions =
            MipsDisassembler::disassemble(mergeBuffer.data(), mergeBuffer.size(), mergeBase);

        auto mergeBoundaries = findFunctionBoundaries(mergeInstructions);

        // After the merge fix the entry stub and the following function
        // collapse into a single boundary.
        assert(mergeBoundaries.size() == 1);
        assert(mergeBoundaries[0].start == mergeBase);
        assert(mergeBoundaries[0].end == mergeBase + 0x1C);
    }

    // ---------------------------------------------------------------
    // Entry function WITH a terminator should NOT merge.
    //
    // Layout:
    //   0x80010000  j     0x80010010           (entry, has terminator)
    //   0x80010004  nop                        (delay slot)
    //   0x80010008  nop                        (padding)
    //   0x8001000C  nop                        (padding)
    //   0x80010010  addiu sp, sp, -16          <-- prologue detected
    //   0x80010014  sw    ra, 12(sp)
    //   0x80010018  jr    ra
    //   0x8001001C  nop
    // These should remain two separate functions because the entry
    // function contains a jump terminator.
    // ---------------------------------------------------------------
    {
        const Address noMergeBase = 0x80010000;
        std::vector<uint8_t> noMergeBuffer;

        // Entry with a jump — should NOT merge.
        appendLe32(noMergeBuffer, encodeJ(0x02, (0x80010010u >> 2))); // j 0x80010010
        appendLe32(noMergeBuffer, encodeR(0, 0, 0, 0, 0x00));         // nop (delay)
        appendLe32(noMergeBuffer, encodeR(0, 0, 0, 0, 0x00));         // nop
        appendLe32(noMergeBuffer, encodeR(0, 0, 0, 0, 0x00));         // nop

        // Separate function with prologue.
        appendLe32(noMergeBuffer, encodeI(0x09, 29, 29, -16)); // addiu sp, sp, -16
        appendLe32(noMergeBuffer, encodeI(0x2B, 29, 31, 12));  // sw ra, 12(sp)
        appendLe32(noMergeBuffer, encodeI(0x23, 29, 31, 12));  // lw ra, 12(sp)
        appendLe32(noMergeBuffer, encodeI(0x09, 29, 29, 16));  // addiu sp, sp, 16
        appendLe32(noMergeBuffer, encodeR(31, 0, 0, 0, 0x08)); // jr ra
        appendLe32(noMergeBuffer, encodeR(0, 0, 0, 0, 0x00));  // nop

        auto noMergeInstructions =
            MipsDisassembler::disassemble(noMergeBuffer.data(), noMergeBuffer.size(), noMergeBase);

        auto noMergeBoundaries = findFunctionBoundaries(noMergeInstructions);

        // Two functions: the entry jump stub and the prologue function.
        assert(noMergeBoundaries.size() == 2);
        assert(noMergeBoundaries[0].start == noMergeBase);
        assert(noMergeBoundaries[1].start == noMergeBase + 0x10);
    }

    return 0;
}
