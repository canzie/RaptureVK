#ifndef RAPTURE__INSTANCE_REF_H
#define RAPTURE__INSTANCE_REF_H

#include "core/utils/Log.h"
#include "scene/SceneLoadContext.h"
#include "scene/instances/Instance.h"

namespace Rapture {

/**
 * @brief A field holding another instance in the same scene.
 *
 * Written as the id of what it points at and read back as that id, which one link turns into a
 * pointer. Empties itself when what it points at is destroyed, so it never names a dead instance.
 */
template <typename T>
class InstanceRef {
  public:
    InstanceRef() = default;

    InstanceRef(const InstanceRef &) = delete;
    InstanceRef &operator=(const InstanceRef &) = delete;

    T *get() const { return m_target; }
    T *operator->() const { return m_target; }
    explicit operator bool() const { return m_target != nullptr; }

    /**
     * @brief Points this reference at an instance
     * @param instance The instance to point at, or nullptr to empty the reference
     */
    void set(T *instance)
    {
        m_target = instance;
        m_documentId = INVALID_INSTANCE_ID;
        m_destroyed.disconnect();

        if (instance == nullptr) {
            return;
        }

        m_destroyed = instance->onDestroy.connect([this](Instance *) { m_target = nullptr; });
    }

    /**
     * @brief Writes what this reference points at
     * @param node Cursor to the object holding the field
     * @param key Key to write the field under
     */
    void serialize(WriteNode node, std::string_view key) const
    {
        node.set(key, m_target != nullptr ? m_target->id() : m_documentId);
    }

    /**
     * @brief Reads the id this reference points at, which stays an id until the link
     * @param node Cursor to the object holding the field
     * @param key Key the field was written under
     */
    void deserialize(ReadNode node, std::string_view key) { m_documentId = node.child(key).asU64(INVALID_INSTANCE_ID); }

    /**
     * @brief Turns the id read into a pointer, leaving it an id if the read produced nothing for it
     * @param context The read this reference came out of
     */
    void link(const SceneLoadContext &context)
    {
        if (m_documentId == INVALID_INSTANCE_ID) {
            return;
        }

        Instance *found = context.find(m_documentId);
        if (found == nullptr) {
            return;
        }

        T *typed = found->as<T>();
        if (typed == nullptr) {
            RP_CORE_ERROR("'{}' is a {}, which is not what the reference to it holds", found->name(), found->type().name);
            return;
        }

        set(typed);
    }

  private:
    T *m_target = nullptr;
    InstanceId m_documentId = INVALID_INSTANCE_ID;
    EventConnection m_destroyed;
};

} // namespace Rapture

#endif // RAPTURE__INSTANCE_REF_H
