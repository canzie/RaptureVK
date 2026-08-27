#include "Controller.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Rapture {

static const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

Controller::Controller(Scene &scene, std::string_view name) : SceneObject(scene, name)
{
    setTickPhase(TICK_INPUT);
    setTickEnabled(true);
}

const TypeInfo &Controller::staticType()
{
    static const TypeInfo type("Controller", &SceneObject::staticType());
    return type;
}

const TypeInfo &Controller::type() const
{
    return staticType();
}

void Controller::onUpdate(float dt)
{
    onUpdateEvent.fire(dt);
}

void Controller::addYawInput(float degrees)
{
    m_yaw += degrees;
}

void Controller::addPitchInput(float degrees)
{
    m_pitch = glm::clamp(m_pitch + degrees, -m_maxPitch, m_maxPitch);
}

void Controller::setControlRotation(float yawDegrees, float pitchDegrees)
{
    m_yaw = yawDegrees;
    m_pitch = glm::clamp(pitchDegrees, -m_maxPitch, m_maxPitch);
}

glm::quat Controller::controlRotation() const
{
    // an aim faces down its own -Z, so the turn is the one taking -Z onto the flattened forward
    glm::vec3 forward = controlForward();
    float turn = std::atan2(-forward.x, -forward.z);

    return glm::angleAxis(turn, WORLD_UP) * glm::angleAxis(glm::radians(m_pitch), glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 Controller::controlForward() const
{
    float yaw = glm::radians(m_yaw);
    return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
}

glm::vec3 Controller::controlRight() const
{
    return glm::cross(controlForward(), WORLD_UP);
}

void Controller::possess(SceneObject *subject)
{
    m_possessed = subject;
    onPossessionChanged.fire(subject);
}

void Controller::unpossess()
{
    m_possessed = nullptr;
    onPossessionChanged.fire(nullptr);
}

} // namespace Rapture
