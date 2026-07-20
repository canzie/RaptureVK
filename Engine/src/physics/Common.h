#ifndef RAPTURE__PHYSICS_COMMON_H
#define RAPTURE__PHYSICS_COMMON_H

#include <cstdint>
#include <variant>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

enum PhysicsMotionType {
    PHYSICS_MOTION_STATIC,
    PHYSICS_MOTION_KINEMATIC,
    PHYSICS_MOTION_DYNAMIC,
    PHYSICS_MOTION_COUNT
};

/**
 * @brief Lightweight handle to a rigid body owned by the PhysicsSystem.
 */
struct PhysicsBodyId {
    static constexpr uint32_t INVALID = 0xFFFFFFFF;
    uint32_t value = INVALID;

    bool isValid() const { return value != INVALID; }
};

struct PhysicsBoxShape {
    glm::vec3 halfExtents{0.5f};
};

struct PhysicsSphereShape {
    float radius = 0.5f;
};

struct PhysicsCapsuleShape {
    float halfHeight = 0.5f;
    float radius = 0.5f;
};

using PhysicsShape = std::variant<PhysicsBoxShape, PhysicsSphereShape, PhysicsCapsuleShape>;

struct RigidBodyConfig {
    PhysicsShape shape;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    PhysicsMotionType motionType = PHYSICS_MOTION_DYNAMIC;
    float friction = 0.2f;
    float restitution = 0.0f;
    bool startActive = true;
};

struct PhysicsBodyState {
    uint64_t userData = 0;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct PhysicsConfig {
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float fixedTimeStep = 1.0f / 60.0f;
    uint32_t maxStepsPerUpdate = 8;
    uint32_t maxBodies = 65536;
    uint32_t maxBodyPairs = 65536;
    uint32_t maxContactConstraints = 10240;
    uint32_t numBodyMutexes = 0;
    uint32_t tempAllocatorSize = 10u * 1024u * 1024u;
};

} // namespace Rapture

#endif // RAPTURE__PHYSICS_COMMON_H
