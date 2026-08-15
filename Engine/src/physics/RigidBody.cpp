#include "physics/RigidBody.h"

#include "physics/PhysicsSystem.h"

namespace Rapture {
namespace physics {

RigidBody::RigidBody(PhysicsSystem &system, BodyId id) : m_system(&system), m_id(id) {}

RigidBody::~RigidBody()
{
    if (!m_id.isValid()) {
        return;
    }

    const JPH::BodyID bodyId(m_id.value);
    m_system->bodyInterface().RemoveBody(bodyId);
    m_system->bodyInterface().DestroyBody(bodyId);
}

void RigidBody::getTransform(glm::vec3 &outPosition, glm::quat &outRotation) const
{
    if (!m_id.isValid()) {
        return;
    }

    JPH::RVec3 position;
    JPH::Quat rotation;
    m_system->bodyInterface().GetPositionAndRotation(JPH::BodyID(m_id.value), position, rotation);
    outPosition = joltToGlmVec3(position);
    outRotation = joltToGlmQuat(rotation);
}

void RigidBody::setLinearVelocity(const glm::vec3 &velocity)
{
    if (!m_id.isValid()) {
        return;
    }

    m_system->bodyInterface().SetLinearVelocity(JPH::BodyID(m_id.value), glmToJoltVec3(velocity));
}

glm::vec3 RigidBody::linearVelocity() const
{
    if (!m_id.isValid()) {
        return glm::vec3(0.0f);
    }

    return joltToGlmVec3(m_system->bodyInterface().GetLinearVelocity(JPH::BodyID(m_id.value)));
}

void RigidBody::setFriction(float friction)
{
    if (!m_id.isValid()) {
        return;
    }

    m_system->bodyInterface().SetFriction(JPH::BodyID(m_id.value), friction);
}

void RigidBody::setRestitution(float restitution)
{
    if (!m_id.isValid()) {
        return;
    }

    m_system->bodyInterface().SetRestitution(JPH::BodyID(m_id.value), restitution);
}

bool RigidBody::isActive() const
{
    if (!m_id.isValid()) {
        return false;
    }

    return m_system->bodyInterface().IsActive(JPH::BodyID(m_id.value));
}

} // namespace physics
} // namespace Rapture
