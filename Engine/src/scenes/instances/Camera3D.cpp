#include "Camera3D.h"

#include "components/Components.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_CAMERA = "camera";
static constexpr std::string_view KEY_FIELD_OF_VIEW = "fieldOfView";
static constexpr std::string_view KEY_NEAR_PLANE = "nearPlane";
static constexpr std::string_view KEY_FAR_PLANE = "farPlane";

Camera3D::Camera3D(Scene &scene, std::string_view name) : Node3D(scene, name)
{
    m_entity.set<CameraComponent>();
}

const TypeInfo &Camera3D::staticType()
{
    static const TypeInfo type("Camera3D", &Node3D::staticType());
    return type;
}

const TypeInfo &Camera3D::type() const
{
    return staticType();
}

float Camera3D::fieldOfView() const
{
    const auto *camera = m_entity.tryRead<CameraComponent>();
    return camera != nullptr ? camera->fov : 0.0f;
}

void Camera3D::setFieldOfView(float degrees)
{
    if (!m_entity.has<CameraComponent>()) {
        return;
    }
    auto camera = m_entity.write<CameraComponent>();

    camera->updateProjectionMatrix(degrees, camera->aspectRatio, camera->nearPlane, camera->farPlane);
}

float Camera3D::nearPlane() const
{
    const auto *camera = m_entity.tryRead<CameraComponent>();
    return camera != nullptr ? camera->nearPlane : 0.0f;
}

void Camera3D::setNearPlane(float nearPlane)
{
    if (!m_entity.has<CameraComponent>()) {
        return;
    }
    auto camera = m_entity.write<CameraComponent>();

    camera->updateProjectionMatrix(camera->fov, camera->aspectRatio, nearPlane, camera->farPlane);
}

float Camera3D::farPlane() const
{
    const auto *camera = m_entity.tryRead<CameraComponent>();
    return camera != nullptr ? camera->farPlane : 0.0f;
}

void Camera3D::setFarPlane(float farPlane)
{
    if (!m_entity.has<CameraComponent>()) {
        return;
    }
    auto camera = m_entity.write<CameraComponent>();

    camera->updateProjectionMatrix(camera->fov, camera->aspectRatio, camera->nearPlane, farPlane);
}

void Camera3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode camera = node.addObject(KEY_CAMERA);
    camera.set(KEY_FIELD_OF_VIEW, fieldOfView());
    camera.set(KEY_NEAR_PLANE, nearPlane());
    camera.set(KEY_FAR_PLANE, farPlane());
}

void Camera3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode camera = node.child(KEY_CAMERA);
    if (!camera.valid()) {
        return;
    }

    setFieldOfView(static_cast<float>(camera.child(KEY_FIELD_OF_VIEW).asF64(fieldOfView())));
    setNearPlane(static_cast<float>(camera.child(KEY_NEAR_PLANE).asF64(nearPlane())));
    setFarPlane(static_cast<float>(camera.child(KEY_FAR_PLANE).asF64(farPlane())));
}

} // namespace Rapture
