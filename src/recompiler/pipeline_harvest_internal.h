#pragma once

#include "pipeline_analysis_helpers.h"

#include <unordered_map>
#include <unordered_set>

namespace psxrecomp
{
namespace recompiler
{
namespace detail
{

using InstructionIndexMap = std::unordered_map<Address, size_t>;

u32 readProgramWord(const std::vector<u8>& programData, Address baseAddress, Address address);

bool isAddressInRanges(const std::vector<disasm::AddressRange>& ranges, Address address);

bool isDecodableInstruction(const std::vector<disasm::Instruction>& disassembled,
                            const InstructionIndexMap& instructionIndexMap, Address address);

bool looksLikeFunctionEntry(const std::vector<disasm::Instruction>& disassembled,
                            const InstructionIndexMap& instructionIndexMap, Address address);

bool looksLikeIndirectTargetEntry(const std::vector<disasm::Instruction>& disassembled,
                                  const InstructionIndexMap& instructionIndexMap, Address address);

bool looksLikeCallableCodeRegion(const std::vector<disasm::Instruction>& disassembled,
                                 const InstructionIndexMap& instructionIndexMap, Address address);

bool isGapAdjacentEntryCandidate(const std::vector<disasm::Instruction>& disassembled,
                                 const InstructionIndexMap& instructionIndexMap,
                                 const std::vector<disasm::FunctionBoundary>& knownBoundaries,
                                 Address address);

bool looksLikeGapAdjacentCallableEntry(
    const std::vector<disasm::Instruction>& disassembled,
    const InstructionIndexMap& instructionIndexMap,
    const std::vector<disasm::FunctionBoundary>& knownBoundaries, Address address);

bool writesRegister(const disasm::Instruction& instruction, Register reg);

std::unordered_set<Address>
findReferencedDataWords(const std::vector<disasm::Instruction>& disassembled,
                        const disasm::CodeDataSegmentation& segmentation, Address baseAddress,
                        Address endAddress);

void appendUniqueSorted(std::vector<Address>& values, Address value);

} // namespace detail
} // namespace recompiler
} // namespace psxrecomp
