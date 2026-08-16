#ifndef RAPTURE__NODE_REGISTRY_H
#define RAPTURE__NODE_REGISTRY_H

#include "MaterialGraphTypes.h"

namespace Rapture {

/**
 * @brief Data-driven catalogue of node definitions keyed by type
 *
 * The compiler looks node types up here instead of switching on them, so a new node
 * is a registerNode call, not a compiler edit.
 */
class NodeRegistry {
  public:
    /**
     * @brief Look up a node definition by its type
     * @param type The node type
     * @return Pointer to the definition, or nullptr if the type is unknown
     */
    static const NodeDefinition *get(GraphNodeType type);

    /**
     * @brief Add or replace a node definition
     * @param def The node definition to register (keyed by its type)
     */
    static void registerNode(NodeDefinition def);

    /**
     * @brief Register the built-in starter node set (idempotent)
     */
    static void registerBuiltins();
};

} // namespace Rapture

#endif // RAPTURE__NODE_REGISTRY_H
