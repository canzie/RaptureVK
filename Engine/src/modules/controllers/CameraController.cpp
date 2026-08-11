#include "CameraController.h"

#include "components/Components.h"
#include "components/systems/Transforms.h"
#include "logging/Log.h"
#include "scenes/instances/Camera3D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Rapture {

static const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

static constexpr std::string_view KEY_MOUSE_SENSITIVITY = "mouseSensitivity";
static constexpr std::string_view KEY_MOVEMENT_SPEED = "movementSpeed";
static constexpr std::string_view KEY_ORBIT_SENSITIVITY = "orbitSensitivity";
static constexpr std::string_view KEY_PAN_SPEED = "panSpeed";
static constexpr std::string_view KEY_ZOOM_SPEED = "zoomSpeed";
static constexpr std::string_view KEY_MAX_PITCH = "maxPitch";

const TypeInfo &CameraController::staticType()
{
    static const TypeInfo type("CameraController", &Controller::staticType());
    return type;
}

const TypeInfo &CameraController::type() const
{
    return staticType();
}

void CameraController::possess(Instance *subject)
{
    Camera3D *camera = subject != nullptr ? subject->as<Camera3D>() : nullptr;
    if (camera == nullptr) {
        RP_CORE_WARN("a camera controller only drives a Camera3D");
        return;
    }

    Controller::possess(camera);
    setViewCamera(camera);
    recalcFront();
}

void CameraController::setMode(CameraControlMode mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    if (mode == CameraControlMode::ORBIT) {
        m_recenterFocus = true;
    }
}

void CameraController::update(float dt, const ControlInput &input)
{
    if (m_viewCamera == nullptr) {
        return;
    }

    if (!m_viewCamera->accessor().has<CameraComponent>()) {
        return;
    }
    auto camera = m_viewCamera->accessor().write<CameraComponent>();

    if (input.releaseControl) {
        setMode(CameraControlMode::ORBIT);
    }

    if (m_mode == CameraControlMode::FLY) {
        updateFly(dt, input, *m_viewCamera);
    } else {
        updateOrbit(input, *m_viewCamera);
    }

    camera->updateViewMatrix(transform::translation(m_viewCamera->worldTransform()), m_front);
}

void CameraController::updateFly(float dt, const ControlInput &input, Node3D &node)
{
    m_desiresCapture = true;

    m_yaw += input.look.x * mouseSensitivity;
    m_pitch -= input.look.y * mouseSensitivity;
    m_pitch = glm::clamp(m_pitch, -maxPitch, maxPitch);
    recalcFront();

    glm::vec3 right = glm::normalize(glm::cross(m_front, WORLD_UP));
    float distance = movementSpeed * dt;

    glm::vec3 position = node.position();
    position += right * input.move.x * distance;
    position += WORLD_UP * input.move.y * distance;
    position += m_front * input.move.z * distance;
    node.setPosition(position);
}

void CameraController::updateOrbit(const ControlInput &input, Node3D &node)
{
    if (m_recenterFocus) {
        m_focusPoint = node.position() + m_front * m_focusDistance;
        m_recenterFocus = false;
    }

    m_desiresCapture = input.orbit;

    if (input.orbit) {
        glm::vec3 right = glm::normalize(glm::cross(m_front, WORLD_UP));
        glm::vec3 up = glm::normalize(glm::cross(right, m_front));
        if (input.pan) {
            float scale = panSpeed * m_focusDistance;
            m_focusPoint += (-right * input.look.x + up * input.look.y) * scale;
        } else {
            m_yaw += input.look.x * orbitSensitivity;
            m_pitch -= input.look.y * orbitSensitivity;
            m_pitch = glm::clamp(m_pitch, -maxPitch, maxPitch);
            recalcFront();
        }
    }

    if (input.zoom != 0.0f) {
        m_focusDistance *= (1.0f - input.zoom * zoomSpeed);
        if (m_focusDistance < 0.1f) {
            m_focusDistance = 0.1f;
        }
    }

    node.setPosition(m_focusPoint - m_front * m_focusDistance);
}

void CameraController::serialize(WriteNode node) const
{
    Controller::serialize(node);

    node.set(KEY_MOUSE_SENSITIVITY, mouseSensitivity);
    node.set(KEY_MOVEMENT_SPEED, movementSpeed);
    node.set(KEY_ORBIT_SENSITIVITY, orbitSensitivity);
    node.set(KEY_PAN_SPEED, panSpeed);
    node.set(KEY_ZOOM_SPEED, zoomSpeed);
    node.set(KEY_MAX_PITCH, maxPitch);
}

void CameraController::deserialize(ReadNode node)
{
    Controller::deserialize(node);

    mouseSensitivity = static_cast<float>(node.child(KEY_MOUSE_SENSITIVITY).asF64(mouseSensitivity));
    movementSpeed = static_cast<float>(node.child(KEY_MOVEMENT_SPEED).asF64(movementSpeed));
    orbitSensitivity = static_cast<float>(node.child(KEY_ORBIT_SENSITIVITY).asF64(orbitSensitivity));
    panSpeed = static_cast<float>(node.child(KEY_PAN_SPEED).asF64(panSpeed));
    zoomSpeed = static_cast<float>(node.child(KEY_ZOOM_SPEED).asF64(zoomSpeed));
    maxPitch = static_cast<float>(node.child(KEY_MAX_PITCH).asF64(maxPitch));
}

void CameraController::recalcFront()
{
    glm::vec3 front;
    front.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
    front.y = std::sin(glm::radians(m_pitch));
    front.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
    m_front = glm::normalize(front);
}

} // namespace Rapture
