#include "psxrecomp/ir/control_flow.h"

#include <algorithm>
#include <stack>

namespace psxrecomp
{
namespace ir
{

std::optional<size_t> ControlFlowGraph::indexOf(std::string_view name) const
{
#if defined(__cpp_lib_generic_unordered_lookup) && __cpp_lib_generic_unordered_lookup >= 201811L
    auto it = blockIndex.find(name);
#else
    auto it = blockIndex.find(std::string(name));
#endif
    if (it == blockIndex.end())
    {
        return std::nullopt;
    }
    return it->second;
}

const BasicBlock* ControlFlowGraph::block(size_t index) const
{
    if (index >= blocks.size())
    {
        return nullptr;
    }
    return blocks[index];
}

ControlFlowGraph buildControlFlowGraph(const Function& function, std::vector<std::string>* errors)
{
    ControlFlowGraph graph;
    graph.blocks.reserve(function.blocks.size());
    graph.successors.resize(function.blocks.size());
    graph.predecessors.resize(function.blocks.size());

    for (size_t index = 0; index < function.blocks.size(); ++index)
    {
        auto& block = function.blocks[index];
        graph.blocks.push_back(&block);
        auto [it, inserted] = graph.blockIndex.emplace(block.name, index);
        if (!inserted)
        {
            if (errors)
            {
                errors->push_back("Duplicate basic block name '" + block.name + "'");
            }
        }
    }

    for (size_t index = 0; index < function.blocks.size(); ++index)
    {
        const auto& block = function.blocks[index];
        for (const auto& successorName : block.successors)
        {
            auto it = graph.blockIndex.find(successorName);
            if (it == graph.blockIndex.end())
            {
                if (errors)
                {
                    errors->push_back("Missing successor block '" + successorName +
                                      "' referenced by '" + block.name + "'");
                }
                continue;
            }
            size_t successorIndex = it->second;
            graph.successors[index].push_back(successorIndex);
            graph.predecessors[successorIndex].push_back(index);
        }
    }

    return graph;
}

std::vector<size_t> computeReversePostOrder(const ControlFlowGraph& graph)
{
    std::vector<size_t> order;
    if (graph.blocks.empty())
    {
        return order;
    }

    const size_t numBlocks = graph.blocks.size();
    std::vector<bool> visited(numBlocks, false);

    auto dfsFrom = [&](size_t start)
    {
        std::stack<size_t> stack;
        stack.push(start);

        while (!stack.empty())
        {
            size_t node = stack.top();
            if (visited[node])
            {
                stack.pop();
                if (node < numBlocks)
                {
                    order.push_back(node);
                }
                continue;
            }

            visited[node] = true;
            for (auto successor : graph.successors[node])
            {
                if (successor < numBlocks && !visited[successor])
                {
                    stack.push(successor);
                }
            }
        }
    };

    for (size_t index = 0; index < numBlocks; ++index)
    {
        if (!visited[index] && graph.predecessors[index].empty())
        {
            dfsFrom(index);
        }
    }

    for (size_t index = 0; index < numBlocks; ++index)
    {
        if (!visited[index])
        {
            dfsFrom(index);
        }
    }

    std::reverse(order.begin(), order.end());
    return order;
}

} // namespace ir
} // namespace psxrecomp
