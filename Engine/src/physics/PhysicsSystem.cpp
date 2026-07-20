#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

#include "physics/PhysicsSystem.h"

#include "logging/Log.h"
#include "utils/rp_assert.h"

JPH_SUPPRESS_WARNINGS

namespace Rapture {

static constexpr JPH::ObjectLayer LAYER_NON_MOVING = 0;
static constexpr JPH::ObjectLayer LAYER_MOVING = 1;
static constexpr JPH::ObjectLayer LAYER_COUNT = 2;

static constexpr JPH::BroadPhaseLayer BROAD_PHASE_NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer BROAD_PHASE_MOVING(1);
static constexpr JPH::uint BROAD_PHASE_COUNT = 2;

static JPH::Vec3 s_glmToJolt(const glm::vec3 &v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

static JPH::RVec3 s_glmToJoltPosition(const glm::vec3 &v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

static JPH::Quat s_glmToJolt(const glm::quat &q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

static glm::vec3 s_joltToGlm(JPH::Vec3Arg v)
{
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

static glm::quat s_joltToGlm(JPH::QuatArg q)
{
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

static JPH::EMotionType s_toJoltMotionType(PhysicsMotionType type)
{
    switch (type) {
    case PHYSICS_MOTION_STATIC: {
        return JPH::EMotionType::Static;
    }
    case PHYSICS_MOTION_KINEMATIC: {
        return JPH::EMotionType::Kinematic;
    }
    case PHYSICS_MOTION_DYNAMIC: {
        return JPH::EMotionType::Dynamic;
    }
    default: {
        return JPH::EMotionType::Dynamic;
    }
    }
}

static JPH::ShapeRefC s_createShape(const PhysicsShape &shape)
{
    return std::visit(
        [](const auto &s) -> JPH::ShapeRefC {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, PhysicsBoxShape>) {
                return new JPH::BoxShape(s_glmToJolt(s.halfExtents));
            } else if constexpr (std::is_same_v<T, PhysicsSphereShape>) {
                return new JPH::SphereShape(s.radius);
            } else if constexpr (std::is_same_v<T, PhysicsCapsuleShape>) {
                return new JPH::CapsuleShape(s.halfHeight, s.radius);
            } else {
                static_assert(sizeof(T) == 0, "Unhandled physics shape type");
                return JPH::ShapeRefC{};
            }
        },
        shape);
}

static int s_jobThreadCount()
{
    const int hardwareThreads = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, hardwareThreads - 1);
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
#endif

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

static void s_traceImpl(const char *inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    RP_CORE_TRACE("[Jolt] {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool s_assertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, JPH::uint inLine)
{
    RP_CORE_ERROR("[Jolt] Assert failed at {}:{}: ({}) {}", inFile, inLine, inExpression, inMessage != nullptr ? inMessage : "");
    return true;
}
#endif

static std::atomic<int> gWorldCount{0};

static void s_registerJoltGlobals()
{
    if (gWorldCount.fetch_add(1) != 0) {
        return;
    }
    JPH::RegisterDefaultAllocator();
    JPH::Trace = s_traceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = s_assertFailedImpl;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

static void s_unregisterJoltGlobals()
{
    if (gWorldCount.fetch_sub(1) != 1) {
        return;
    }
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

struct PhysicsSystem::Impl {
    BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;
    JPH::TempAllocatorImpl tempAllocator;
    JPH::JobSystemThreadPool jobSystem;
    JPH::PhysicsSystem physicsSystem;
    JPH::BodyInterface *bodyInterface = nullptr;
    std::vector<PhysicsBodyState> activeBodyStates;

    float fixedTimeStep;
    uint32_t maxStepsPerUpdate;
    float accumulator = 0.0f;

    explicit Impl(const PhysicsConfig &config)
        : tempAllocator(config.tempAllocatorSize), jobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, s_jobThreadCount()),
          fixedTimeStep(config.fixedTimeStep), maxStepsPerUpdate(config.maxStepsPerUpdate)
    {
        physicsSystem.Init(config.maxBodies, config.numBodyMutexes, config.maxBodyPairs, config.maxContactConstraints,
                           broadPhaseLayerInterface, objectVsBroadPhaseLayerFilter, objectLayerPairFilter);
        physicsSystem.SetGravity(s_glmToJolt(config.gravity));
        bodyInterface = &physicsSystem.GetBodyInterface();
    }
};

PhysicsSystem::PhysicsSystem(const PhysicsConfig &config)
{
    s_registerJoltGlobals();
    m_impl = std::make_unique<Impl>(config);
}

PhysicsSystem::~PhysicsSystem()
{
    m_impl.reset();
    s_unregisterJoltGlobals();
}

void PhysicsSystem::onUpdate(float deltaTime)
{
    if (deltaTime <= 0.0f) {
        return;
    }

    m_impl->accumulator += deltaTime;

    const float fixedStep = m_impl->fixedTimeStep;
    uint32_t stepsTaken = 0;
    while (m_impl->accumulator >= fixedStep && stepsTaken < m_impl->maxStepsPerUpdate) {
        m_impl->physicsSystem.Update(fixedStep, 1, &m_impl->tempAllocator, &m_impl->jobSystem);
        m_impl->accumulator -= fixedStep;
        ++stepsTaken;
    }

    if (m_impl->accumulator >= fixedStep) {
        m_impl->accumulator = 0.0f;
    }
}

PhysicsBodyId PhysicsSystem::createRigidBody(const RigidBodyConfig &config, uint64_t userData)
{
    JPH::ShapeRefC shape = s_createShape(config.shape);
    if (shape == nullptr) {
        RP_CORE_ERROR("Failed to create physics shape");
        return {};
    }

    const JPH::ObjectLayer layer = (config.motionType == PHYSICS_MOTION_STATIC) ? LAYER_NON_MOVING : LAYER_MOVING;
    JPH::BodyCreationSettings settings(shape, s_glmToJoltPosition(config.position), s_glmToJolt(config.rotation),
                                       s_toJoltMotionType(config.motionType), layer);
    settings.mFriction = config.friction;
    settings.mRestitution = config.restitution;
    settings.mUserData = userData;

    const JPH::EActivation activation = config.startActive ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
    const JPH::BodyID bodyId = m_impl->bodyInterface->CreateAndAddBody(settings, activation);
    if (bodyId.IsInvalid()) {
        RP_CORE_ERROR("Failed to create physics body; the body pool may be full");
        return {};
    }
    return PhysicsBodyId{bodyId.GetIndexAndSequenceNumber()};
}

void PhysicsSystem::removeRigidBody(PhysicsBodyId body)
{
    if (!body.isValid()) {
        return;
    }
    const JPH::BodyID bodyId(body.value);
    m_impl->bodyInterface->RemoveBody(bodyId);
    m_impl->bodyInterface->DestroyBody(bodyId);
}

void PhysicsSystem::getBodyTransform(PhysicsBodyId body, glm::vec3 &outPosition, glm::quat &outRotation) const
{
    if (!body.isValid()) {
        return;
    }
    JPH::RVec3 position;
    JPH::Quat rotation;
    m_impl->bodyInterface->GetPositionAndRotation(JPH::BodyID(body.value), position, rotation);
    outPosition = s_joltToGlm(position);
    outRotation = s_joltToGlm(rotation);
}

void PhysicsSystem::setLinearVelocity(PhysicsBodyId body, const glm::vec3 &velocity)
{
    if (!body.isValid()) {
        return;
    }
    m_impl->bodyInterface->SetLinearVelocity(JPH::BodyID(body.value), s_glmToJolt(velocity));
}

glm::vec3 PhysicsSystem::getLinearVelocity(PhysicsBodyId body) const
{
    if (!body.isValid()) {
        return glm::vec3(0.0f);
    }
    return s_joltToGlm(m_impl->bodyInterface->GetLinearVelocity(JPH::BodyID(body.value)));
}

bool PhysicsSystem::isActive(PhysicsBodyId body) const
{
    if (!body.isValid()) {
        return false;
    }
    return m_impl->bodyInterface->IsActive(JPH::BodyID(body.value));
}

void PhysicsSystem::setGravity(const glm::vec3 &gravity)
{
    m_impl->physicsSystem.SetGravity(s_glmToJolt(gravity));
}

const std::vector<PhysicsBodyState> &PhysicsSystem::getActiveBodyStates()
{
    std::vector<PhysicsBodyState> &states = m_impl->activeBodyStates;
    states.clear();

    const uint32_t activeCount = m_impl->physicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    if (activeCount == 0) {
        return states;
    }

    const JPH::BodyID *activeBodies = m_impl->physicsSystem.GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
    states.reserve(activeCount);
    for (uint32_t i = 0; i < activeCount; ++i) {
        const JPH::BodyID bodyId = activeBodies[i];
        JPH::RVec3 position;
        JPH::Quat rotation;
        m_impl->bodyInterface->GetPositionAndRotation(bodyId, position, rotation);

        PhysicsBodyState state;
        state.userData = m_impl->bodyInterface->GetUserData(bodyId);
        state.position = s_joltToGlm(position);
        state.rotation = s_joltToGlm(rotation);
        states.push_back(state);
    }

    return states;
}

} // namespace Rapture
