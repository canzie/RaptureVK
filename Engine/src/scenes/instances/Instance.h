#ifndef RAPTURE__INSTANCE_H
#define RAPTURE__INSTANCE_H

#include "events/EventSignal.h"
#include "scenes/entities/Entity.h"
#include "scenes/instances/TypeInfo.h"
#include "serialization/SerialDocument.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rapture {

class Scene;

/**
 * @brief Base of every authored object in a scene.
 *
 * An instance owns an entity and holds its place in the scene tree. Subclasses attach the
 * components their class is made of in their constructor, and never cache a pointer into
 * component storage.
 */
class Instance {
  public:
    Instance(Scene &scene, std::string_view name);
    virtual ~Instance();

    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;

    static const TypeInfo &staticType();

    /**
     * @brief The type of the object this actually is, rather than of the class it is held as
     */
    virtual const TypeInfo &type() const;

    /**
     * @brief Whether this instance is a T, or derives from one
     */
    template <typename T>
    bool isA() const
    {
        const TypeInfo &self = type();
        const TypeInfo &other = T::staticType();
        return self.depth >= other.depth && self.chain[other.depth] == &other;
    }

    /**
     * @brief Casts to T if this instance is one
     * @return The cast pointer, or nullptr if the cast is not legal
     */
    template <typename T>
    T *as()
    {
        return isA<T>() ? static_cast<T *>(this) : nullptr;
    }

    template <typename T>
    const T *as() const
    {
        return isA<T>() ? static_cast<const T *>(this) : nullptr;
    }

    /**
     * @brief Creates a T and parents it to this instance
     * @param name Name for the new child
     * @return The child, owned by this instance
     */
    template <typename T>
    T *add(std::string_view name)
    {
        auto child = std::make_unique<T>(*m_scene, name);
        T *raw = child.get();
        addChild(std::move(child));
        return raw;
    }

    /**
     * @brief Takes ownership of a child and links it to this instance
     * @param child The child to adopt
     */
    void addChild(std::unique_ptr<Instance> child);

    /**
     * @brief Unlinks a child and hands its ownership back
     * @param child The child to release
     * @return The child, or nullptr if it is not a child of this instance
     */
    std::unique_ptr<Instance> removeChild(Instance *child);

    /**
     * @brief Finds a direct child by name
     * @param name The child's name
     * @return The child, or nullptr if no child has that name
     */
    Instance *findChild(std::string_view name) const;

    /**
     * @brief Finds a descendant by a slash separated path, for example "Door/Handle"
     * @param path Names from this instance down, separated by slashes
     * @return The descendant, or nullptr if any step is missing
     */
    Instance *findDescendant(std::string_view path) const;

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

    template <typename T>
    T *findFirstAncestorOfType() const
    {
        for (Instance *node = m_parent; node != nullptr; node = node->m_parent) {
            if (T *typed = node->as<T>()) {
                return typed;
            }
        }
        return nullptr;
    }

    /**
     * @brief Writes this instance and its subtree
     * @param node Cursor to write this instance's object into
     */
    virtual void serialize(WriteNode node) const;

    /**
     * @brief Reads this instance's own fields
     * @param node Cursor to this instance's object
     */
    virtual void deserialize(ReadNode node);

    /**
     * @brief Creates the instance a document names, parents it and reads its subtree
     * @param parent The instance the new instance is added to
     * @param node Cursor to the instance's object
     * @param order Receives every instance created, in the order serialize wrote them
     * @return True if the whole subtree was read
     */
    static bool loadSubtree(Instance &parent, ReadNode node, std::vector<Instance *> &order);

    /**
     * @brief Fires as this instance is destroyed, before its subtree is torn down
     */
    EventSignal<void(Instance *)> onDestroy;

    Instance *parent() const { return m_parent; }
    std::span<const std::unique_ptr<Instance>> children() const { return m_children; }
    std::string_view name() const { return m_name; }
    void setName(std::string_view name);
    Entity entity() const { return m_entity; }
    Scene *scene() const { return m_scene; }

  protected:
    Entity m_entity;

  private:
    Scene *m_scene;
    Instance *m_parent = nullptr;
    std::vector<std::unique_ptr<Instance>> m_children;
    std::string m_name;
};

/**
 * @brief Back reference from an entity to the instance that owns it
 */
struct InstanceComponent {
    Instance *instance = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__INSTANCE_H
