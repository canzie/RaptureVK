#include "EditorCamera.h"

#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/Mesh3D.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

static constexpr glm::vec3 EDITOR_CAMERA_START = glm::vec3(0.0f, 0.0f, 5.0f);
static constexpr float EDITOR_CAMERA_FOV = 90.0f;
static constexpr float EDITOR_CAMERA_NEAR_PLANE = 0.1f;
static constexpr float EDITOR_CAMERA_FAR_PLANE = 200.0f;
static constexpr float SMALLEST_FOCUS_RADIUS = 0.5f;
static constexpr uint32_t BOX_CORNER_COUNT = 8;

struct WorldBounds {
    glm::vec3 min;
    glm::vec3 max;
};

static WorldBounds s_worldBoundsOfMesh(const Rapture::Mesh3D &mesh)
{
    const glm::vec3 localMin = mesh.boundsMin();
    const glm::vec3 localMax = mesh.boundsMax();
    const glm::mat4 &world = mesh.worldTransform();

    WorldBounds bounds{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    for (uint32_t corner = 0; corner < BOX_CORNER_COUNT; corner++) {
        const glm::vec3 local((corner & 1) != 0 ? localMax.x : localMin.x, (corner & 2) != 0 ? localMax.y : localMin.y,
                              (corner & 4) != 0 ? localMax.z : localMin.z);
        const glm::vec3 placed = glm::vec3(world * glm::vec4(local, 1.0f));
        bounds.min = glm::min(bounds.min, placed);
        bounds.max = glm::max(bounds.max, placed);
    }
    return bounds;
}

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

void EditorCamera::focusOnObject(const Rapture::SceneObject &object)
{
    const auto *node = object.as<Rapture::Node3D>();
    if (node == nullptr) {
        return;
    }

    glm::vec3 center = node->worldPosition();
    float radius = SMALLEST_FOCUS_RADIUS;

    const auto *mesh = object.as<Rapture::Mesh3D>();
    if (mesh != nullptr && mesh->mesh() != Rapture::INVALID_ASSET_HANDLE) {
        const WorldBounds bounds = s_worldBoundsOfMesh(*mesh);
        center = (bounds.min + bounds.max) * 0.5f;
        radius = std::max(glm::length(bounds.max - bounds.min) * 0.5f, SMALLEST_FOCUS_RADIUS);
    }

    m_controller->focusOn(center, radius);
}
