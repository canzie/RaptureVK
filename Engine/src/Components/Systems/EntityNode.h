#pragma once

#include <memory>
#include <vector>
#include <stdexcept> // For std::runtime_error
#include <algorithm> // For std::find

#include "Scenes/Entities/Entity.h"

namespace Rapture
{
    // Forward declaration
    class EntityNode;

    // Used to create a hierarchy of entities and save the relationships between them
    // Useful for complex meshes with a hierarchy of submeshes and when they each need their own components
    // The lifetime of the EntityNode object is managed by the shared_ptr in the EntityNodeComponent.
    // This class uses raw pointers internally for tree structure to avoid shared_ptr overhead and cycles.
    class EntityNode : public std::enable_shared_from_this<EntityNode>
    {
    public:
        // Constructor remains the same, taking the owning entity
        EntityNode(std::shared_ptr<Entity> entity);
        EntityNode(std::shared_ptr<Entity> entity, std::shared_ptr<EntityNode> parent);
        ~EntityNode();

        // --- Public API (Preserved where possible, using shared_ptr for external interaction) ---

        // Adds a child node. Takes ownership via shared_ptr from component.
        void addChild(std::shared_ptr<EntityNode> child);

        // Removes a specific child node.
        void removeChild(std::shared_ptr<EntityNode> child);

        // Sets the parent node. Internal link is raw pointer.
        void setParent(std::shared_ptr<EntityNode> parent);

        // Removes the parent link.
        void removeParent();

        // Getters
        std::shared_ptr<Entity> getEntity() const;
        std::vector<std::shared_ptr<EntityNode>> getChildren() const; // Returns shared_ptrs for safety
        std::shared_ptr<EntityNode> getParent() const;             // Returns shared_ptr for safety



    private:
        std::shared_ptr<Entity> m_entity; // Owning pointer to the entity data
        std::weak_ptr<EntityNode> m_parent; // Non-owning raw pointer to parent
        std::vector<std::weak_ptr<EntityNode>> m_children; // Non-owning raw pointers to children
    };

} // namespace Rapture

