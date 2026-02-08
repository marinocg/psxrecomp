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

struct TransparentStringHash
{
    using is_transparent = void;

    size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }

    size_t operator()(const std::string& value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentStringEqual
{
    using is_transparent = void;

    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept
    {
        return lhs == rhs;
    }

    bool operator()(const std::string& lhs, const std::string& rhs) const noexcept
    {
        return lhs == rhs;
    }

    bool operator()(const std::string& lhs, std::string_view rhs) const noexcept
    {
        return lhs == rhs;
    }

    bool operator()(std::string_view lhs, const std::string& rhs) const noexcept
    {
        return lhs == rhs;
    }
};

struct ControlFlowGraph
{
    std::vector<const BasicBlock*> blocks;
    std::vector<std::vector<size_t>> successors;
    std::vector<std::vector<size_t>> predecessors;
    std::unordered_map<std::string, size_t, TransparentStringHash, TransparentStringEqual>
        blockIndex;

    std::optional<size_t> indexOf(std::string_view name) const;
    const BasicBlock* block(size_t index) const;
};

ControlFlowGraph buildControlFlowGraph(const Function& function,
                                       std::vector<std::string>* errors = nullptr);
std::vector<size_t> computeReversePostOrder(const ControlFlowGraph& graph);

} // namespace ir
} // namespace psxrecomp
