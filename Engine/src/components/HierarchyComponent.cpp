#include "HierarchyComponent.h"

namespace Rapture {

void HierarchyComponent::setParent(Entity child, Entity newParent)
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

    childHier.parent = newParent;

    if (newParent.isValid()) {
        if (!newParent.hasComponent<HierarchyComponent>()) {
            newParent.addComponent<HierarchyComponent>();
        }
        newParent.getComponent<HierarchyComponent>().addChild(child);
    }
}

void HierarchyComponent::removeFromParent(Entity child)
{
    if (!child.isValid()) return;

    auto *childHier = child.tryGetComponent<HierarchyComponent>();
    if (!childHier || !childHier->hasParent()) return;

    if (childHier->parent.isValid()) {
        if (auto *parentHier = childHier->parent.tryGetComponent<HierarchyComponent>()) {
            parentHier->removeChild(child);
        }
    }
    childHier->parent = Entity::null();
}

void HierarchyComponent::destroyHierarchy(Entity entity)
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

void HierarchyComponent::destroyKeepChildren(Entity entity)
{
    if (!entity.isValid()) return;

    Entity parent;
    if (auto *hier = entity.tryGetComponent<HierarchyComponent>()) {
        parent = hier->parent;
        auto childrenCopy = hier->children;
        for (Entity child : childrenCopy) {
            setParent(child, parent);
        }
    }

    removeFromParent(entity);
    entity.destroy();
}

Entity HierarchyComponent::getRoot(Entity entity)
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
