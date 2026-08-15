#ifndef RAPTURE__PHYSICS_BODY3D_H
#define RAPTURE__PHYSICS_BODY3D_H

#include "physics/Common.h"
#include "scenes/instances/SceneComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace Rapture {

namespace physics {
class CharacterBody;
class RigidBody;
} // namespace physics

/**
 * @brief A body that moves the object it is part of.
 */
class PhysicsBody3D : public SceneComponent {
  public:
    PhysicsBody3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Hands a simulated transform to the object this body moves
     * @param position Where the simulation put the body
     * @param rotation How the simulation oriented the body
     */
    virtual void applySimulatedTransform(const glm::vec3 &position, const glm::quat &rotation) = 0;

    /**
     * @brief Sets the velocity the object moves at until it is told otherwise
     * @param velocity World space velocity
     */
    virtual void setVelocity(const glm::vec3 &velocity) = 0;

  protected:
    /**
     * @brief Adds a rigid body to this object's scene, owned by this body
     * @param config What the body is built from
     * @return The body, or nullptr if the scene has no simulation to join
     */
    std::unique_ptr<physics::RigidBody> createRigidBody(const physics::RigidBodyConfig &config);

    /**
     * @brief Adds a character body to this object's scene, owned by this body
     * @param config What the body is built from
     * @return The body, or nullptr if the scene has no simulation to join
     */
    std::unique_ptr<physics::CharacterBody> createCharacterBody(const physics::CharacterBodyConfig &config);
};

} // namespace Rapture

#endif // RAPTURE__PHYSICS_BODY3D_H
