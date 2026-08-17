#ifndef RAPTURE__INSTANCE_H
#define RAPTURE__INSTANCE_H

#include "core/events/EventSignal.h"
#include "core/utils/Typed.h"
#include "scene/TickPhase.h"
#include "core/serialization/SerialDocument.h"
#include "core/utils/TypeInfo.h"
#include "core/utils/UUID.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Rapture {

class Scene;
class SceneLoadContext;

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
class Instance : public Typed {
  public:
    Instance(Scene &scene, std::string_view name);
    ~Instance() override;

    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

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
     * @brief Whether this instance is updated each frame
     */
    bool isTickEnabled() const;

    /**
     * @brief Puts this instance into or takes it out of its scene's tick list
     * @param enabled Whether the instance should be updated
     */
    void setTickEnabled(bool enabled);

    TickPhase tickPhase() const { return m_tickPhase; }

    /**
     * @brief Moves this instance to another phase, leaving it updating if it already was
     * @param phase The phase to update in
     */
    void setTickPhase(TickPhase phase);

    /**
     * @brief Advances this instance, only called while updating is enabled
     * @param dt Seconds since the last update
     */
    virtual void onUpdate(float dt);

    /**
     * @brief Runs the link hook
     * @param context The read this instance came out of
     */
    void link(const SceneLoadContext &context);

    /**
     * @brief Runs the ready hook, once, however often this instance changes owner afterwards
     */
    void ready();

    bool isReady() const { return m_isReady; }

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

  protected:
    /**
     * @brief Turns the ids this instance was read with into pointers to what they name
     * @param context The read this instance came out of
     */
    virtual void onLink(const SceneLoadContext &context);

    /**
     * @brief Called once this instance is in a scene with every reference it was read with resolved
     */
    virtual void onReady();

  private:
    Scene *m_scene;
    InstanceId m_id;
    std::string m_name;
    TickPhase m_tickPhase = TICK_PRE_PHYSICS;
    uint32_t m_tickSlot;
    bool m_isReady = false;
};

} // namespace Rapture

#endif // RAPTURE__INSTANCE_H
