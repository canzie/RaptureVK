#include "Camera3D.h"

#include "components/Components.h"
#include "scenes/Scene.h"

namespace Rapture {

Camera3D::Camera3D(Scene &scene, std::string_view name) : Node3D(scene, name)
{
    m_entity.setComponent<CameraComponent>();
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
    const auto *camera = m_entity.tryGetComponent<CameraComponent>();
    return camera != nullptr ? camera->fov : 0.0f;
}

void Camera3D::setFieldOfView(float degrees)
{
    auto *camera = m_entity.tryGetComponent<CameraComponent>();
    if (camera == nullptr) {
        return;
    }

    camera->updateProjectionMatrix(degrees, camera->aspectRatio, camera->nearPlane, camera->farPlane);
    m_entity.markDirty();
}

float Camera3D::nearPlane() const
{
    const auto *camera = m_entity.tryGetComponent<CameraComponent>();
    return camera != nullptr ? camera->nearPlane : 0.0f;
}

void Camera3D::setNearPlane(float nearPlane)
{
    auto *camera = m_entity.tryGetComponent<CameraComponent>();
    if (camera == nullptr) {
        return;
    }

    camera->updateProjectionMatrix(camera->fov, camera->aspectRatio, nearPlane, camera->farPlane);
    m_entity.markDirty();
}

float Camera3D::farPlane() const
{
    const auto *camera = m_entity.tryGetComponent<CameraComponent>();
    return camera != nullptr ? camera->farPlane : 0.0f;
}

void Camera3D::setFarPlane(float farPlane)
{
    auto *camera = m_entity.tryGetComponent<CameraComponent>();
    if (camera == nullptr) {
        return;
    }

    camera->updateProjectionMatrix(camera->fov, camera->aspectRatio, camera->nearPlane, farPlane);
    m_entity.markDirty();
}

void Camera3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode camera = node.addObject("camera");
    camera.set("fieldOfView", fieldOfView());
    camera.set("nearPlane", nearPlane());
    camera.set("farPlane", farPlane());
}

void Camera3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode camera = node.child("camera");
    if (!camera.valid()) {
        return;
    }

    setFieldOfView(static_cast<float>(camera.child("fieldOfView").asF64(fieldOfView())));
    setNearPlane(static_cast<float>(camera.child("nearPlane").asF64(nearPlane())));
    setFarPlane(static_cast<float>(camera.child("farPlane").asF64(farPlane())));
}

} // namespace Rapture
