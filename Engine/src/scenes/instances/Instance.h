#ifndef RAPTURE__INSTANCE_H
#define RAPTURE__INSTANCE_H

#include "events/EventSignal.h"
#include "serialization/SerialDocument.h"
#include "utils/TypeInfo.h"
#include "utils/UUID.h"

#include <string>
#include <string_view>

namespace Rapture {

class Scene;

/**
 * @brief Identifies an instance for as long as it exists, across renames, reparenting and a reload
 */
using InstanceId = UUID;

static constexpr InstanceId INVALID_INSTANCE_ID = 0;

/**
 * @brief Base of every authored object in a scene.
 *
 * Carries the identity an authored object is referred to by and the document pair it is written
 * through. What it is made of and where it sits are the two branches below it, SceneObject and
 * SceneComponent.
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
     * @brief Writes this instance's class and the fields its class declares
     * @param node Cursor to write this instance's object into
     */
    virtual void serialize(WriteNode node) const;

    /**
     * @brief Reads this instance's own fields
     * @param node Cursor to this instance's object
     */
    virtual void deserialize(ReadNode node);

    /**
     * @brief Reads the class a document names, before there is an instance to read into
     * @param node Cursor to the instance's object
     * @return The class name, empty if the document names none
     */
    static std::string_view readClassName(ReadNode node);

    /**
     * @brief Fires as this instance is destroyed, before anything it owns is torn down
     */
    EventSignal<void(Instance *)> onDestroy;

    /**
     * @brief This instance's stable identity, what another instance stores to refer to it
     */
    InstanceId id() const { return m_id; }

    /**
     * @brief Gives this instance a fresh identity, so a copy does not claim to be its source
     */
    void remintId();

    std::string_view name() const { return m_name; }

    /**
     * @brief Renames this instance
     * @param name The new name
     */
    virtual void setName(std::string_view name);

    Scene *scene() const { return m_scene; }

  private:
    Scene *m_scene;
    InstanceId m_id;
    std::string m_name;
};

} // namespace Rapture

#endif // RAPTURE__INSTANCE_H
