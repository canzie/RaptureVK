#include "PlayerController.h"

#include "components/Components.h"
#include "logging/Log.h"
#include "scenes/instances/Camera3D.h"
#include "scenes/instances/CharacterBody3D.h"
#include "scenes/instances/Node3D.h"
#include "scenes/instances/PhysicsBody3D.h"
#include "scenes/instances/SpringArm3D.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Rapture {

static const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

static constexpr std::string_view KEY_MOVEMENT_SPEED = "movementSpeed";
static constexpr std::string_view KEY_MOUSE_SENSITIVITY = "mouseSensitivity";
static constexpr std::string_view KEY_MAX_PITCH = "maxPitch";

PlayerController::PlayerController(Scene &scene, std::string_view name) : Controller(scene, name) {}

const TypeInfo &PlayerController::staticType()
{
    static const TypeInfo type("PlayerController", &Controller::staticType());
    return type;
}

const TypeInfo &PlayerController::type() const
{
    return staticType();
}

void PlayerController::possess(SceneObject *subject)
{
    Node3D *node = subject != nullptr ? subject->as<Node3D>() : nullptr;
    if (node == nullptr) {
        RP_CORE_WARN("a player controller only drives a puppet that has a place in the world");
        return;
    }

    Controller::possess(node);

    m_body = node->component<PhysicsBody3D>();
    m_characterBody = m_body != nullptr ? m_body->as<CharacterBody3D>() : nullptr;

    m_cameraArm = node->findFirstDescendantOfType<SpringArm3D>();
    if (m_cameraArm != nullptr) {
        m_cameraArm->applyLength();
    }

    setViewCamera(node->findFirstDescendantOfType<Camera3D>());
    if (viewCamera() == nullptr) {
        RP_CORE_WARN("puppet '{}' holds no camera, so it is driven unseen", node->name());
    }
}

void PlayerController::onUpdate(float dt)
{
    Node3D *subject = m_possessed != nullptr ? m_possessed->as<Node3D>() : nullptr;
    if (subject == nullptr) {
        return;
    }

    m_yaw += m_intent.look.x * mouseSensitivity;
    m_pitch = glm::clamp(m_pitch - m_intent.look.y * mouseSensitivity, -maxPitch, maxPitch);

    float yaw = glm::radians(m_yaw);
    glm::vec3 forward = glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
    glm::vec3 right = glm::cross(forward, WORLD_UP);

    const glm::vec3 walk = (right * m_intent.move.x + forward * m_intent.move.z) * movementSpeed;
    if (m_body != nullptr) {
        m_body->setVelocity(walk);
        if (m_intent.jump && m_characterBody != nullptr) {
            m_characterBody->jump();
        }
    } else {
        subject->setPosition(subject->position() + walk * dt);
    }

    // a puppet faces down its own -Z, so the turn is the one taking -Z onto the walk direction
    subject->setRotation(glm::angleAxis(std::atan2(-forward.x, -forward.z), WORLD_UP));

    if (m_cameraArm != nullptr) {
        m_cameraArm->setRotation(glm::angleAxis(glm::radians(m_pitch), glm::vec3(1.0f, 0.0f, 0.0f)));
    }
}

void PlayerController::updateViewCamera()
{
    Camera3D *camera = viewCamera();
    if (camera == nullptr) {
        return;
    }

    if (!camera->accessor().has<CameraComponent>()) {
        return;
    }
    auto cameraComponent = camera->accessor().write<CameraComponent>();

    // the camera hangs under the arm, so where it ends up is only in its world transform
    glm::mat4 world = camera->worldTransform();
    glm::vec3 position = glm::vec3(world[3]);
    glm::vec3 front = glm::normalize(glm::vec3(world[2]) * -1.0f);

    cameraComponent->updateViewMatrix(position, front);
}

void PlayerController::serialize(WriteNode node) const
{
    Controller::serialize(node);

    node.set(KEY_MOVEMENT_SPEED, movementSpeed);
    node.set(KEY_MOUSE_SENSITIVITY, mouseSensitivity);
    node.set(KEY_MAX_PITCH, maxPitch);
}

void PlayerController::deserialize(ReadNode node)
{
    Controller::deserialize(node);

    movementSpeed = static_cast<float>(node.child(KEY_MOVEMENT_SPEED).asF64(movementSpeed));
    mouseSensitivity = static_cast<float>(node.child(KEY_MOUSE_SENSITIVITY).asF64(mouseSensitivity));
    maxPitch = static_cast<float>(node.child(KEY_MAX_PITCH).asF64(maxPitch));
}

} // namespace Rapture
