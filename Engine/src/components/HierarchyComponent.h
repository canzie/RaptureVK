#ifndef RAPTURE__HIERARCHYCOMPONENT_H
#define RAPTURE__HIERARCHYCOMPONENT_H

#include "scenes/entities/Entity.h"
#include <algorithm>
#include <vector>

namespace Rapture {

// Stores parent-child relationships for entities.
// Used for: transform propagation, skeleton/animation, mesh hierarchies, UI display.
// Replaces the old EntityNodeComponent + EntityNode system.
struct HierarchyComponent {
    Entity parent;
    std::vector<Entity> children;

    HierarchyComponent() = default;
    explicit HierarchyComponent(Entity parent) : parent(parent) {}

    bool hasParent() const { return parent.isValid(); }

    bool hasChildren() const { return !children.empty(); }

    size_t childCount() const { return children.size(); }

    void addChild(Entity child)
    {
        if (std::find(children.begin(), children.end(), child) == children.end()) {
            children.push_back(child);
        }
    }

    void removeChild(Entity child) { children.erase(std::remove(children.begin(), children.end(), child), children.end()); }

    bool hasChild(Entity child) const { return std::find(children.begin(), children.end(), child) != children.end(); }

    bool isRoot() const { return !hasParent(); }

    bool isLeaf() const { return !hasChildren(); }

    /**
     * @brief Reparents a child under a new parent, keeping both sides of the link in sync
     * @param child The entity to reparent
     * @param newParent The new parent, or an invalid entity to detach to root
     */
    static void setParent(Entity child, Entity newParent);

    /**
     * @brief Detaches a child from its current parent
     * @param child The entity to detach
     */
    static void removeFromParent(Entity child);

    /**
     * @brief Destroys an entity along with its entire subtree
     * @param entity The entity to destroy
     */
    static void destroyHierarchy(Entity entity);

    /**
     * @brief Destroys an entity and reparents its children up to its parent, or to the root if it has none
     * @param entity The entity to destroy
     */
    static void destroyKeepChildren(Entity entity);

    /**
     * @brief Walks up the tree to find the root ancestor of an entity
     * @param entity The entity to start from
     * @return The root ancestor, or the entity itself if it has no parent
     */
    static Entity getRoot(Entity entity);
};

} // namespace Rapture

#endif
