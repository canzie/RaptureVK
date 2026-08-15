#ifndef RAPTURE__PHYSICS_CHARACTER_BODY_H
#define RAPTURE__PHYSICS_CHARACTER_BODY_H

#include "physics/Common.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

class PhysicsSystem;

namespace physics {

/**
 * @brief A body the simulation moves only where it is told to.
 */
class CharacterBody {
  public:
    CharacterBody(PhysicsSystem &system, CharacterBodyId id);
    ~CharacterBody();

    CharacterBody(const CharacterBody &) = delete;
    CharacterBody &operator=(const CharacterBody &) = delete;

    /**
     * @brief Sets what this body does on the steps that follow
     * @param movement Velocity to walk at and whether to jump
     */
    void setMovement(const CharacterBodyMovement &movement);

    /**
     * @brief Reads this body's world transform
     * @param outPosition Receives the world space position
     * @param outRotation Receives the world space orientation
     */
    void getTransform(glm::vec3 &outPosition, glm::quat &outRotation) const;

    /**
     * @brief Places this body
     * @param position World space position
     * @param rotation World space orientation
     */
    void setTransform(const glm::vec3 &position, const glm::quat &rotation);

    /**
     * @brief Turns this body
     * @param rotation World space orientation
     */
    void setRotation(const glm::quat &rotation);

    void setLinearVelocity(const glm::vec3 &velocity);

    void setMass(float mass);
    void setMaxSlopeAngle(float radians);
    void setStepUp(float stepUp);
    void setStepDown(float stepDown);

    GroundState groundState() const;
    glm::vec3 linearVelocity() const;

    CharacterBodyId id() const { return m_id; }

  private:
    PhysicsSystem *m_system = nullptr;
    CharacterBodyId m_id;
};

} // namespace physics
} // namespace Rapture

#endif // RAPTURE__PHYSICS_CHARACTER_BODY_H
