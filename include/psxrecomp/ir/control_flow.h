#pragma once

#include "psxrecomp/ir/ir.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace psxrecomp
{
namespace ir
{

struct ControlFlowGraph
{
    std::vector<const BasicBlock*> blocks;
    std::vector<std::vector<size_t>> successors;
    std::vector<std::vector<size_t>> predecessors;
    std::unordered_map<std::string, size_t> blockIndex;

    std::optional<size_t> indexOf(std::string_view name) const;
    const BasicBlock* block(size_t index) const;
};

ControlFlowGraph buildControlFlowGraph(const Function& function,
                                       std::vector<std::string>* errors = nullptr);
std::vector<size_t> computeReversePostOrder(const ControlFlowGraph& graph);

} // namespace ir
} // namespace psxrecomp
