#pragma once

#include "cpp_emitter.h"
#include "psxrecomp/ir/ir.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace recompiler
{

struct LoweringContext
{
    std::map<u32, std::string> temporaries;
    bool generateComments = true;
    bool enableOptimizations = true;
};

std::string valueToExpr(const ir::Value& value, LoweringContext& context);
std::string valueToLoadMergeExpr(const ir::Value& value, LoweringContext& context);
std::optional<std::string> valueToWriteExpr(const ir::Value& value, LoweringContext& context);
std::optional<Register> valueToLoadDelayRegister(const ir::Value& value);
void emitLoadResultWrite(const ir::Value& output, const std::string& resultExpr,
                         LoweringContext& context, CppEmitter& emitter);
std::string opcodeToComment(ir::Opcode opcode);
std::set<u32> collectTemporaries(const ir::Function& function);
std::string resolveBlockId(std::string_view name,
                           const std::unordered_map<std::string, std::string>& blockNames);
std::unordered_map<std::string, size_t> buildBlockIndex(const ir::Function& function);
std::vector<std::vector<std::string>>
buildPredecessors(const ir::Function& function,
                  const std::unordered_map<std::string, size_t>& indexMap);
void emitPhiAssignments(const ir::BasicBlock& block, const std::vector<std::string>& predecessors,
                        const std::unordered_map<std::string, std::string>& blockNames,
                        LoweringContext& context, CppEmitter& emitter);
bool emitControlFlowInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                                const std::unordered_map<std::string, std::string>& blockNames,
                                LoweringContext& context, CppEmitter& emitter);
bool emitMemoryAndSystemInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                                    const std::unordered_map<std::string, std::string>& blockNames,
                                    LoweringContext& context, CppEmitter& emitter);
void emitInstruction(const ir::Instruction& instruction, const ir::BasicBlock& block,
                     const std::unordered_map<std::string, std::string>& blockNames,
                     LoweringContext& context, CppEmitter& emitter);

} // namespace recompiler
} // namespace psxrecomp
