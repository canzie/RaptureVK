#include "PlayerController.h"

#include "scene/components/Components.h"
#include "core/utils/Log.h"
#include "scene/instances/Camera3D.h"
#include "scene/instances/CharacterBody3D.h"
#include "scene/instances/Node3D.h"
#include "scene/instances/PhysicsBody3D.h"
#include "scene/instances/SpringArm3D.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Rapture {

static const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

static constexpr std::string_view KEY_MOVEMENT_SPEED = "movementSpeed";
static constexpr std::string_view KEY_MOUSE_SENSITIVITY = "mouseSensitivity";
static constexpr std::string_view KEY_MAX_PITCH = "maxPitch";

PlayerController::PlayerController(Scene &scene, std::string_view name) : Controller(scene, name)
{
    setCapturesCursor(true);
}

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
        m_cameraArm->setFollowsControlRotation(true);
    }

    setViewCamera(node->findFirstDescendantOfType<Camera3D>());
    if (viewCamera() == nullptr) {
        RP_CORE_WARN("puppet '{}' holds no camera, so it is driven unseen", node->name());
    }
}

void PlayerController::onUpdate(float dt)
{
    Controller::onUpdate(dt);
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
    node.set(KEY_MAX_PITCH, maxPitch());
}

void PlayerController::deserialize(ReadNode node)
{
    Controller::deserialize(node);

    movementSpeed = static_cast<float>(node.child(KEY_MOVEMENT_SPEED).asF64(movementSpeed));
    mouseSensitivity = static_cast<float>(node.child(KEY_MOUSE_SENSITIVITY).asF64(mouseSensitivity));
    setMaxPitch(static_cast<float>(node.child(KEY_MAX_PITCH).asF64(maxPitch())));
}

} // namespace Rapture
