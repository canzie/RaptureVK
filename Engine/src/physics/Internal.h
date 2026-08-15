#ifndef RAPTURE__PHYSICS_INTERNAL_H
#define RAPTURE__PHYSICS_INTERNAL_H

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "logging/Log.h"
#include "physics/Common.h"
#include "utils/FreeList.h"
#include "utils/rp_assert.h"

#include <algorithm>
#include <type_traits>
#include <vector>

namespace Rapture {
namespace physics {

static constexpr JPH::ObjectLayer LAYER_NON_MOVING = 0;
static constexpr JPH::ObjectLayer LAYER_MOVING = 1;
static constexpr JPH::ObjectLayer LAYER_COUNT = 2;

static constexpr JPH::BroadPhaseLayer BROAD_PHASE_NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer BROAD_PHASE_MOVING(1);
static constexpr JPH::uint BROAD_PHASE_COUNT = 2;

// A shape thinner than this has no volume, so a dynamic body built from it would have no mass. Flat
// geometry reaches here whenever a box is derived from the bounds of a plane.
static constexpr float MIN_SHAPE_EXTENT = JPH::cDefaultConvexRadius;

inline JPH::Vec3 glmToJoltVec3(const glm::vec3 &v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

inline JPH::RVec3 glmToJoltPosition(const glm::vec3 &v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

inline JPH::Quat glmToJoltQuat(const glm::quat &q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

inline glm::vec3 joltToGlmVec3(JPH::Vec3Arg v)
{
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

inline glm::quat joltToGlmQuat(JPH::QuatArg q)
{
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

inline JPH::EMotionType motionTypeToJolt(MotionType type)
{
    switch (type) {
    case MOTION_STATIC: {
        return JPH::EMotionType::Static;
    }
    case MOTION_KINEMATIC: {
        return JPH::EMotionType::Kinematic;
    }
    case MOTION_DYNAMIC: {
        return JPH::EMotionType::Dynamic;
    }
    default: {
        return JPH::EMotionType::Dynamic;
    }
    }
}

inline GroundState joltToGroundState(JPH::CharacterBase::EGroundState state)
{
    switch (state) {
    case JPH::CharacterBase::EGroundState::OnGround: {
        return GROUND_ON_GROUND;
    }
    case JPH::CharacterBase::EGroundState::OnSteepGround: {
        return GROUND_ON_STEEP_GROUND;
    }
    case JPH::CharacterBase::EGroundState::NotSupported: {
        return GROUND_NOT_SUPPORTED;
    }
    default: {
        return GROUND_IN_AIR;
    }
    }
}

inline JPH::ShapeRefC createJoltShape(const CollisionShape &shape)
{
    return std::visit(
        [](const auto &s) -> JPH::ShapeRefC {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, BoxShape>) {
                return new JPH::BoxShape(glmToJoltVec3(glm::max(s.halfExtents, glm::vec3(MIN_SHAPE_EXTENT))));
            } else if constexpr (std::is_same_v<T, SphereShape>) {
                return new JPH::SphereShape(std::max(s.radius, MIN_SHAPE_EXTENT));
            } else if constexpr (std::is_same_v<T, CapsuleShape>) {
                return new JPH::CapsuleShape(std::max(s.halfHeight, MIN_SHAPE_EXTENT), std::max(s.radius, MIN_SHAPE_EXTENT));
            } else {
                static_assert(sizeof(T) == 0, "Unhandled physics shape type");
                return JPH::ShapeRefC{};
            }
        },
        shape);
}

class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
  public:
    BroadPhaseLayerInterfaceImpl()
    {
        m_objectToBroadPhase[LAYER_NON_MOVING] = BROAD_PHASE_NON_MOVING;
        m_objectToBroadPhase[LAYER_MOVING] = BROAD_PHASE_MOVING;
    }

    JPH::uint GetNumBroadPhaseLayers() const override { return BROAD_PHASE_COUNT; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        RP_ASSERT(inLayer < LAYER_COUNT, "Physics object layer out of range");
        return m_objectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer)) {
        case static_cast<JPH::BroadPhaseLayer::Type>(BROAD_PHASE_NON_MOVING): {
            return "NON_MOVING";
        }
        case static_cast<JPH::BroadPhaseLayer::Type>(BROAD_PHASE_MOVING): {
            return "MOVING";
        }
        default: {
            return "INVALID";
        }
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

  private:
    JPH::BroadPhaseLayer m_objectToBroadPhase[LAYER_COUNT];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1) {
        case LAYER_NON_MOVING: {
            return inLayer2 == BROAD_PHASE_MOVING;
        }
        case LAYER_MOVING: {
            return true;
        }
        default: {
            return false;
        }
        }
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
    {
        switch (inObject1) {
        case LAYER_NON_MOVING: {
            return inObject2 == LAYER_MOVING;
        }
        case LAYER_MOVING: {
            return true;
        }
        default: {
            return false;
        }
        }
    }
};

/**
 * @brief One character body's Jolt state, and what it has been asked to do next.
 */
struct CharacterRecord {
    JPH::Ref<JPH::CharacterVirtual> character;
    void *owner = nullptr;
    CharacterBodyMovement movement;
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float stepUp = 0.4f;
    float stepDown = 0.5f;
    float jumpBufferTime = 0.15f;
    float jumpBufferRemaining = 0.0f; ///< Seconds an unserved jump has left before it lapses
    glm::vec3 prevVelocity{0.0f};
};

} // namespace physics
} // namespace Rapture

#endif // RAPTURE__PHYSICS_INTERNAL_H
