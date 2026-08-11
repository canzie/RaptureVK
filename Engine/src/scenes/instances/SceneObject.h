#ifndef RAPTURE__SCENE_OBJECT_H
#define RAPTURE__SCENE_OBJECT_H

#include "ecs/entity_accessor.h"
#include "scenes/instances/Instance.h"
#include "scenes/instances/SceneComponent.h"

#include <memory>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief An authored object that exists in its own right inside a scene.
 *
 * Holds a place in the scene tree, owns the entity its components write their storage onto, and
 * can contain other scene objects. Subclasses attach the storage their class is made of in their
 * constructor, and never cache a pointer into it.
 */
class SceneObject : public Instance {
  public:
    SceneObject(Scene &scene, std::string_view name);
    ~SceneObject() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    void setName(std::string_view name) override;

    /**
     * @brief Creates a T and parents it to this object
     * @param name Name for the new child
     * @return The child, owned by this object
     */
    template <typename T>
    T *add(std::string_view name)
    {
        auto child = std::make_unique<T>(*scene(), name);
        T *raw = child.get();
        addChild(std::move(child));
        return raw;
    }

    /**
     * @brief Takes ownership of a child and links it to this object
     * @param child The child to adopt
     */
    void addChild(std::unique_ptr<SceneObject> child);

    /**
     * @brief Unlinks a child and hands its ownership back
     * @param child The child to release
     * @return The child, or nullptr if it is not a child of this object
     */
    std::unique_ptr<SceneObject> removeChild(SceneObject *child);

    /**
     * @brief Finds a direct child by name
     * @param name The child's name
     * @return The child, or nullptr if no child has that name
     */
    SceneObject *findChild(std::string_view name) const;

    /**
     * @brief Finds a descendant by a slash separated path, for example "Door/Handle"
     * @param path Names from this object down, separated by slashes
     * @return The descendant, or nullptr if any step is missing
     */
    SceneObject *findDescendant(std::string_view path) const;

    template <typename T>
    T *findFirstChildOfType() const
    {
        for (const auto &child : m_children) {
            if (T *typed = child->as<T>()) {
                return typed;
            }
        }
        return nullptr;
    }

    /**
     * @brief Finds the first T anywhere below this object, depth first
     * @return The descendant, or nullptr if the subtree holds no T
     */
    template <typename T>
    T *findFirstDescendantOfType() const
    {
        for (const auto &child : m_children) {
            if (T *typed = child->as<T>()) {
                return typed;
            }
            if (T *typed = child->findFirstDescendantOfType<T>()) {
                return typed;
            }
        }
        return nullptr;
    }

    template <typename T>
    T *findFirstAncestorOfType() const
    {
        for (SceneObject *object = m_parent; object != nullptr; object = object->m_parent) {
            if (T *typed = object->as<T>()) {
                return typed;
            }
        }
        return nullptr;
    }

    /**
     * @brief Attaches a T to this object, or hands back the one it already has
     * @return The component, owned by this object
     */
    template <typename T>
    T *addComponent()
    {
        if (T *existing = component<T>()) {
            return existing;
        }

        auto created = std::make_unique<T>(*scene(), T::staticType().name);
        T *raw = created.get();
        attachComponent(std::move(created));
        return raw;
    }

    /**
     * @brief This object's T
     * @return The component, or nullptr if this object has none
     */
    template <typename T>
    T *component() const
    {
        for (const auto &attached : m_components) {
            if (T *typed = attached->as<T>()) {
                return typed;
            }
        }
        return nullptr;
    }

    /**
     * @brief Detaches and destroys this object's T, if it has one
     */
    template <typename T>
    void removeComponent()
    {
        if (T *attached = component<T>()) {
            removeComponent(attached);
        }
    }

    /**
     * @brief Takes ownership of a component and attaches it to this object
     * @param component The component to attach
     */
    void attachComponent(std::unique_ptr<SceneComponent> component);

    /**
     * @brief Detaches a component and hands its ownership back
     * @param component The component to release
     * @return The component, or nullptr if it is not attached to this object
     */
    std::unique_ptr<SceneComponent> detachComponent(SceneComponent *component);

    /**
     * @brief Detaches and destroys a component
     * @param component The component to remove
     */
    void removeComponent(SceneComponent *component);

    /**
     * @brief Writes this object, its components and its subtree
     * @param node Cursor to write this object's object into
     */
    void serialize(WriteNode node) const override;

    /**
     * @brief What a document says a scene object is, readable before there is one to read into
     */
    struct DocumentHeader {
        std::string_view className;
        std::string_view name;
        InstanceId id = INVALID_INSTANCE_ID;
        ReadNode components;
        ReadNode children;
    };

    /**
     * @brief Reads a scene object's header out of a document
     * @param node Cursor to the object's object
     * @return The header
     */
    static DocumentHeader readHeader(ReadNode node);

    /**
     * @brief Creates the scene object a document names, parents it and reads its subtree
     * @param parent The object the new object is added to
     * @param node Cursor to the object's object
     * @param order Receives every object created, in the order serialize wrote them
     * @return True if the whole subtree was read
     */
    static bool loadSubtree(SceneObject &parent, ReadNode node, std::vector<SceneObject *> &order);

    /**
     * @brief Reads a subtree into a scene as its own objects rather than as the ones it was written from
     * @param parent The object the root is parented to, which gains ownership of it
     * @param node Cursor to the root object's object
     * @return The root of what was read, or nullptr if it could not be read
     */
    static SceneObject *spawnSubtree(SceneObject &parent, ReadNode node);

    SceneObject *parent() const { return m_parent; }
    std::span<const std::unique_ptr<SceneObject>> children() const { return m_children; }
    std::span<const std::unique_ptr<SceneComponent>> components() const { return m_components; }

    ecs::Entity entity() const { return m_entity.getEntity(); }

    /**
     * @brief This object's entity bound to the registry that resolves it
     * @return The accessor, invalid if this object has no entity
     */
    const ecs::EntityAccessor &accessor() const { return m_entity; }

  protected:
    /**
     * @brief Called on an object once it has been linked to a new parent, or unlinked from one.
     *
     * The default passes it down the subtree, so a subclass that has no stake in where it sits still
     * lets the ones below it react.
     */
    virtual void onParentChanged();

    /**
     * @brief Reads this object's components and children out of a document
     * @param header The header the fields were read from
     * @param order Receives every object created below this one
     * @return True if everything below this object was read
     */
    bool loadContents(const DocumentHeader &header, std::vector<SceneObject *> &order);

  protected:
    ecs::EntityAccessor m_entity;

  private:
    SceneObject *m_parent = nullptr;
    std::vector<std::unique_ptr<SceneObject>> m_children;
    std::vector<std::unique_ptr<SceneComponent>> m_components;
};

/**
 * @brief Back reference from an entity to the scene object that owns it
 */
struct InstanceComponent {
    SceneObject *instance = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_OBJECT_H
