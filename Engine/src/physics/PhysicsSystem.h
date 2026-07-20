#ifndef RAPTURE__PHYSICS_SYSTEM_H
#define RAPTURE__PHYSICS_SYSTEM_H

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "physics/Common.h"

namespace Rapture {

/**
 * @brief Rigid body physics simulation.
 *
 * Wraps the underlying physics library so no third party primitive leaks into
 * the rest of the engine. Create bodies, step the simulation and read results
 * back in engine terms (glm and PhysicsBodyId).
 */
class PhysicsSystem {
  public:
    explicit PhysicsSystem(const PhysicsConfig &config = {});
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem &) = delete;
    PhysicsSystem &operator=(const PhysicsSystem &) = delete;

    /**
     * @brief Advance the simulation by a time step.
     * @param deltaTime Elapsed time in seconds.
     */
    void onUpdate(float deltaTime);

    /**
     * @brief Create a rigid body and add it to the simulation.
     * @param config Body shape, transform and material parameters.
     * @param userData Opaque value stored on the body, returned in PhysicsBodyState.
     * @return Handle to the new body, or an invalid handle on failure.
     */
    PhysicsBodyId createRigidBody(const RigidBodyConfig &config, uint64_t userData = 0);

    /**
     * @brief Get the state of every currently active (awake) body.
     * @return Reference to an internal list, valid until the next call.
     */
    const std::vector<PhysicsBodyState> &getActiveBodyStates();

    /**
     * @brief Remove a body from the simulation and destroy it.
     * @param body Handle returned by createRigidBody.
     */
    void removeRigidBody(PhysicsBodyId body);

    /**
     * @brief Read a body's world space position and orientation.
     * @param body Body to query.
     * @param outPosition Receives the world space position.
     * @param outRotation Receives the world space orientation.
     */
    void getBodyTransform(PhysicsBodyId body, glm::vec3 &outPosition, glm::quat &outRotation) const;

    /**
     * @brief Set a body's linear velocity.
     * @param body Body to modify.
     * @param velocity Linear velocity in world space.
     */
    void setLinearVelocity(PhysicsBodyId body, const glm::vec3 &velocity);

    /**
     * @brief Get a body's linear velocity.
     * @param body Body to query.
     * @return Linear velocity in world space, or zero for an invalid handle.
     */
    glm::vec3 getLinearVelocity(PhysicsBodyId body) const;

    /**
     * @brief Check whether a body is currently active (awake).
     * @param body Body to query.
     * @return True if the body is simulating.
     */
    bool isActive(PhysicsBodyId body) const;

    /**
     * @brief Set the world gravity vector.
     * @param gravity Gravity acceleration in world space.
     */
    void setGravity(const glm::vec3 &gravity);

    /**
     * @brief Cast a ray against the simulation and return the closest hit.
     * @param origin Ray start in world space.
     * @param direction Unit ray direction in world space.
     * @param maxDistance Maximum distance to test along the direction.
     * @return The closest hit; its hit field is false when nothing was struck.
     */
    PhysicsRaycastHit raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Rapture

#endif // RAPTURE__PHYSICS_SYSTEM_H
