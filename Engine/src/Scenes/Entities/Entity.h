#pragma once

#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <cstdint>
#include <entt/entt.hpp>
#include "Scenes/Scene.h"


/*
    This is just a entt wrapper using magic (...)
    Just a big template shitter
*/

namespace Rapture {

    class EntityException : public std::runtime_error {
    public:
        explicit EntityException(const std::string& message) : std::runtime_error(message) {}
    };

    using EntityID = uint32_t;

class Entity {


    public:
        // Default constructor creates null entity
        Entity() = default;
        
        // Constructor for valid entity
        Entity(entt::entity handle, Scene* scene)
            : m_EntityHandle(handle), m_Scene(scene) {}

        Entity(uint32_t handle, Scene* scene)
          : m_EntityHandle(static_cast<entt::entity>(handle)), m_Scene(scene) {}


        Entity(const Entity& other) = default;
        Entity& operator=(const Entity& other) = default;

        // Static null entity for comparisons
        static Entity null() {
            return Entity();
        }

        // Creates a new component and attaches it to the entity, returns reference to it
        template<typename T, typename... Args>
        T& addComponent(Args&&... args) {
            validateEntity("Cannot add component to invalid entity");
            if (hasComponent<T>()) {
                throw EntityException("Component already exists on this entity");
            }
            return m_Scene->getRegistry().emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        // Creates or replaces a component on the entity, returns reference to it
        template<typename T, typename... Args>
        T& setComponent(Args&&... args) {
            validateEntity("Cannot set component on invalid entity");
            return m_Scene->getRegistry().emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        // Gets a component, throws if not found
        template<typename T>
        T& getComponent() {
            validateEntity("Cannot get component from invalid entity");
            if (!hasComponent<T>()) {
                throw EntityException("Entity does not have the requested component");
            }
            return m_Scene->getRegistry().get<T>(m_EntityHandle);
        }

        // Const version of getComponent
        template<typename T>
        const T& getComponent() const {
            validateEntity("Cannot get component from invalid entity");
            if (!hasComponent<T>()) {
                throw EntityException("Entity does not have the requested component");
            }
            return m_Scene->getRegistry().get<T>(m_EntityHandle);
        }

        // Gets a component if it exists, or nullptr if not
        template<typename T>
        T* tryGetComponent() {
            if (!isValid() || !hasComponent<T>()) {
                return nullptr;
            }
            return &m_Scene->getRegistry().get<T>(m_EntityHandle);
        }

        // Const version of tryGetComponent
        template<typename T>
        const T* tryGetComponent() const {
            if (!isValid() || !hasComponent<T>()) {
                return nullptr;
            }
            return &m_Scene->getRegistry().get<T>(m_EntityHandle);
        }

        // Tries to get multiple components at once
        template<typename... T>
        std::tuple<T*...> tryGetComponents() {
            if (!isValid()) {
                return std::make_tuple(static_cast<T*>(nullptr)...);
            }
            return tryGetComponentsTuple<T...>(std::index_sequence_for<T...>{});
        }

        // Check if entity has a component
        template<typename T>
        bool hasComponent() const {
            if (!isValid()) return false;
            return m_Scene->getRegistry().all_of<T>(m_EntityHandle);
        }

        // Check if entity has all of the specified components
        template<typename... T>
        bool hasAllComponents() const {
            if (!isValid()) return false;
            return m_Scene->getRegistry().all_of<T...>(m_EntityHandle);
        }

        // Check if entity has any of the specified components
        template<typename... T>
        bool hasAnyComponent() const {
            if (!isValid()) return false;
            return m_Scene->getRegistry().any_of<T...>(m_EntityHandle);
        }

        // Removes a component, throws if component doesn't exist
        template<typename T>
        void removeComponent() {
            validateEntity("Cannot remove component from invalid entity");
            if (!hasComponent<T>()) {
                throw EntityException("Cannot remove component that doesn't exist");
            }
            m_Scene->getRegistry().remove<T>(m_EntityHandle);
        }

        // Safely removes a component if it exists
        template<typename T>
        bool tryRemoveComponent() {
            if (!isValid() || !hasComponent<T>()) {
                return false;
            }
            m_Scene->getRegistry().remove<T>(m_EntityHandle);
            return true;
        }

        // Patch (update) a component with a function
        template<typename T, typename Func>
        void patchComponent(Func&& func) {
            validateEntity("Cannot patch component on invalid entity");
            if (!hasComponent<T>()) {
                throw EntityException("Cannot patch component that doesn't exist");
            }
            m_Scene->getRegistry().patch<T>(m_EntityHandle, std::forward<Func>(func));
        }

        // Check if entity is valid (has a valid handle and scene)
        bool isValid() const {
            return m_Scene != nullptr && m_EntityHandle != entt::null &&
                   m_Scene->getRegistry().valid(m_EntityHandle);
        }

        // Check if entity is null/invalid
        bool isNull() const {
            return !isValid();
        }

        // Get the entity's ID (useful for debugging)
        uint32_t getID() const {
            return static_cast<uint32_t>(m_EntityHandle);
        }

        // Destroy this entity
        void destroy() {
            if (isValid()) {
                m_Scene->destroyEntity(*this);
                m_EntityHandle = entt::null;
                m_Scene = nullptr;
            }
        }

        // Static utility function
        static EntityID enttHandleToEntityID(const entt::entity& handle) {
            return static_cast<EntityID>(handle);
        }

        // Return the underlying scene
        Scene* getScene() const { return m_Scene; }

        // Get the raw entt entity handle
        entt::entity getHandle() const { return m_EntityHandle; }

        // Conversion operators
        operator entt::entity() const { return m_EntityHandle; }
        operator uint32_t() const { return static_cast<uint32_t>(m_EntityHandle); }
        operator bool() const { return isValid(); }

        // Comparison operators
        bool operator==(const Entity& other) const {
            return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
        }
        
        bool operator!=(const Entity& other) const {
            return !(*this == other);
        }

        // Comparison with null
        bool operator==(std::nullptr_t) const {
            return isNull();
        }

        bool operator!=(std::nullptr_t) const {
            return isValid();
        }

        // Ordering operators for use in containers
        bool operator<(const Entity& other) const {
            if (m_Scene != other.m_Scene) {
                return m_Scene < other.m_Scene;
            }
            return m_EntityHandle < other.m_EntityHandle;
        }

    private:

        void validateEntity(const std::string& errorMessage) const {
            if (!isValid()) {
                throw EntityException(errorMessage);
            }
        }

        // Helper for tryGetComponents
        template<typename... T, size_t... I>
        std::tuple<T*...> tryGetComponentsTuple(std::index_sequence<I...>) {
            return std::make_tuple(
                (hasComponent<T>() ? &m_Scene->getRegistry().get<T>(m_EntityHandle) : nullptr)...
            );
        }
    private:
        entt::entity m_EntityHandle{ entt::null };
        Scene* m_Scene = nullptr;
    };

    // Allow comparison with nullptr on left side
    inline bool operator==(std::nullptr_t, const Entity& entity) {
        return entity.isNull();
    }

    inline bool operator!=(std::nullptr_t, const Entity& entity) {
        return entity.isValid();
    }
}
