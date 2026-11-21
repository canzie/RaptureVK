#ifndef RAPTURE__SCENE_OBJECT_H
#define RAPTURE__SCENE_OBJECT_H

#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/core/named_values/Attributes.h>
#include <rapture/scene/instances/Instance.h>
#include <rapture/scene/instances/SceneComponent.h>

#include <memory>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief Whether a child belongs to the object that made it rather than to whoever authored the scene
 */
enum class InternalMode {
    DISABLED,
    ENABLED
};

/**
 * @brief What holds of a scene object beyond what it is made of
 */
enum SceneObjectFlag {
    SCENE_OBJECT_FLAG_NONE = 0,
    SCENE_OBJECT_FLAG_INTERNAL = 1 << 0,
    SCENE_OBJECT_FLAG_SERIALIZED = 1 << 1
};

/**
 * @brief Where a scene object is shown
 */
struct SceneObjectVisibility {
    bool inOutliner = true;
};

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
     * @param internalMode Whether the child is added as internal
     * @return The child, owned by this object
     */
    template <typename T>
    T *add(std::string_view name, InternalMode internalMode = InternalMode::DISABLED)
    {
        auto child = std::make_unique<T>(*scene(), name);
        T *raw = child.get();
        if (addChild(std::move(child), internalMode) == nullptr) {
            return nullptr;
        }
        raw->ready();
        return raw;
    }

    /**
     * @brief Takes ownership of a child and links it to this object
     * @param child The child to adopt
     * @param internalMode Whether the child is added as internal
     * @return The child, or nullptr if this object is being destroyed, in which case the child is destroyed too
     */
    SceneObject *addChild(std::unique_ptr<SceneObject> child, InternalMode internalMode = InternalMode::DISABLED);

    /**
     * @brief Destroys a child and its subtree, telling every listener in the subtree before any of it is torn down
     * @param child The child to destroy, internal or not
     */
    void removeChild(SceneObject *child);

    /**
     * @brief Moves this object and its subtree under another object
     * @param parent The object to move under
     * @return True if this object now sits under the parent, false if it is internal, has no parent to leave, is being
     *         destroyed, or the parent is this object or sits below it
     */
    bool setParent(SceneObject *parent);

    /**
     * @brief Where this object sits among its parent's children of the same kind, internal or authored
     * @return The index, 0 for an object without a parent
     */
    size_t siblingIndex() const;

    /**
     * @brief Moves this object to another place among its parent's children of the same kind
     * @param index The place to move to, past the last moving it to the end
     */
    void setSiblingIndex(size_t index);

    /**
     * @brief Whether this object is a subtree's root or sits below it
     * @param root The root of the subtree
     * @return True if the object is in the subtree
     */
    bool isInSubtreeOf(const SceneObject &root) const;

    bool isInternal() const { return (m_flags & SCENE_OBJECT_FLAG_INTERNAL) != 0; }

    bool isSerialized() const { return (m_flags & SCENE_OBJECT_FLAG_SERIALIZED) != 0; }

    /**
     * @brief Sets whether this object is written out with the scene it sits in
     * @param serialized True to write it
     */
    void setSerialized(bool serialized);

    /**
     * @brief Finds a direct child by name
     * @param name The child's name
     * @param includeInternal Whether internal children are searched
     * @return The child, or nullptr if no child has that name
     */
    SceneObject *findChild(std::string_view name, bool includeInternal = false) const;

    /**
     * @brief Finds a descendant by a slash separated path, for example "Door/Handle"
     * @param path Names from this object down, separated by slashes
     * @param includeInternal Whether internal children are searched
     * @return The descendant, or nullptr if any step is missing
     */
    SceneObject *findDescendant(std::string_view path, bool includeInternal = false) const;

    template <typename T>
    T *findFirstChildOfType(bool includeInternal = false) const
    {
        for (const auto &child : children(includeInternal)) {
            if (T *typed = child->as<T>()) {
                return typed;
            }
        }
        return nullptr;
    }

    /**
     * @brief Finds the first T anywhere below this object, depth first
     * @param includeInternal Whether internal children are searched
     * @return The descendant, or nullptr if the subtree holds no T
     */
    template <typename T>
    T *findFirstDescendantOfType(bool includeInternal = false) const
    {
        for (const auto &child : children(includeInternal)) {
            if (T *typed = child->as<T>()) {
                return typed;
            }
            if (T *typed = child->findFirstDescendantOfType<T>(includeInternal)) {
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
        if (addComponent(std::move(created)) == nullptr) {
            return nullptr;
        }
        raw->ready();
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
     * @param component The component to add
     * @return The component, or nullptr if this object is being destroyed, in which case the component is destroyed too
     */
    SceneComponent *addComponent(std::unique_ptr<SceneComponent> component);

    /**
     * @brief Destroys a component, telling its listeners while it is still attached
     * @param component The component to remove
     */
    void removeComponent(SceneComponent *component);

    /**
     * @brief Writes this object and its components, leaving its children out
     * @param node Cursor to write this object's object into
     */
    void serialize(WriteNode node) const override;

    /**
     * @brief Writes this object, its components and every serialized object below it
     * @param node Cursor to write this object's object into
     */
    void serializeSubtree(WriteNode node) const;

    /**
     * @brief Reads this object's own fields back, leaving its components and children to the load
     * @param node Cursor to this object's object
     */
    void deserialize(ReadNode node) override;

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
     * @param context The read this object and its subtree belong to
     * @param internalMode Whether the root that is read is added as internal
     * @return The object read, or nullptr if any of its subtree could not be read
     */
    static SceneObject *loadSubtree(SceneObject &parent, ReadNode node, SceneLoadContext &context,
                                    InternalMode internalMode = InternalMode::DISABLED);

    /**
     * @brief Reads a subtree into a scene as its own objects rather than as the ones it was written from
     * @param parent The object the root is parented to, which gains ownership of it
     * @param node Cursor to the root object's object
     * @param internalMode Whether the root that is read is added as internal
     * @return The root of what was read, or nullptr if it could not be read
     */
    static SceneObject *spawnSubtree(SceneObject &parent, ReadNode node, InternalMode internalMode = InternalMode::DISABLED);

    /**
     * @brief Destroys every component this object has and reads the ones a document names in their place
     * @param header The header of this object's document
     * @param context The read the components belong to
     * @return True if every component was read
     */
    bool replaceComponents(const DocumentHeader &header, SceneLoadContext &context);

    SceneObject *parent() const { return m_parent; }

    /**
     * @brief The objects below this one
     * @param includeInternal Whether internal children are included
     * @return The children
     */
    std::span<const std::unique_ptr<SceneObject>> children(bool includeInternal = false) const;
    std::span<const std::unique_ptr<SceneComponent>> components() const { return m_components; }

    ecs::Entity entity() const { return m_entity.getEntity(); }

    /**
     * @brief This object's entity bound to the registry that resolves it
     * @return The accessor, invalid if this object has no entity
     */
    const ecs::EntityAccessor &accessor() const { return m_entity; }

    SceneObjectVisibility &visibility() { return m_visibility; }
    const SceneObjectVisibility &visibility() const { return m_visibility; }

    Attributes &attributes() { return m_attributes; }
    const Attributes &attributes() const { return m_attributes; }

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
     * @param context The read this object belongs to
     * @return True if everything below this object was read
     */
    bool loadContents(const DocumentHeader &header, SceneLoadContext &context);

  protected:
    ecs::EntityAccessor m_entity;

  private:
    /**
     * @brief Takes ownership of a child, placing it among the internal or authored children by its flag
     * @param child The child to link
     */
    void linkChild(std::unique_ptr<SceneObject> child);

    /**
     * @brief Takes a child out of this object's children and hands its ownership back
     * @param child The child to unlink
     * @return The child
     */
    std::unique_ptr<SceneObject> unlinkChild(SceneObject *child);

    /**
     * @brief Reads the components a document names onto this object
     * @param header The header of this object's document
     * @param context The read the components belong to
     * @return True if every component was read
     */
    bool loadComponents(const DocumentHeader &header, SceneLoadContext &context);

    /**
     * @brief Marks a subtree as being destroyed, then tells every listener in it
     * @param object The root of the subtree
     */
    static void beginSubtreeDestruction(SceneObject &object);

  private:
    SceneObject *m_parent = nullptr;
    std::vector<std::unique_ptr<SceneObject>> m_children;
    std::vector<std::unique_ptr<SceneComponent>> m_components;
    size_t m_internalCount = 0;
    uint32_t m_flags = SCENE_OBJECT_FLAG_SERIALIZED;
    SceneObjectVisibility m_visibility;
    Attributes m_attributes;
};

/**
 * @brief Back reference from an entity to the scene object that owns it
 */
struct InstanceComponent {
    SceneObject *instance = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_OBJECT_H
