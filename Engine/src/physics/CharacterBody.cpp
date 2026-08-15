#include "physics/CharacterBody.h"

#include "physics/PhysicsSystem.h"

namespace Rapture {
namespace physics {

CharacterBody::CharacterBody(PhysicsSystem &system, CharacterBodyId id) : m_system(&system), m_id(id) {}

CharacterBody::~CharacterBody()
{
    if (!m_id.isValid()) {
        return;
    }

    m_system->characterRecords().remove(m_id.value);
}

void CharacterBody::setMovement(const CharacterBodyMovement &movement)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    CharacterRecord &record = m_system->characterRecords()[m_id.value];

    // a jump waits for ground rather than being dropped by the next movement, so a press just
    // before landing is still served, but it lapses once the buffer runs out
    if (movement.jump) {
        record.jumpBufferRemaining = record.jumpBufferTime;
    }

    record.movement = movement;
}

void CharacterBody::getTransform(glm::vec3 &outPosition, glm::quat &outRotation) const
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        outPosition = glm::vec3(0.0f);
        outRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const JPH::CharacterVirtual &character = *m_system->characterRecords()[m_id.value].character;
    outPosition = joltToGlmVec3(character.GetPosition());
    outRotation = joltToGlmQuat(character.GetRotation());
}

void CharacterBody::setTransform(const glm::vec3 &position, const glm::quat &rotation)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    JPH::CharacterVirtual &character = *m_system->characterRecords()[m_id.value].character;
    character.SetPosition(glmToJoltPosition(position));
    character.SetRotation(glmToJoltQuat(rotation));
}

void CharacterBody::setRotation(const glm::quat &rotation)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    m_system->characterRecords()[m_id.value].character->SetRotation(glmToJoltQuat(rotation));
}

void CharacterBody::setLinearVelocity(const glm::vec3 &velocity)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    m_system->characterRecords()[m_id.value].character->SetLinearVelocity(glmToJoltVec3(velocity));
}

void CharacterBody::setMass(float mass)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    m_system->characterRecords()[m_id.value].character->SetMass(mass);
}

void CharacterBody::setMaxSlopeAngle(float radians)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    m_system->characterRecords()[m_id.value].character->SetMaxSlopeAngle(radians);
}

void CharacterBody::setStepUp(float stepUp)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    m_system->characterRecords()[m_id.value].stepUp = stepUp;
}

void CharacterBody::setStepDown(float stepDown)
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return;
    }

    m_system->characterRecords()[m_id.value].stepDown = stepDown;
}

GroundState CharacterBody::groundState() const
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return GROUND_IN_AIR;
    }

    return joltToGroundState(m_system->characterRecords()[m_id.value].character->GetGroundState());
}

glm::vec3 CharacterBody::linearVelocity() const
{
    if (!m_system->characterRecords().isLive(m_id.value)) {
        return glm::vec3(0.0f);
    }

    return joltToGlmVec3(m_system->characterRecords()[m_id.value].character->GetLinearVelocity());
}

} // namespace physics
} // namespace Rapture
