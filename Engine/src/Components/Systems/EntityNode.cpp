#include "EntityNode.h"

#include <algorithm> // Required for std::remove

namespace Rapture {

EntityNode::EntityNode(std::shared_ptr<Entity> entity) : m_entity(std::move(entity))
{
    m_parent.reset();
    m_children.clear();
}

EntityNode::EntityNode(std::shared_ptr<Entity> entity, std::shared_ptr<EntityNode> parent)
    : m_entity(std::move(entity)), m_parent(parent)
{
    m_children.clear();
}

EntityNode::~EntityNode()
{

    auto parent = m_parent.lock();
    for (size_t i = 0; i < m_children.size(); ++i) {
        if (auto child = m_children[i].lock()) {
            child->setParent(parent);
            parent->addChild(child);
        }
    }

    // Detach from parent
    if (auto parent = m_parent.lock()) {
        parent->removeChild(shared_from_this());
        m_parent.reset();
    }

    m_children.clear(); // Clear the list of raw pointers
}

void EntityNode::addChild(std::shared_ptr<EntityNode> childNode)
{
    if (!childNode)
        return;

    // Check if child already has a parent and remove it from there first
    if (auto existingParent = childNode->getParent()) {
        if (existingParent->getEntity()->getID() != m_entity->getID()) // Avoid removing/adding if already the parent
        {
            existingParent->removeChild(childNode);
        } else {
            return; // Already a child of this node
        }
    }

    // Set this as the child's parent (internal raw pointer link)
    childNode->setParent(shared_from_this());

    // Add pointer to children collection if not already present
    if (std::find_if(m_children.begin(), m_children.end(),
                     [childNode](const std::weak_ptr<EntityNode> &ptr) { return ptr.lock() == childNode; }) == m_children.end()) {
        m_children.push_back(childNode);
    }
}

// Remove a specific child node using shared_ptr
void EntityNode::removeChild(std::shared_ptr<EntityNode> childNode)
{
    if (!childNode)
        return;

    // Find and remove the pointer from the children list
    auto it = std::find_if(m_children.begin(), m_children.end(),
                           [childNode](const std::weak_ptr<EntityNode> &ptr) { return ptr.lock() == childNode; });
    if (it != m_children.end()) {
        m_children.erase(it);
        // Detach the child from this parent internally
        childNode->setParent(m_parent.lock() ? m_parent.lock() : nullptr);
    }
}

// Sets the parent node using shared_ptr
void EntityNode::setParent(std::shared_ptr<EntityNode> parentNode)
{
    if (!parentNode)
        return;

    // If currently have a parent, detach from it
    auto parent = m_parent.lock();
    if (parent && parent->getEntity()->getID() != parentNode->getEntity()->getID()) {
        parent->removeChild(shared_from_this());
    }

    // update the new parent that it has a new child
    if (parentNode) {
        if (std::find_if(parentNode->m_children.begin(), parentNode->m_children.end(),
                         [this](const std::weak_ptr<EntityNode> &ptr) { return ptr.lock() == shared_from_this(); }) ==
            parentNode->m_children.end()) {
            parentNode->addChild(shared_from_this());
        }
    }

    m_parent = parentNode;
}

void EntityNode::removeParent()
{
    setParent(nullptr);
}

// Getters using shared_ptr for safety

std::shared_ptr<Entity> EntityNode::getEntity() const
{
    return m_entity;
}

std::vector<std::shared_ptr<EntityNode>> EntityNode::getChildren() const
{
    std::vector<std::shared_ptr<EntityNode>> childrenSharedPtrs;
    childrenSharedPtrs.reserve(m_children.size());
    for (auto childNode : m_children) {
        if (childNode.lock()) {
            childrenSharedPtrs.push_back(childNode.lock());
        }
    }

    return childrenSharedPtrs;
}

std::shared_ptr<EntityNode> EntityNode::getParent() const
{
    if (auto parent = m_parent.lock()) {
        return parent;
    }
    return nullptr; // No parent
}

} // namespace Rapture
