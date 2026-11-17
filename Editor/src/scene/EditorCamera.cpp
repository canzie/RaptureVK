#include "EditorCamera.h"

#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>

#include <glm/glm.hpp>

static constexpr glm::vec3 EDITOR_CAMERA_START = glm::vec3(0.0f, 0.0f, 5.0f);
static constexpr float EDITOR_CAMERA_FOV = 90.0f;
static constexpr float EDITOR_CAMERA_NEAR_PLANE = 0.1f;
static constexpr float EDITOR_CAMERA_FAR_PLANE = 200.0f;

EditorCamera::EditorCamera(Rapture::Scene &scene)
{
    // never parented, so it is not part of the scene it looks at and no save or snapshot restore can touch it
    m_camera = std::make_unique<Rapture::Camera3D>(scene, "Editor Camera");
    m_camera->setPosition(EDITOR_CAMERA_START);
    m_camera->setFieldOfView(EDITOR_CAMERA_FOV);
    m_camera->setNearPlane(EDITOR_CAMERA_NEAR_PLANE);
    m_camera->setFarPlane(EDITOR_CAMERA_FAR_PLANE);

    m_controller = std::make_unique<Rapture::CameraController>(scene, "Editor Camera Controller");
    // driven by the panel showing the viewport, with that panel's own intent, so it stays out of the scene's tick lists
    m_controller->setTickEnabled(false);
    m_controller->possess(m_camera.get());
}

EditorCamera::~EditorCamera() = default;
