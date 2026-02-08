#include "psxrecomp/ir/control_flow.h"

#include <algorithm>
#include <stack>

namespace psxrecomp
{
namespace ir
{

std::optional<size_t> ControlFlowGraph::indexOf(std::string_view name) const
{
    auto it = blockIndex.find(std::string(name));
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
        graph.blockIndex.emplace(block.name, index);
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
                    errors->push_back("Missing successor block '" + successorName + "' referenced by '" +
                                      block.name + "'");
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

    std::vector<bool> visited(graph.blocks.size(), false);
    std::stack<size_t> stack;
    stack.push(0);

    while (!stack.empty())
    {
        size_t node = stack.top();
        if (visited[node])
        {
            stack.pop();
            if (node < graph.blocks.size())
            {
                order.push_back(node);
            }
            continue;
        }

        visited[node] = true;
        for (auto successor : graph.successors[node])
        {
            if (!visited[successor])
            {
                stack.push(successor);
            }
        }
    }

    std::reverse(order.begin(), order.end());
    return order;
}

} // namespace ir
} // namespace psxrecomp
