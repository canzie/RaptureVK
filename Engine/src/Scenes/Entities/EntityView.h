#pragma once

#include <tuple>
#include <type_traits>
#include <optional>
#include <cstdint>
#include <iterator>
#include "entt/entt.hpp"
#include "Entity.h"
#include "../Scene.h"

namespace Rapture {

    // Forward declarations
    template<typename... Component>
    class EntityView;

    template<typename... Component>
    class EntityViewIterator;

    // Entity wrapper that holds component references from the view
    template<typename... Component>
    class ViewEntity {
    public:
        ViewEntity(entt::entity handle, Scene* scene, Component&... components)
            : m_EntityHandle(handle), m_Scene(scene), m_Components(components...) {}

        // Get entity without wrapper (most efficient)
        Entity getEntity() const {
            return Entity(m_EntityHandle, m_Scene);
        }

        // Get component by type (no lookup, uses cached reference)
        template<typename T>
        T& getComponent() {
            static_assert((std::is_same_v<T, Component> || ...), "Component type not in view");
            return std::get<T&>(m_Components);
        }

        template<typename T>
        const T& getComponent() const {
            static_assert((std::is_same_v<T, Component> || ...), "Component type not in view");
            return std::get<T&>(m_Components);
        }

        // Get multiple components at once
        template<typename... T>
        std::tuple<T&...> getComponents() {
            static_assert(((std::is_same_v<T, Component> || ...) && ...), "All component types must be in view");
            return std::tie(std::get<T&>(m_Components)...);
        }

        // Get all components in the view
        std::tuple<Component&...> getAllComponents() {
            return m_Components;
        }

        // Check if entity is valid
        bool isValid() const {
            return m_Scene != nullptr && m_EntityHandle != entt::null &&
                   m_Scene->getRegistry().valid(m_EntityHandle);
        }

        // Get entity ID
        uint32_t getID() const {
            return static_cast<uint32_t>(m_EntityHandle);
        }

        // Get raw handle
        entt::entity getHandle() const { return m_EntityHandle; }

        // Conversion operators
        operator entt::entity() const { return m_EntityHandle; }
        operator uint32_t() const { return static_cast<uint32_t>(m_EntityHandle); }
        operator bool() const { return isValid(); }

        // Comparison operators
        bool operator==(const ViewEntity& other) const {
            return m_EntityHandle == other.m_EntityHandle;
        }

        bool operator!=(const ViewEntity& other) const {
            return m_EntityHandle != other.m_EntityHandle;
        }

        bool operator==(const Entity& other) const {
            return m_EntityHandle == other.getHandle();
        }

        bool operator!=(const Entity& other) const {
            return m_EntityHandle != other.getHandle();
        }

    private:
        entt::entity m_EntityHandle;
        Scene* m_Scene;
        std::tuple<Component&...> m_Components;
    };

    // Iterator for EntityView
    template<typename... Component>
    class EntityViewIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = ViewEntity<Component...>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        EntityViewIterator(typename entt::view<Component...>::iterator it, Scene* scene)
            : m_Iterator(it), m_Scene(scene) {}

        ViewEntity<Component...> operator*() const {
            auto entity = *m_Iterator;
            auto& registry = m_Scene->getRegistry();
            return ViewEntity<Component...>(entity, m_Scene, registry.get<Component>(entity)...);
        }

        EntityViewIterator& operator++() {
            ++m_Iterator;
            return *this;
        }

        EntityViewIterator operator++(int) {
            EntityViewIterator temp = *this;
            ++(*this);
            return temp;
        }

        bool operator==(const EntityViewIterator& other) const {
            return m_Iterator == other.m_Iterator;
        }

        bool operator!=(const EntityViewIterator& other) const {
            return m_Iterator != other.m_Iterator;
        }

    private:
        typename entt::view<Component...>::iterator m_Iterator;
        Scene* m_Scene;
    };

    // Main EntityView class
    template<typename... Component>
    class EntityView {
    public:
        using iterator = EntityViewIterator<Component...>;
        using const_iterator = EntityViewIterator<Component...>;

        EntityView(Scene* scene) : m_Scene(scene), m_View(scene->getRegistry().view<Component...>()) {}

        // Iterator interface
        iterator begin() {
            return iterator(m_View.begin(), m_Scene);
        }

        iterator end() {
            return iterator(m_View.end(), m_Scene);
        }

        const_iterator begin() const {
            return const_iterator(m_View.begin(), m_Scene);
        }

        const_iterator end() const {
            return const_iterator(m_View.end(), m_Scene);
        }

        const_iterator cbegin() const {
            return begin();
        }

        const_iterator cend() const {
            return end();
        }

        // Utility methods
        size_t size() const {
            return m_View.size();
        }

        bool empty() const {
            return m_View.empty();
        }

        // Check if entity is in view
        bool contains(Entity entity) const {
            return m_View.contains(entity.getHandle());
        }

        // Get ViewEntity for specific entity (if it's in the view)
        std::optional<ViewEntity<Component...>> get(Entity entity) const {
            if (!contains(entity)) {
                return std::nullopt;
            }
            auto& registry = m_Scene->getRegistry();
            auto handle = entity.getHandle();
            return ViewEntity<Component...>(handle, m_Scene, registry.get<Component>(handle)...);
        }

        // Execute function for each entity in view
        template<typename Func>
        void forEach(Func&& func) {
            for (auto viewEntity : *this) {
                func(viewEntity);
            }
        }

        // Execute function for each entity with access to raw components
        template<typename Func>
        void forEachRaw(Func&& func) {
            m_View.each([&func](auto entity, Component&... components) {
                func(entity, components...);
            });
        }

        // Get the underlying entt view (for advanced usage)
        const entt::view<Component...>& getRawView() const {
            return m_View;
        }

    private:
        Scene* m_Scene;
        entt::view<Component...> m_View;
    };

    // Convenience functions for creating views
    namespace Views {
        template<typename... Component>
        EntityView<Component...> createView(Scene* scene) {
            return EntityView<Component...>(scene);
        }

        template<typename... Component>
        EntityView<Component...> createView(Scene& scene) {
            return EntityView<Component...>(&scene);
        }
    }

    // RAII-style view holder for automatic cleanup
    template<typename... Component>
    class ScopedEntityView {
    public:
        explicit ScopedEntityView(Scene* scene) : m_View(scene) {}
        explicit ScopedEntityView(Scene& scene) : m_View(&scene) {}

        EntityView<Component...>* operator->() { return &m_View; }
        const EntityView<Component...>* operator->() const { return &m_View; }

        EntityView<Component...>& operator*() { return m_View; }
        const EntityView<Component...>& operator*() const { return m_View; }

        // Direct access to common operations
        auto begin() { return m_View.begin(); }
        auto end() { return m_View.end(); }
        auto begin() const { return m_View.begin(); }
        auto end() const { return m_View.end(); }

        size_t size() const { return m_View.size(); }
        bool empty() const { return m_View.empty(); }

        template<typename Func>
        void forEach(Func&& func) { m_View.forEach(std::forward<Func>(func)); }

    private:
        EntityView<Component...> m_View;
    };
} 