#ifndef RAPTURE__HIERARCHYCOMPONENT_H
#define RAPTURE__HIERARCHYCOMPONENT_H

#include "Scenes/Entities/Entity.h"
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

    void setParent(Entity newParent) { parent = newParent; }

    void clearParent() { parent = Entity::null(); }

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
};

inline void setParent(Entity child, Entity newParent)
{
    if (!child.isValid()) return;

    if (!child.hasComponent<HierarchyComponent>()) {
        child.addComponent<HierarchyComponent>();
    }
    auto &childHier = child.getComponent<HierarchyComponent>();

    if (childHier.hasParent() && childHier.parent.isValid()) {
        if (auto *oldParentHier = childHier.parent.tryGetComponent<HierarchyComponent>()) {
            oldParentHier->removeChild(child);
        }
    }

    childHier.setParent(newParent);

    if (newParent.isValid()) {
        if (!newParent.hasComponent<HierarchyComponent>()) {
            newParent.addComponent<HierarchyComponent>();
        }
        newParent.getComponent<HierarchyComponent>().addChild(child);
    }
}

inline void removeFromParent(Entity child)
{
    if (!child.isValid()) return;

    auto *childHier = child.tryGetComponent<HierarchyComponent>();
    if (!childHier || !childHier->hasParent()) return;

    if (childHier->parent.isValid()) {
        if (auto *parentHier = childHier->parent.tryGetComponent<HierarchyComponent>()) {
            parentHier->removeChild(child);
        }
    }
    childHier->clearParent();
}

inline void destroyHierarchy(Entity entity)
{
    if (!entity.isValid()) return;

    removeFromParent(entity);

    if (auto *hier = entity.tryGetComponent<HierarchyComponent>()) {
        auto childrenCopy = hier->children;
        for (Entity child : childrenCopy) {
            destroyHierarchy(child);
        }
    }

    entity.destroy();
}

// Gets root ancestor of an entity (walks up the tree).
inline Entity getRoot(Entity entity)
{
    if (!entity.isValid()) return Entity::null();

    Entity current = entity;
    while (current.isValid()) {
        auto *hier = current.tryGetComponent<HierarchyComponent>();
        if (!hier || !hier->hasParent()) {
            return current;
        }
        current = hier->parent;
    }
    return entity;
}

} // namespace Rapture

#endif
