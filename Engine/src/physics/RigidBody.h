#ifndef RAPTURE__PHYSICS_RIGID_BODY_H
#define RAPTURE__PHYSICS_RIGID_BODY_H

#include "physics/Common.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

class PhysicsSystem;

namespace physics {

/**
 * @brief A body the simulation moves.
 */
class RigidBody {
  public:
    RigidBody(PhysicsSystem &system, BodyId id);
    ~RigidBody();

    RigidBody(const RigidBody &) = delete;
    RigidBody &operator=(const RigidBody &) = delete;

    /**
     * @brief Reads this body's world transform
     * @param outPosition Receives the world space position
     * @param outRotation Receives the world space orientation
     */
    void getTransform(glm::vec3 &outPosition, glm::quat &outRotation) const;

    void setLinearVelocity(const glm::vec3 &velocity);
    glm::vec3 linearVelocity() const;

    void setFriction(float friction);
    void setRestitution(float restitution);

    /**
     * @brief Whether this body is awake
     * @return True while the simulation is still stepping it
     */
    bool isActive() const;

    BodyId id() const { return m_id; }

  private:
    PhysicsSystem *m_system = nullptr;
    BodyId m_id;
};

} // namespace physics
} // namespace Rapture

#endif // RAPTURE__PHYSICS_RIGID_BODY_H
