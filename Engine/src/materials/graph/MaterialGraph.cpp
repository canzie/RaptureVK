#include "MaterialGraph.h"

namespace Rapture {

const GraphNode *MaterialGraph::findNode(uint32_t id) const
{
    for (const auto &node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

} // namespace Rapture
