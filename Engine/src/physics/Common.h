#ifndef RAPTURE__PHYSICS_COMMON_H
#define RAPTURE__PHYSICS_COMMON_H

#include "logging/Log.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <variant>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture::physics {

enum MotionType {
    MOTION_STATIC,
    MOTION_KINEMATIC,
    MOTION_DYNAMIC,
    MOTION_COUNT
};

/**
 * @brief A motion type's stable name, safe to write to a file
 * @param type The type to name
 * @return The name, or an empty view for an unknown type
 */
inline std::string_view MotionType_toString(MotionType type)
{
    switch (type) {
    case MOTION_STATIC:
        return "static";
    case MOTION_KINEMATIC:
        return "kinematic";
    case MOTION_DYNAMIC:
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
inline MotionType MotionType_fromString(std::string_view name, MotionType fallback)
{
    for (uint32_t type = 0; type < MOTION_COUNT; ++type) {
        if (MotionType_toString(static_cast<MotionType>(type)) == name) {
            return static_cast<MotionType>(type);
        }
    }

    return fallback;
}

/**
 * @brief Lightweight handle to a rigid body owned by the PhysicsSystem.
 */
struct BodyId {
    static constexpr uint32_t INVALID = 0xFFFFFFFF;
    uint32_t value = INVALID;

    bool isValid() const { return value != INVALID; }
};

/**
 * @brief Lightweight handle to a character body owned by the PhysicsSystem.
 */
struct CharacterBodyId {
    static constexpr uint32_t INVALID = 0xFFFFFFFF;
    uint32_t value = INVALID;

    bool isValid() const { return value != INVALID; }
};

struct BoxShape {
    glm::vec3 halfExtents{0.5f};
};

struct SphereShape {
    float radius = 0.5f;
};

struct CapsuleShape {
    float halfHeight = 0.5f;
    float radius = 0.5f;
};

/**
 * @brief Names the alternatives of CollisionShape, in the order the variant declares them
 */
enum CollisionShapeType {
    COLLISION_SHAPE_BOX,
    COLLISION_SHAPE_SPHERE,
    COLLISION_SHAPE_CAPSULE,
    COLLISION_SHAPE_COUNT
};

using CollisionShape = std::variant<BoxShape, SphereShape, CapsuleShape>;

/**
 * @brief The type of the geometry a shape currently holds
 * @param shape The shape to inspect
 * @return The matching type
 */
inline CollisionShapeType CollisionShape_typeOf(const CollisionShape &shape)
{
    return static_cast<CollisionShapeType>(shape.index());
}

/**
 * @brief A shape type's stable name, safe to write to a file
 * @param type The type to name
 * @return The name, or an empty view for an unknown type
 */
inline std::string_view CollisionShape_toString(CollisionShapeType type)
{
    switch (type) {
    case COLLISION_SHAPE_BOX:
        return "box";
    case COLLISION_SHAPE_SPHERE:
        return "sphere";
    case COLLISION_SHAPE_CAPSULE:
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
inline CollisionShapeType CollisionShape_fromString(std::string_view name, CollisionShapeType fallback)
{
    for (uint32_t type = 0; type < COLLISION_SHAPE_COUNT; ++type) {
        if (CollisionShape_toString(static_cast<CollisionShapeType>(type)) == name) {
            return static_cast<CollisionShapeType>(type);
        }
    }

    return fallback;
}

/**
 * @brief A shape of a given type, at the dimensions that type starts out with
 * @param type The type of geometry to build
 * @return The shape
 */
inline CollisionShape CollisionShape_ofType(CollisionShapeType type)
{
    switch (type) {
    case COLLISION_SHAPE_BOX:
        return BoxShape{};
    case COLLISION_SHAPE_SPHERE:
        return SphereShape{};
    case COLLISION_SHAPE_CAPSULE:
        return CapsuleShape{};
    default:
        RP_CORE_ERROR("a collision shape type was added without a shape to build for it");
        return BoxShape{};
    }
}

/**
 * @brief A shape grown by a scale
 * @param shape The shape to scale
 * @param scale The scale to grow it by
 * @return The scaled shape
 */
inline CollisionShape CollisionShape_scaled(const CollisionShape &shape, const glm::vec3 &scale)
{
    const glm::vec3 magnitude = glm::abs(scale);

    if (const auto *box = std::get_if<BoxShape>(&shape)) {
        return BoxShape{box->halfExtents * magnitude};
    }

    // a sphere and a capsule stay round, so the widest axis is the one they take
    if (const auto *sphere = std::get_if<SphereShape>(&shape)) {
        return SphereShape{sphere->radius * std::max({magnitude.x, magnitude.y, magnitude.z})};
    }

    if (const auto *capsule = std::get_if<CapsuleShape>(&shape)) {
        return CapsuleShape{capsule->halfHeight * magnitude.y, capsule->radius * std::max(magnitude.x, magnitude.z)};
    }

    return shape;
}

/**
 * @brief What a character body is standing on, as of the last step it was advanced through.
 */
enum GroundState {
    GROUND_ON_GROUND,       ///< Standing on ground it can walk off in any direction
    GROUND_ON_STEEP_GROUND, ///< Standing on a slope past the slope limit, so it can only slide down
    GROUND_NOT_SUPPORTED,   ///< Touching something that does not hold it up, such as a wall it fell past
    GROUND_IN_AIR,          ///< Touching nothing at all
    GROUND_COUNT
};

struct RigidBodyConfig {
    CollisionShape shape;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    MotionType motionType = MOTION_DYNAMIC;
    float friction = 0.2f;
    float restitution = 0.0f;
    bool startActive = true;
};

struct CharacterBodyConfig {
    CollisionShape shape;
    glm::vec3 shapeOffset{0.0f}; ///< Offset of the shape from the origin, for standing the origin at the feet
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};            ///< Direction the character treats as upright
    float mass = 70.0f;                        ///< Kilograms, weighing on what the character stands on
    float maxStrength = 100.0f;                ///< Newtons, the hardest the character pushes other bodies
    float maxSlopeAngle = glm::radians(50.0f); ///< Steepest slope still walked up
    float characterPadding = 0.02f;            ///< Gap kept between the shape and geometry, so sweeps hit less
    float predictiveContactDistance = 0.1f;    ///< How far past the shape contacts are looked for, to slide rather than snag
    float stepUp = 0.4f;                       ///< Tallest step climbed, zero to stop climbing steps
    float stepDown = 0.5f;                     ///< How far the character is pulled back down onto a floor it walked off
    float jumpBufferTime = 0.15f;              ///< Seconds a jump asked for in the air waits for ground to arrive
};

/**
 * @brief What a character body is being asked to do, held until the step that carries it out.
 */
struct CharacterBodyMovement {
    glm::vec3 velocity{0.0f}; ///< World space velocity to walk at, whose component along up the fall replaces
    bool jump = false;        ///< Asks for a jump, which waits out the buffer time for ground before it lapses
    float jumpSpeed = 4.0f;   ///< Speed along up the jump leaves with
};

struct BodyState {
    void *owner = nullptr;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/**
 * @brief What a ray found.
 */
struct RaycastResult {
    bool hit = false;
    void *owner = nullptr;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

/**
 * @brief The settings a simulation is created with.
 */
struct SystemConfig {
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float fixedTimeStep = 1.0f / 60.0f;
    uint32_t maxStepsPerUpdate = 8;
    uint32_t maxBodies = 65536;
    uint32_t maxBodyPairs = 65536;
    uint32_t maxContactConstraints = 10240;
    uint32_t numBodyMutexes = 0;
    uint32_t tempAllocatorSize = 10u * 1024u * 1024u;
};

} // namespace Rapture::physics

#endif // RAPTURE__PHYSICS_COMMON_H
