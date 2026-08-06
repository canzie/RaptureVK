#include "CameraController.h"

#include "components/Components.h"
#include "scenes/instances/Camera3D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Rapture {

static const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

CameraController::CameraController(Camera3D &camera) : m_camera(camera)
{
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
    auto [transform, camera] = m_camera.entity().tryGetComponents<TransformComponent, CameraComponent>();
    if (transform == nullptr || camera == nullptr) {
        return;
    }

    if (input.releaseControl) {
        setMode(CameraControlMode::ORBIT);
    }

    if (m_mode == CameraControlMode::FLY) {
        updateFly(dt, input, *transform);
    } else {
        updateOrbit(input, *transform);
    }

    camera->updateViewMatrix(*transform, m_front);
}

void CameraController::updateFly(float dt, const ControlInput &input, TransformComponent &transform)
{
    m_desiresCapture = true;

    m_yaw += input.look.x * mouseSensitivity;
    m_pitch -= input.look.y * mouseSensitivity;
    m_pitch = glm::clamp(m_pitch, -maxPitch, maxPitch);
    recalcFront();

    glm::vec3 right = glm::normalize(glm::cross(m_front, WORLD_UP));
    float distance = movementSpeed * dt;

    glm::vec3 position = transform.translation();
    position += right * input.move.x * distance;
    position += WORLD_UP * input.move.y * distance;
    position += m_front * input.move.z * distance;
    transform.transforms.setTranslation(position);
}

void CameraController::updateOrbit(const ControlInput &input, TransformComponent &transform)
{
    if (m_recenterFocus) {
        m_focusPoint = transform.translation() + m_front * m_focusDistance;
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

    transform.transforms.setTranslation(m_focusPoint - m_front * m_focusDistance);
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
