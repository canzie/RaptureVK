#ifndef RAPTURE__SCENE_COMPONENT_H
#define RAPTURE__SCENE_COMPONENT_H

#include "ecs/entity_accessor.h"
#include "scenes/instances/Instance.h"

#include <cstdint>

namespace Rapture {

class SceneObject;

/**
 * @brief A capability that exists only as part of one scene object.
 *
 * Holds no place in the scene tree, has no children and owns no entity of its own, writing its
 * storage onto the entity of the object it is attached to. Being owned by a unique pointer and
 * never copied, it is the tier allowed to own a resource and release it again.
 */
class SceneComponent : public Instance {
  public:
    SceneComponent(Scene &scene, std::string_view name);
    ~SceneComponent() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief The scene object this component is part of
     * @return The owner, or nullptr while the component is detached
     */
    SceneObject *owner() const { return m_owner; }

    /**
     * @brief The owner's entity, which this component attaches its storage to
     * @return The accessor, invalid while the component is detached
     */
    ecs::EntityAccessor ownerEntity() const;

    /**
     * @brief Whether this component is updated each frame
     */
    bool isUpdateEnabled() const { return m_updateSlot != INVALID_UPDATE_SLOT; }

    /**
     * @brief Puts this component into or takes it out of its scene's update list
     * @param enabled Whether the component should be updated
     */
    void setUpdateEnabled(bool enabled);

    /**
     * @brief Advances this component, only called while updating is enabled
     * @param dt Seconds since the last update
     */
    virtual void update(float dt);

    /**
     * @brief Binds this component to the object it was attached to and runs onAttach
     * @param owner The object taking this component on
     */
    void attachTo(SceneObject *owner);

    /**
     * @brief Runs onDetach and unbinds this component from its owner
     */
    void detach();

  protected:
    /**
     * @brief Called once this component has been attached to an owner, for claiming what it needs
     */
    virtual void onAttach();

    /**
     * @brief Called before this component is detached from its owner, for releasing what it claimed
     */
    virtual void onDetach();

  private:
    static constexpr uint32_t INVALID_UPDATE_SLOT = UINT32_MAX;

    SceneObject *m_owner = nullptr;
    uint32_t m_updateSlot = INVALID_UPDATE_SLOT;
};

} // namespace Rapture

#endif // RAPTURE__SCENE_COMPONENT_H
