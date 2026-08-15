#include "physics/PhysicsSystem.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

#include "physics/CharacterBody.h"
#include "physics/RigidBody.h"

#include <atomic>
#include <cstdarg>
#include <thread>

JPH_SUPPRESS_WARNINGS

namespace Rapture {

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
#endif // JPH_ENABLE_ASSERTS

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

static int s_jobThreadCount()
{
    const int hardwareThreads = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, hardwareThreads - 1);
}

// A jump that arrives mid air is served by the first step that finds ground, but only for as long
// as the buffer lasts. Without the lapse a jump asked for at the top of a long fall still fires on
// landing, however much later that is.
static constexpr float JUMP_SETTLE_SPEED = 0.1f;

/**
 * @brief Builds the velocity a character body leaves a step with
 * @param record The character being stepped, whose buffered jump is taken here
 * @param up The character's upright direction
 * @param gravity Gravity in world space
 * @param deltaTime Length of the step in seconds
 * @return The velocity to step with
 */
static JPH::Vec3 s_characterStepVelocity(physics::CharacterRecord &record, JPH::Vec3Arg up, JPH::Vec3Arg gravity, float deltaTime)
{
    const JPH::CharacterVirtual &character = *record.character;
    const JPH::Vec3 ground = character.GetGroundVelocity();

    // taking back what the character was told to move at leaves what the world did to it, so a fall
    // keeps building while a wall it walked into does not
    JPH::Vec3 carried = character.GetLinearVelocity() - physics::glmToJoltVec3(record.prevVelocity);
    carried += gravity * deltaTime;

    // measured against the ground rather than the world, so a character riding a lift is still
    // standing on it and a character already rising out of a jump is not
    const bool settled = (carried.Dot(up) - ground.Dot(up)) < JUMP_SETTLE_SPEED;
    const bool grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && settled;
    if (grounded) {
        carried -= up * carried.Dot(up);
        carried += up * ground.Dot(up);

        if (record.jumpBufferRemaining > 0.0f) {
            carried += up * record.movement.jumpSpeed;
            record.jumpBufferRemaining = 0.0f;
        }
    }

    record.jumpBufferRemaining = std::max(0.0f, record.jumpBufferRemaining - deltaTime);

    const JPH::Vec3 requested = physics::glmToJoltVec3(record.movement.velocity);
    JPH::Vec3 applied = requested - up * requested.Dot(up);
    if (grounded) {
        // carries the character along with whatever it is standing on
        applied += ground - up * ground.Dot(up);
    }

    record.prevVelocity = physics::joltToGlmVec3(applied);
    return carried + applied;
}

PhysicsSystem::PhysicsSystem(const physics::SystemConfig &config)
    : m_fixedTimeStep(config.fixedTimeStep), m_maxStepsPerUpdate(config.maxStepsPerUpdate)
{
    s_registerJoltGlobals();

    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(config.tempAllocatorSize);
    m_jobSystem.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, s_jobThreadCount());

    m_physicsSystem.Init(config.maxBodies, config.numBodyMutexes, config.maxBodyPairs, config.maxContactConstraints,
                         m_broadPhaseLayerInterface, m_objectVsBroadPhaseLayerFilter, m_objectLayerPairFilter);
    m_physicsSystem.SetGravity(physics::glmToJoltVec3(config.gravity));
    m_bodyInterface = &m_physicsSystem.GetBodyInterface();
}

PhysicsSystem::~PhysicsSystem()
{
    m_characterRecords.clear();
    s_unregisterJoltGlobals();
}

void PhysicsSystem::onUpdate(float deltaTime)
{
    if (deltaTime <= 0.0f) {
        return;
    }

    m_accumulator += deltaTime;

    uint32_t stepsTaken = 0;
    while (m_accumulator >= m_fixedTimeStep && stepsTaken < m_maxStepsPerUpdate) {
        m_physicsSystem.Update(m_fixedTimeStep, 1, m_tempAllocator.get(), &m_jobSystem);
        stepCharacters(m_fixedTimeStep);
        m_accumulator -= m_fixedTimeStep;
        ++stepsTaken;
    }

    if (m_accumulator >= m_fixedTimeStep) {
        m_accumulator = 0.0f;
    }
}

void PhysicsSystem::stepCharacters(float deltaTime)
{
    if (m_characterRecords.size() == 0) {
        return;
    }

    const JPH::Vec3 gravity = m_physicsSystem.GetGravity();
    const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter =
        m_physicsSystem.GetDefaultBroadPhaseLayerFilter(physics::LAYER_MOVING);
    const JPH::DefaultObjectLayerFilter objectFilter = m_physicsSystem.GetDefaultLayerFilter(physics::LAYER_MOVING);
    const JPH::BodyFilter bodyFilter;
    const JPH::ShapeFilter shapeFilter;

    m_characterRecords.forEach([&](uint32_t id, physics::CharacterRecord &record) {
        (void)id;

        const JPH::Vec3 up = physics::glmToJoltVec3(record.up);
        record.character->SetLinearVelocity(s_characterStepVelocity(record, up, gravity, deltaTime));

        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mStickToFloorStepDown = -up * record.stepDown;
        settings.mWalkStairsStepUp = up * record.stepUp;

        record.character->ExtendedUpdate(deltaTime, gravity, settings, broadPhaseFilter, objectFilter, bodyFilter, shapeFilter,
                                         *m_tempAllocator);
    });
}

std::unique_ptr<physics::RigidBody> PhysicsSystem::createRigidBody(const physics::RigidBodyConfig &config, void *owner)
{
    JPH::ShapeRefC shape = physics::createJoltShape(config.shape);
    if (shape == nullptr) {
        RP_CORE_ERROR("Failed to create physics shape");
        return nullptr;
    }

    const JPH::ObjectLayer layer =
        (config.motionType == physics::MOTION_STATIC) ? physics::LAYER_NON_MOVING : physics::LAYER_MOVING;
    JPH::BodyCreationSettings settings(shape, physics::glmToJoltPosition(config.position), physics::glmToJoltQuat(config.rotation),
                                       physics::motionTypeToJolt(config.motionType), layer);
    settings.mFriction = config.friction;
    settings.mRestitution = config.restitution;
    settings.mUserData = reinterpret_cast<uint64_t>(owner);

    const JPH::EActivation activation = config.startActive ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
    const JPH::BodyID bodyId = m_bodyInterface->CreateAndAddBody(settings, activation);
    if (bodyId.IsInvalid()) {
        RP_CORE_ERROR("Failed to create physics body; the body pool may be full");
        return nullptr;
    }

    return std::make_unique<physics::RigidBody>(*this, physics::BodyId{bodyId.GetIndexAndSequenceNumber()});
}

std::unique_ptr<physics::CharacterBody> PhysicsSystem::createCharacterBody(const physics::CharacterBodyConfig &config, void *owner)
{
    JPH::ShapeRefC shape = physics::createJoltShape(config.shape);
    if (shape == nullptr) {
        RP_CORE_ERROR("Failed to create character shape");
        return nullptr;
    }

    if (config.shapeOffset != glm::vec3(0.0f)) {
        shape = new JPH::RotatedTranslatedShape(physics::glmToJoltVec3(config.shapeOffset), JPH::Quat::sIdentity(), shape);
    }

    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mUp = physics::glmToJoltVec3(config.up);
    settings.mMass = config.mass;
    settings.mMaxStrength = config.maxStrength;
    settings.mMaxSlopeAngle = config.maxSlopeAngle;
    settings.mCharacterPadding = config.characterPadding;
    settings.mPredictiveContactDistance = config.predictiveContactDistance;

    physics::CharacterRecord record;
    record.character = new JPH::CharacterVirtual(&settings, physics::glmToJoltPosition(config.position),
                                                 physics::glmToJoltQuat(config.rotation),
                                                 reinterpret_cast<uint64_t>(owner), &m_physicsSystem);
    record.owner = owner;
    record.up = config.up;
    record.stepUp = config.stepUp;
    record.stepDown = config.stepDown;
    record.jumpBufferTime = config.jumpBufferTime;

    const uint32_t id = m_characterRecords.insert(std::move(record));
    return std::make_unique<physics::CharacterBody>(*this, physics::CharacterBodyId{id});
}

void PhysicsSystem::getSimulatedStates(std::vector<physics::BodyState> &outStates) const
{
    outStates.clear();

    const uint32_t activeCount = m_physicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    outStates.reserve(activeCount + m_characterRecords.size());

    const JPH::BodyID *activeBodies = m_physicsSystem.GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
    for (uint32_t i = 0; i < activeCount; ++i) {
        const JPH::BodyID bodyId = activeBodies[i];
        JPH::RVec3 position;
        JPH::Quat rotation;
        m_bodyInterface->GetPositionAndRotation(bodyId, position, rotation);

        physics::BodyState state;
        state.owner = reinterpret_cast<void *>(m_bodyInterface->GetUserData(bodyId));
        state.position = physics::joltToGlmVec3(position);
        state.rotation = physics::joltToGlmQuat(rotation);
        outStates.push_back(state);
    }

    m_characterRecords.forEach([&outStates](uint32_t id, const physics::CharacterRecord &record) {
        (void)id;

        physics::BodyState state;
        state.owner = record.owner;
        state.position = physics::joltToGlmVec3(record.character->GetPosition());
        state.rotation = physics::joltToGlmQuat(record.character->GetRotation());
        outStates.push_back(state);
    });
}

void PhysicsSystem::setGravity(const glm::vec3 &gravity)
{
    m_physicsSystem.SetGravity(physics::glmToJoltVec3(gravity));
}

glm::vec3 PhysicsSystem::getGravity() const
{
    return physics::joltToGlmVec3(m_physicsSystem.GetGravity());
}

physics::RaycastResult PhysicsSystem::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance) const
{
    physics::RaycastResult hit;

    const JPH::RRayCast ray(physics::glmToJoltPosition(origin), physics::glmToJoltVec3(direction) * maxDistance);

    JPH::RayCastResult result;
    if (!m_physicsSystem.GetNarrowPhaseQuery().CastRay(ray, result)) {
        return hit;
    }

    const JPH::RVec3 hitPosition = ray.mOrigin + result.mFraction * ray.mDirection;
    hit.hit = true;
    hit.position = physics::joltToGlmVec3(hitPosition);
    hit.distance = maxDistance * result.mFraction;

    JPH::BodyLockRead lock(m_physicsSystem.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body &body = lock.GetBody();
        hit.owner = reinterpret_cast<void *>(body.GetUserData());
        hit.normal = physics::joltToGlmVec3(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPosition));
    }

    return hit;
}

} // namespace Rapture
