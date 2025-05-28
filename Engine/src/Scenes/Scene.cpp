#include "Scene.h"
#include "Entities/Entity.h"

#include "Components/Components.h"

namespace Rapture {

    Scene::Scene(const std::string& sceneName) {
        m_config.sceneName = sceneName;
    }

    Scene::~Scene() {
        // EnTT registry will automatically clean up all entities and components
    }

    Entity Scene::createEntity(const std::string& name) {
        // Create entity in the registry
        entt::entity handle = m_Registry.create();
        
        // Create Entity wrapper
        Entity entity(handle, this);
        
        // Add basic name component if you have one
        entity.addComponent<TagComponent>(name);
        
        return entity;
    }

    void Scene::destroyEntity(Entity entity) {
        if (entity.isValid() && entity.getScene() == this) {
            m_Registry.destroy(entity.getHandle());
        }
    }

    void Scene::onUpdate() {
        // This is where you would implement scene update logic
        // For example, system updates, physics, rendering preparation, etc.
        
        // Example of how you might iterate through entities with components:
        /*
        auto view = m_Registry.view<Transform, Velocity>();
        for (auto entity : view) {
            auto& transform = view.get<Transform>(entity);
            auto& velocity = view.get<Velocity>(entity);
            
            // Update position based on velocity
            transform.position.x += velocity.x * deltaTime;
            transform.position.y += velocity.y * deltaTime;
            transform.position.z += velocity.z * deltaTime;
        }
        */
    }
} 