#include "SceneRenderData.h"

#include "components/Components.h"
#include "scenes/Scene.h"

#include <cmath>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

struct SceneRenderData::SignalBridge {
    SceneRenderData* owner;

    void onMeshAdded(entt::registry&, entt::entity entity)
    {
        owner->onMeshAdded(static_cast<EntityID>(entity));
    }

    void onMeshRemoved(entt::registry&, entt::entity entity)
    {
        owner->onMeshRemoved(static_cast<EntityID>(entity));
    }

    void onLightAdded(entt::registry&, entt::entity entity)
    {
        owner->onLightAdded(static_cast<EntityID>(entity));
    }

    void onLightRemoved(entt::registry&, entt::entity entity)
    {
        owner->onLightRemoved(static_cast<EntityID>(entity));
    }

    void onCameraAdded(entt::registry&, entt::entity entity)
    {
        owner->onCameraAdded(static_cast<EntityID>(entity));
    }

    void onCameraRemoved(entt::registry&, entt::entity entity)
    {
        owner->onCameraRemoved(static_cast<EntityID>(entity));
    }
};

SceneRenderData::SceneRenderData(RenderContext* renderContext, Scene& scene, uint32_t frameCount)
    : m_scene(&scene), m_frameCount(frameCount)
{
    m_meshes.init(frameCount, renderContext);
    m_lights.init(frameCount, renderContext);
    m_cameras.init(frameCount, renderContext);

    m_signalBridge = std::make_unique<SignalBridge>();
    m_signalBridge->owner = this;

    auto& registry = m_scene->getRegistry();
    registry.on_construct<MeshComponent>().connect<&SignalBridge::onMeshAdded>(m_signalBridge.get());
    registry.on_destroy<MeshComponent>().connect<&SignalBridge::onMeshRemoved>(m_signalBridge.get());
    registry.on_construct<LightComponent>().connect<&SignalBridge::onLightAdded>(m_signalBridge.get());
    registry.on_destroy<LightComponent>().connect<&SignalBridge::onLightRemoved>(m_signalBridge.get());
    registry.on_construct<CameraComponent>().connect<&SignalBridge::onCameraAdded>(m_signalBridge.get());
    registry.on_destroy<CameraComponent>().connect<&SignalBridge::onCameraRemoved>(m_signalBridge.get());

    auto existingMeshes = registry.view<MeshComponent>();
    for (auto entity : existingMeshes) {
        onMeshAdded(static_cast<EntityID>(entity));
    }

    auto existingLights = registry.view<LightComponent>();
    for (auto entity : existingLights) {
        onLightAdded(static_cast<EntityID>(entity));
    }

    auto existingCameras = registry.view<CameraComponent>();
    for (auto entity : existingCameras) {
        onCameraAdded(static_cast<EntityID>(entity));
    }
}

SceneRenderData::~SceneRenderData()
{
    if (m_scene) {
        auto& registry = m_scene->getRegistry();
        registry.on_construct<MeshComponent>().disconnect<&SignalBridge::onMeshAdded>(m_signalBridge.get());
        registry.on_destroy<MeshComponent>().disconnect<&SignalBridge::onMeshRemoved>(m_signalBridge.get());
        registry.on_construct<LightComponent>().disconnect<&SignalBridge::onLightAdded>(m_signalBridge.get());
        registry.on_destroy<LightComponent>().disconnect<&SignalBridge::onLightRemoved>(m_signalBridge.get());
        registry.on_construct<CameraComponent>().disconnect<&SignalBridge::onCameraAdded>(m_signalBridge.get());
        registry.on_destroy<CameraComponent>().disconnect<&SignalBridge::onCameraRemoved>(m_signalBridge.get());
    }
}

void SceneRenderData::onMeshAdded(EntityID entityId)
{
    auto entity = static_cast<entt::entity>(entityId);
    auto& mesh = m_scene->getRegistry().get<MeshComponent>(entity);
    Mobility mobility = mesh.isStatic ? MOBILITY_STATIC : MOBILITY_DYNAMIC;
    m_meshes.allocateSlot(entityId, mobility);
}

void SceneRenderData::onMeshRemoved(EntityID entityId)
{
    m_meshes.freeSlot(entityId);
}

void SceneRenderData::onLightAdded(EntityID entityId)
{
    m_lights.allocateSlot(entityId, MOBILITY_DYNAMIC);
}

void SceneRenderData::onLightRemoved(EntityID entityId)
{
    m_lights.freeSlot(entityId);
}

void SceneRenderData::onCameraAdded(EntityID entityId)
{
    m_cameras.allocateSlot(entityId, MOBILITY_DYNAMIC);
}

void SceneRenderData::onCameraRemoved(EntityID entityId)
{
    m_cameras.freeSlot(entityId);
}

void SceneRenderData::onUpdate(uint32_t frameIndex)
{
    updateMeshes();
    updateLights();
    updateCameras();

    m_meshes.upload(frameIndex);
    m_lights.upload(frameIndex);
    m_cameras.upload(frameIndex);
}

void SceneRenderData::updateMeshes()
{
    auto& registry = m_scene->getRegistry();

    auto& staticPartition = m_meshes.getStaticPartition();
    for (uint32_t i = 0; i < staticPartition.getCount(); i++) {
        auto entity = static_cast<entt::entity>(staticPartition.getEntityId(i));
        auto* transform = registry.try_get<TransformComponent>(entity);
        auto* mesh = registry.try_get<MeshComponent>(entity);
        if (!transform || !mesh || !mesh->mesh) continue;

        generation_t gen = transform->getGeneration();
        if (gen == staticPartition.getLastSeenGeneration(i)) continue;
        staticPartition.setLastSeenGeneration(i, gen);

        auto& data = staticPartition.getSlotData(i);
        data.modelMatrix = transform->transformMatrix();
        data.vertexBufferFlags = mesh->mesh->getVertexBuffer()->getBufferLayout().getFlags();
        data.entityId = static_cast<uint32_t>(entity);
        data.materialIndex = 0;
        data._pad = 0;

        staticPartition.markDirtyAllFrames(i);
    }

    auto& dynamicPartition = m_meshes.getDynamicPartition();
    for (uint32_t i = 0; i < dynamicPartition.getCount(); i++) {
        auto entity = static_cast<entt::entity>(dynamicPartition.getEntityId(i));
        auto* transform = registry.try_get<TransformComponent>(entity);
        auto* mesh = registry.try_get<MeshComponent>(entity);
        if (!transform || !mesh || !mesh->mesh) continue;

        auto& data = dynamicPartition.getSlotData(i);
        data.modelMatrix = transform->transformMatrix();
        data.vertexBufferFlags = mesh->mesh->getVertexBuffer()->getBufferLayout().getFlags();
        data.entityId = static_cast<uint32_t>(entity);
        data.materialIndex = 0;
        data._pad = 0;
    }
}

void SceneRenderData::updateLights()
{
    auto& registry = m_scene->getRegistry();
    auto& partition = m_lights.getDynamicPartition();

    for (uint32_t i = 0; i < partition.getCount(); i++) {
        auto entity = static_cast<entt::entity>(partition.getEntityId(i));
        auto* transform = registry.try_get<TransformComponent>(entity);
        auto* light = registry.try_get<LightComponent>(entity);
        if (!transform || !light) continue;

        generation_t combinedGen = transform->getGeneration() + light->getGeneration();
        if (combinedGen == partition.getLastSeenGeneration(i)) continue;
        partition.setLastSeenGeneration(i, combinedGen);

        auto& data = partition.getSlotData(i);

        glm::vec3 position = transform->translation();
        if (light->type == LightType::Directional) {
            position = glm::vec3(0.0f);
        }
        data.positionAndRange = glm::vec4(position, light->range);

        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
        if (light->type == LightType::Directional || light->type == LightType::Spot) {
            glm::quat rotationQuat = transform->transforms.getRotationQuat();
            direction = glm::normalize(rotationQuat * glm::vec3(0, 0, -1));
        }
        data.directionAndType = glm::vec4(direction, static_cast<float>(light->type));

        data.colorAndIntensity = glm::vec4(light->color, light->intensity);

        if (light->type == LightType::Spot) {
            data.innerConeCos = std::cos(light->innerConeAngle);
            data.outerConeCos = std::cos(light->outerConeAngle);
        } else {
            data.innerConeCos = 0.0f;
            data.outerConeCos = 0.0f;
        }

        data.entityId = static_cast<uint32_t>(entity);
        data._pad = 0;

        partition.markDirtyAllFrames(i);
    }
}

void SceneRenderData::updateCameras()
{
    auto& registry = m_scene->getRegistry();
    auto& partition = m_cameras.getDynamicPartition();

    for (uint32_t i = 0; i < partition.getCount(); i++) {
        auto entity = static_cast<entt::entity>(partition.getEntityId(i));
        auto* transform = registry.try_get<TransformComponent>(entity);
        auto* camera = registry.try_get<CameraComponent>(entity);
        if (!transform || !camera) continue;

        auto& data = partition.getSlotData(i);

        data.view = camera->camera.getViewMatrix();
        data.projection = camera->camera.getProjectionMatrix();
        data.projection[1][1] *= -1;
        data.viewProjection = data.projection * data.view;

        data.positionAndNear = glm::vec4(transform->translation(), camera->nearPlane);

        glm::vec3 eulerAngles = transform->rotation();
        glm::vec3 front;
        front.x = std::cos(glm::radians(eulerAngles.y)) * std::cos(glm::radians(eulerAngles.x));
        front.y = std::sin(glm::radians(eulerAngles.x));
        front.z = std::sin(glm::radians(eulerAngles.y)) * std::cos(glm::radians(eulerAngles.x));
        data.forwardAndFar = glm::vec4(glm::normalize(front), camera->farPlane);
    }
}

} // namespace Rapture
