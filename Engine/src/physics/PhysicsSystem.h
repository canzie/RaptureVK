#ifndef RAPTURE__PHYSICS_SYSTEM_H
#define RAPTURE__PHYSICS_SYSTEM_H

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "physics/Internal.h"

namespace Rapture {

namespace physics {
class CharacterBody;
class RigidBody;
} // namespace physics

/**
 * @brief A world of bodies and the simulation that moves them.
 */
class PhysicsSystem {
  public:
    explicit PhysicsSystem(const physics::SystemConfig &config = {});
    ~PhysicsSystem();

    PhysicsSystem(const PhysicsSystem &) = delete;
    PhysicsSystem &operator=(const PhysicsSystem &) = delete;

    /**
     * @brief Advances the simulation
     * @param deltaTime Elapsed time in seconds
     */
    void onUpdate(float deltaTime);

    /**
     * @brief Adds a rigid body to the simulation
     * @param config What the body is built from
     * @param owner What the body moves, carried back on its state and never read here, which must outlive the body
     * @return The body, or nullptr if it could not be created
     */
    std::unique_ptr<physics::RigidBody> createRigidBody(const physics::RigidBodyConfig &config, void *owner);

    /**
     * @brief Adds a character body to the simulation
     * @param config What the body is built from
     * @param owner What the body walks, carried back on its state and never read here, which must outlive the body
     * @return The body, or nullptr if it could not be created
     */
    std::unique_ptr<physics::CharacterBody> createCharacterBody(const physics::CharacterBodyConfig &config, void *owner);

    /**
     * @brief Collects the state of every body that may have moved
     * @param outStates Receives one entry per awake rigid body and one per character body, replacing what it held
     */
    void getSimulatedStates(std::vector<physics::BodyState> &outStates) const;

    void setGravity(const glm::vec3 &gravity);
    glm::vec3 getGravity() const;

    /**
     * @brief Casts a ray against the simulation
     * @param origin Ray start in world space
     * @param direction Unit ray direction in world space
     * @param maxDistance Furthest distance tested along the direction
     * @return The closest hit, whose hit field is false when nothing was struck
     */
    physics::RaycastResult raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance) const;

    JPH::BodyInterface &bodyInterface() const { return *m_bodyInterface; }
    FreeList<physics::CharacterRecord> &characterRecords() { return m_characterRecords; }
    const FreeList<physics::CharacterRecord> &characterRecords() const { return m_characterRecords; }

  private:
    /**
     * @brief Sweeps every character body through the world
     * @param deltaTime Length of the step in seconds
     */
    void stepCharacters(float deltaTime);

  private:
    physics::BroadPhaseLayerInterfaceImpl m_broadPhaseLayerInterface;
    physics::ObjectVsBroadPhaseLayerFilterImpl m_objectVsBroadPhaseLayerFilter;
    physics::ObjectLayerPairFilterImpl m_objectLayerPairFilter;
    std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
    JPH::JobSystemThreadPool m_jobSystem;
    JPH::PhysicsSystem m_physicsSystem;
    JPH::BodyInterface *m_bodyInterface = nullptr;
    FreeList<physics::CharacterRecord> m_characterRecords;

    float m_fixedTimeStep;
    uint32_t m_maxStepsPerUpdate;
    float m_accumulator = 0.0f;
};

} // namespace Rapture

#endif // RAPTURE__PHYSICS_SYSTEM_H
