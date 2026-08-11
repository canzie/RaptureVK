#ifndef RAPTURE__PHYSICS_COMMON_H
#define RAPTURE__PHYSICS_COMMON_H

#include <cstdint>
#include <string_view>
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
 * @brief A motion type's stable name, safe to write to a file
 * @param type The type to name
 * @return The name, or an empty view for an unknown type
 */
inline std::string_view PhysicsMotionType_toString(PhysicsMotionType type)
{
    switch (type) {
    case PHYSICS_MOTION_STATIC:
        return "static";
    case PHYSICS_MOTION_KINEMATIC:
        return "kinematic";
    case PHYSICS_MOTION_DYNAMIC:
        return "dynamic";
    default:
        return {};
    }
}

/**
 * @brief Reads a motion type back from its stable name
 * @param name The name to look up
 * @param fallback Returned when the name matches no type
 * @return The type
 */
inline PhysicsMotionType PhysicsMotionType_fromString(std::string_view name, PhysicsMotionType fallback)
{
    for (uint32_t type = 0; type < PHYSICS_MOTION_COUNT; ++type) {
        if (PhysicsMotionType_toString(static_cast<PhysicsMotionType>(type)) == name) {
            return static_cast<PhysicsMotionType>(type);
        }
    }

    return fallback;
}

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

/**
 * @brief Names the alternatives of PhysicsShape, in the order the variant declares them
 */
enum PhysicsShapeType {
    PHYSICS_SHAPE_BOX,
    PHYSICS_SHAPE_SPHERE,
    PHYSICS_SHAPE_CAPSULE,
    PHYSICS_SHAPE_COUNT
};

using PhysicsShape = std::variant<PhysicsBoxShape, PhysicsSphereShape, PhysicsCapsuleShape>;

/**
 * @brief The type of the geometry a shape currently holds
 * @param shape The shape to inspect
 * @return The matching type
 */
inline PhysicsShapeType PhysicsShape_typeOf(const PhysicsShape &shape)
{
    return static_cast<PhysicsShapeType>(shape.index());
}

/**
 * @brief A shape type's stable name, safe to write to a file
 * @param type The type to name
 * @return The name, or an empty view for an unknown type
 */
inline std::string_view PhysicsShape_toString(PhysicsShapeType type)
{
    switch (type) {
    case PHYSICS_SHAPE_BOX:
        return "box";
    case PHYSICS_SHAPE_SPHERE:
        return "sphere";
    case PHYSICS_SHAPE_CAPSULE:
        return "capsule";
    default:
        return {};
    }
}

/**
 * @brief Reads a shape type back from its stable name
 * @param name The name to look up
 * @param fallback Returned when the name matches no type
 * @return The type
 */
inline PhysicsShapeType PhysicsShape_fromString(std::string_view name, PhysicsShapeType fallback)
{
    for (uint32_t type = 0; type < PHYSICS_SHAPE_COUNT; ++type) {
        if (PhysicsShape_toString(static_cast<PhysicsShapeType>(type)) == name) {
            return static_cast<PhysicsShapeType>(type);
        }
    }

    return fallback;
}

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

struct PhysicsRaycastHit {
    bool hit = false;
    uint64_t userData = 0;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
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
