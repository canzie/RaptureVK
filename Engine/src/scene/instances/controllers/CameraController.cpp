#include "CameraController.h"

#include "scene/components/Components.h"
#include "scene/systems/Transforms.h"
#include "core/utils/Log.h"
#include "scene/instances/Camera3D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Rapture {

static const glm::vec3 WORLD_UP = glm::vec3(0.0f, 1.0f, 0.0f);

// leaves a margin around what is framed, so it does not sit edge to edge
static constexpr float FOCUS_FILL = 0.87f;

// what a sphere with no size is framed as, so focusing a point still yields a usable distance
static constexpr float FOCUS_MIN_RADIUS = 0.01f;

static constexpr std::string_view KEY_MOUSE_SENSITIVITY = "mouseSensitivity";
static constexpr std::string_view KEY_MOVEMENT_SPEED = "movementSpeed";
static constexpr std::string_view KEY_ORBIT_SENSITIVITY = "orbitSensitivity";
static constexpr std::string_view KEY_PAN_SPEED = "panSpeed";
static constexpr std::string_view KEY_ZOOM_SPEED = "zoomSpeed";
static constexpr std::string_view KEY_MAX_PITCH = "maxPitch";

CameraController::CameraController(Scene &scene, std::string_view name) : Controller(scene, name) {}

const TypeInfo &CameraController::staticType()
{
    static const TypeInfo type("CameraController", &Controller::staticType());
    return type;
}

const TypeInfo &CameraController::type() const
{
    return staticType();
}

void CameraController::possess(SceneObject *subject)
{
    Camera3D *camera = subject != nullptr ? subject->as<Camera3D>() : nullptr;
    if (camera == nullptr) {
        RP_CORE_WARN("a camera controller only drives a Camera3D");
        return;
    }

    Controller::possess(camera);
    setViewCamera(camera);
    recalcForward();
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

void CameraController::onUpdate(float dt)
{
    Controller::onUpdate(dt);

    if (m_viewCamera == nullptr) {
        return;
    }

    if (!m_viewCamera->accessor().has<CameraComponent>()) {
        return;
    }
    auto camera = m_viewCamera->accessor().write<CameraComponent>();

    if (m_intent.releaseControl) {
        setMode(CameraControlMode::ORBIT);
    }

    if (m_mode == CameraControlMode::FLY) {
        updateFly(dt, *m_viewCamera);
    } else {
        updateOrbit(*m_viewCamera);
    }

    camera->updateViewMatrix(transform::translation(m_viewCamera->worldTransform()), m_forward);
}

void CameraController::updateViewCamera()
{
    if (m_viewCamera == nullptr || !m_viewCamera->accessor().has<CameraComponent>()) {
        return;
    }

    // this controller turns itself rather than the object it sits on, so where it looks is its own
    // forward and not anything the transform carries
    m_viewCamera->accessor().write<CameraComponent>()->updateViewMatrix(
        transform::translation(m_viewCamera->worldTransform()), m_forward);
}

void CameraController::updateFly(float dt, Node3D &node)
{
    setCapturesCursor(true);

    addYawInput(m_intent.look.x * mouseSensitivity);
    addPitchInput(-m_intent.look.y * mouseSensitivity);
    recalcForward();

    glm::vec3 right = glm::normalize(glm::cross(m_forward, WORLD_UP));
    float distance = movementSpeed * dt;

    glm::vec3 position = node.position();
    position += right * m_intent.move.x * distance;
    position += WORLD_UP * m_intent.move.y * distance;
    position += m_forward * m_intent.move.z * distance;
    node.setPosition(position);
}

void CameraController::updateOrbit(Node3D &node)
{
    if (m_recenterFocus) {
        m_focusPoint = node.position() + m_forward * m_focusDistance;
        m_recenterFocus = false;
    }

    setCapturesCursor(m_intent.orbit);

    if (m_intent.orbit) {
        glm::vec3 right = glm::normalize(glm::cross(m_forward, WORLD_UP));
        glm::vec3 up = glm::normalize(glm::cross(right, m_forward));
        if (m_intent.pan) {
            float scale = panSpeed * m_focusDistance;
            m_focusPoint += (-right * m_intent.look.x + up * m_intent.look.y) * scale;
        } else {
            addYawInput(m_intent.look.x * orbitSensitivity);
            addPitchInput(-m_intent.look.y * orbitSensitivity);
            recalcForward();
        }
    }

    if (m_intent.zoom != 0.0f) {
        m_focusDistance *= (1.0f - m_intent.zoom * zoomSpeed);
        if (m_focusDistance < 0.1f) {
            m_focusDistance = 0.1f;
        }
    }

    node.setPosition(m_focusPoint - m_forward * m_focusDistance);
}

void CameraController::serialize(WriteNode node) const
{
    Controller::serialize(node);

    node.set(KEY_MOUSE_SENSITIVITY, mouseSensitivity);
    node.set(KEY_MOVEMENT_SPEED, movementSpeed);
    node.set(KEY_ORBIT_SENSITIVITY, orbitSensitivity);
    node.set(KEY_PAN_SPEED, panSpeed);
    node.set(KEY_ZOOM_SPEED, zoomSpeed);
    node.set(KEY_MAX_PITCH, maxPitch());
}

void CameraController::deserialize(ReadNode node)
{
    Controller::deserialize(node);

    mouseSensitivity = static_cast<float>(node.child(KEY_MOUSE_SENSITIVITY).asF64(mouseSensitivity));
    movementSpeed = static_cast<float>(node.child(KEY_MOVEMENT_SPEED).asF64(movementSpeed));
    orbitSensitivity = static_cast<float>(node.child(KEY_ORBIT_SENSITIVITY).asF64(orbitSensitivity));
    panSpeed = static_cast<float>(node.child(KEY_PAN_SPEED).asF64(panSpeed));
    zoomSpeed = static_cast<float>(node.child(KEY_ZOOM_SPEED).asF64(zoomSpeed));
    setMaxPitch(static_cast<float>(node.child(KEY_MAX_PITCH).asF64(maxPitch())));
}

void CameraController::focusOn(const glm::vec3 &center, float radius, const glm::vec3 &direction)
{
    if (glm::length(direction) > 0.0f) {
        glm::vec3 forward = glm::normalize(direction);
        setControlRotation(glm::degrees(std::atan2(forward.z, forward.x)), glm::degrees(std::asin(forward.y)));
        recalcForward();
    }

    focusOn(center, radius);
}

void CameraController::focusOn(const glm::vec3 &center, float radius)
{
    if (m_viewCamera == nullptr) {
        return;
    }

    const auto *camera = m_viewCamera->accessor().tryRead<CameraComponent>();
    if (camera == nullptr) {
        return;
    }

    float fitRadius = std::max(radius, FOCUS_MIN_RADIUS) / FOCUS_FILL;

    // the frustum plane is tangent to the sphere, so the perpendicular from the centre to it is the
    // radius, and that perpendicular measures distance * sin(half fov)
    float halfFovY = glm::radians(camera->fov) * 0.5f;
    float distance = fitRadius / std::sin(halfFovY);

    // a viewport taller than it is wide is tighter across than it is down
    if (camera->aspectRatio < 1.0f) {
        float halfFovX = std::atan(camera->aspectRatio * std::tan(halfFovY));
        distance = std::max(distance, fitRadius / std::sin(halfFovX));
    }

    distance = std::max(distance, camera->nearPlane + fitRadius);

    m_focusPoint = center;
    m_focusDistance = distance;
    m_recenterFocus = false;

    m_viewCamera->setPosition(center - m_forward * distance);
    updateViewCamera();
}

void CameraController::recalcForward()
{
    glm::vec3 forward;
    forward.x = std::cos(glm::radians(yaw())) * std::cos(glm::radians(pitch()));
    forward.y = std::sin(glm::radians(pitch()));
    forward.z = std::sin(glm::radians(yaw())) * std::cos(glm::radians(pitch()));
    m_forward = glm::normalize(forward);
}

} // namespace Rapture
