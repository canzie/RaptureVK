#include "SceneRenderData.h"

#include "components/Components.h"
#include "events/AssetEvents.h"
#include "logging/TracyProfiler.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"

#include <cmath>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

struct SceneRenderData::SignalBridge {
    SceneRenderData *owner;

    void onMeshAdded(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onMeshAdded(static_cast<EntityID>(entity));
    }

    void onMeshRemoved(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onMeshRemoved(static_cast<EntityID>(entity));
    }

    void onLightAdded(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onLightAdded(static_cast<EntityID>(entity));
    }

    void onLightRemoved(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onLightRemoved(static_cast<EntityID>(entity));
    }

    void onCameraAdded(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onCameraAdded(static_cast<EntityID>(entity));
    }

    void onCameraRemoved(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onCameraRemoved(static_cast<EntityID>(entity));
    }

    void onShadowAdded(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onShadowAdded(static_cast<EntityID>(entity));
    }

    void onShadowRemoved(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onShadowRemoved(static_cast<EntityID>(entity));
    }

    void onCascadedShadowAdded(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onCascadedShadowAdded(static_cast<EntityID>(entity));
    }

    void onCascadedShadowRemoved(entt::registry &registry, entt::entity entity)
    {
        (void)registry;
        owner->onCascadedShadowRemoved(static_cast<EntityID>(entity));
    }
};

SceneRenderData::SceneRenderData(const RenderContext &renderContext, Scene &scene, uint32_t frameCount)
    : m_renderContext(renderContext), m_scene(&scene), m_frameCount(frameCount)
{
    m_meshes.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::MESH_DATA_SSBO);
    m_lights.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::LIGHT_DATA_SSBO);
    m_cameras.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::CAMERA_DATA_SSBO);
    m_shadows.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::SHADOW_DATA_SSBO);

    auto meshSwapCb = [this](EntityID entityId, uint32_t newSlot) {
        Entity entity(entityId, m_scene);
        auto *mesh = entity.tryGetComponent<MeshComponent>();
        if (mesh != nullptr) {
            mesh->renderDataSlot = m_meshes.getGlobalSlot(mesh->mobility, newSlot);
        }
    };
    auto lightSwapCb = [this](EntityID entityId, uint32_t newSlot) {
        Entity entity(entityId, m_scene);
        auto *light = entity.tryGetComponent<LightComponent>();
        if (light != nullptr) {
            light->renderDataSlot = m_lights.getGlobalSlot(light->mobility, newSlot);
        }
    };
    auto cameraSwapCb = [this](EntityID entityId, uint32_t newSlot) {
        Entity entity(entityId, m_scene);
        auto *camera = entity.tryGetComponent<CameraComponent>();
        if (camera != nullptr) {
            camera->renderDataSlot = m_cameras.getGlobalSlot(MOBILITY_DYNAMIC, newSlot);
        }
    };
    auto shadowSwapCb = [this](EntityID entityId, uint32_t newSlot) {
        Entity entity(entityId, m_scene);
        auto *shadow = entity.tryGetComponent<ShadowComponent>();
        if (shadow != nullptr) {
            shadow->renderDataSlot = m_shadows.getGlobalSlot(shadow->mobility, newSlot);
            return;
        }
        auto *cascaded = entity.tryGetComponent<CascadedShadowComponent>();
        if (cascaded != nullptr) {
            cascaded->renderDataSlot = m_shadows.getGlobalSlot(cascaded->mobility, newSlot);
        }
    };

    for (int i = 0; i < MOBILITY_COUNT; i++) {
        m_meshes.getPartition(static_cast<Mobility>(i)).init(frameCount, meshSwapCb);
        m_lights.getPartition(static_cast<Mobility>(i)).init(frameCount, lightSwapCb);
        m_cameras.getPartition(static_cast<Mobility>(i)).init(frameCount, cameraSwapCb);
        m_shadows.getPartition(static_cast<Mobility>(i)).init(frameCount, shadowSwapCb);
    }

    m_signalBridge = std::make_unique<SignalBridge>();
    m_signalBridge->owner = this;

    auto &registry = m_scene->getRegistry();
    registry.on_construct<MeshComponent>().connect<&SignalBridge::onMeshAdded>(m_signalBridge.get());
    registry.on_destroy<MeshComponent>().connect<&SignalBridge::onMeshRemoved>(m_signalBridge.get());
    registry.on_construct<LightComponent>().connect<&SignalBridge::onLightAdded>(m_signalBridge.get());
    registry.on_destroy<LightComponent>().connect<&SignalBridge::onLightRemoved>(m_signalBridge.get());
    registry.on_construct<CameraComponent>().connect<&SignalBridge::onCameraAdded>(m_signalBridge.get());
    registry.on_destroy<CameraComponent>().connect<&SignalBridge::onCameraRemoved>(m_signalBridge.get());
    registry.on_construct<ShadowComponent>().connect<&SignalBridge::onShadowAdded>(m_signalBridge.get());
    registry.on_destroy<ShadowComponent>().connect<&SignalBridge::onShadowRemoved>(m_signalBridge.get());
    registry.on_construct<CascadedShadowComponent>().connect<&SignalBridge::onCascadedShadowAdded>(m_signalBridge.get());
    registry.on_destroy<CascadedShadowComponent>().connect<&SignalBridge::onCascadedShadowRemoved>(m_signalBridge.get());

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

    auto existingShadows = registry.view<ShadowComponent>();
    for (auto entity : existingShadows) {
        onShadowAdded(static_cast<EntityID>(entity));
    }

    auto existingCascadedShadows = registry.view<CascadedShadowComponent>();
    for (auto entity : existingCascadedShadows) {
        onCascadedShadowAdded(static_cast<EntityID>(entity));
    }
}

SceneRenderData::~SceneRenderData()
{
    if (m_scene) {
        auto &registry = m_scene->getRegistry();
        registry.on_construct<MeshComponent>().disconnect<&SignalBridge::onMeshAdded>(m_signalBridge.get());
        registry.on_destroy<MeshComponent>().disconnect<&SignalBridge::onMeshRemoved>(m_signalBridge.get());
        registry.on_construct<LightComponent>().disconnect<&SignalBridge::onLightAdded>(m_signalBridge.get());
        registry.on_destroy<LightComponent>().disconnect<&SignalBridge::onLightRemoved>(m_signalBridge.get());
        registry.on_construct<CameraComponent>().disconnect<&SignalBridge::onCameraAdded>(m_signalBridge.get());
        registry.on_destroy<CameraComponent>().disconnect<&SignalBridge::onCameraRemoved>(m_signalBridge.get());
        registry.on_construct<ShadowComponent>().disconnect<&SignalBridge::onShadowAdded>(m_signalBridge.get());
        registry.on_destroy<ShadowComponent>().disconnect<&SignalBridge::onShadowRemoved>(m_signalBridge.get());
        registry.on_construct<CascadedShadowComponent>().disconnect<&SignalBridge::onCascadedShadowAdded>(m_signalBridge.get());
        registry.on_destroy<CascadedShadowComponent>().disconnect<&SignalBridge::onCascadedShadowRemoved>(m_signalBridge.get());
    }
}

void SceneRenderData::onMeshAdded(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *mesh = entity.tryGetComponent<MeshComponent>();
    if (mesh == nullptr) {
        return;
    }
    uint32_t localSlot = m_meshes.getPartition(mesh->mobility).allocateSlot(entityId);
    mesh->renderDataSlot = m_meshes.getGlobalSlot(mesh->mobility, localSlot);
}

void SceneRenderData::onMeshRemoved(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *mesh = entity.tryGetComponent<MeshComponent>();
    if (mesh == nullptr || mesh->renderDataSlot == UINT32_MAX) {
        return;
    }
    uint32_t localSlot = m_meshes.getLocalSlot(mesh->mobility, mesh->renderDataSlot);
    m_meshes.getPartition(mesh->mobility).freeSlot(localSlot);
    mesh->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onLightAdded(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *light = entity.tryGetComponent<LightComponent>();
    if (light == nullptr) {
        return;
    }
    uint32_t localSlot = m_lights.getPartition(light->mobility).allocateSlot(entityId);
    light->renderDataSlot = m_lights.getGlobalSlot(light->mobility, localSlot);
}

void SceneRenderData::onLightRemoved(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *light = entity.tryGetComponent<LightComponent>();
    if (light == nullptr || light->renderDataSlot == UINT32_MAX) {
        return;
    }
    uint32_t localSlot = m_lights.getLocalSlot(light->mobility, light->renderDataSlot);
    m_lights.getPartition(light->mobility).freeSlot(localSlot);
    light->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onCameraAdded(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *camera = entity.tryGetComponent<CameraComponent>();
    if (camera == nullptr) {
        return;
    }
    uint32_t localSlot = m_cameras.getPartition(MOBILITY_DYNAMIC).allocateSlot(entityId);
    camera->renderDataSlot = m_cameras.getGlobalSlot(MOBILITY_DYNAMIC, localSlot);
}

void SceneRenderData::onCameraRemoved(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *camera = entity.tryGetComponent<CameraComponent>();
    if (camera == nullptr || camera->renderDataSlot == UINT32_MAX) {
        return;
    }
    uint32_t localSlot = m_cameras.getLocalSlot(MOBILITY_DYNAMIC, camera->renderDataSlot);
    m_cameras.getPartition(MOBILITY_DYNAMIC).freeSlot(localSlot);
    camera->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onShadowAdded(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *shadow = entity.tryGetComponent<ShadowComponent>();
    if (shadow == nullptr) {
        return;
    }
    uint32_t localSlot = m_shadows.getPartition(shadow->mobility).allocateSlot(entityId);
    shadow->renderDataSlot = m_shadows.getGlobalSlot(shadow->mobility, localSlot);
}

void SceneRenderData::onShadowRemoved(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *shadow = entity.tryGetComponent<ShadowComponent>();
    if (shadow == nullptr || shadow->renderDataSlot == UINT32_MAX) {
        return;
    }
    uint32_t localSlot = m_shadows.getLocalSlot(shadow->mobility, shadow->renderDataSlot);
    m_shadows.getPartition(shadow->mobility).freeSlot(localSlot);
    shadow->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onCascadedShadowAdded(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *shadow = entity.tryGetComponent<CascadedShadowComponent>();
    if (shadow == nullptr) {
        return;
    }
    uint32_t localSlot = m_shadows.getPartition(shadow->mobility).allocateSlot(entityId);
    shadow->renderDataSlot = m_shadows.getGlobalSlot(shadow->mobility, localSlot);
}

void SceneRenderData::onCascadedShadowRemoved(EntityID entityId)
{
    Entity entity(entityId, m_scene);
    auto *shadow = entity.tryGetComponent<CascadedShadowComponent>();
    if (shadow == nullptr || shadow->renderDataSlot == UINT32_MAX) {
        return;
    }
    uint32_t localSlot = m_shadows.getLocalSlot(shadow->mobility, shadow->renderDataSlot);
    m_shadows.getPartition(shadow->mobility).freeSlot(localSlot);
    shadow->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onUpdate(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::onUpdate");

    updateMeshes(frameIndex);
    updateLights(frameIndex);
    updateCameras(frameIndex);
    updateShadows(frameIndex);

    m_meshes.upload(frameIndex);
    m_lights.upload(frameIndex);
    m_cameras.upload(frameIndex);
    m_shadows.upload(frameIndex);
}

void SceneRenderData::updateMeshes(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::updateMeshes");

    auto &staticPartition = m_meshes.getPartition(MOBILITY_STATIC);
    if (staticPartition.hasDirty(frameIndex)) {
        staticPartition.forEachDirty(frameIndex, [&](uint32_t i) {
            Entity entity(staticPartition.getEntityId(i), m_scene);
            auto [transform, mesh] = entity.tryGetComponents<TransformComponent, MeshComponent>();
            if (!transform || !mesh || !mesh->mesh) {
                return;
            }

            generation_t gen = transform->getGeneration();
            if (gen != staticPartition.getLastSeenGeneration(i)) {
                staticPartition.setLastSeenGeneration(i, gen);
                AssetEvents::onMeshTransformChanged().publish(entity.getID());
            }

            auto &data = staticPartition.getSlotData(i);
            data.modelMatrix = transform->transformMatrix();
            data.vertexBufferFlags = mesh->mesh->getVertexBuffer()->getBufferLayout().getFlags();
            data.entityId = entity.getID();
            data.materialIndex = 0;
        });
    }

    auto &dynamicPartition = m_meshes.getPartition(MOBILITY_DYNAMIC);
    for (uint32_t i = 0; i < dynamicPartition.getCount(); i++) {
        Entity entity(dynamicPartition.getEntityId(i), m_scene);
        auto [transform, mesh] = entity.tryGetComponents<TransformComponent, MeshComponent>();
        if (!transform || !mesh || !mesh->mesh) {
            continue;
        }

        generation_t gen = transform->getGeneration();
        if (gen != dynamicPartition.getLastSeenGeneration(i)) {
            dynamicPartition.setLastSeenGeneration(i, gen);
            AssetEvents::onMeshTransformChanged().publish(entity.getID());
        }

        auto &data = dynamicPartition.getSlotData(i);
        data.modelMatrix = transform->transformMatrix();
        data.vertexBufferFlags = mesh->mesh->getVertexBuffer()->getBufferLayout().getFlags();
        data.entityId = entity.getID();
        data.materialIndex = 0;
    }
}

void SceneRenderData::updateLights(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::updateLights");

    auto packLight = [&](RenderPartition<LightGPUData> &partition, uint32_t i) {
        Entity entity(partition.getEntityId(i), m_scene);
        auto [transform, light] = entity.tryGetComponents<TransformComponent, LightComponent>();
        if (!transform || !light) {
            return;
        }

        auto &data = partition.getSlotData(i);

        glm::vec3 position = transform->translation();
        if (light->type == LightType::Directional) {
            position = glm::vec3(0.0f);
        }
        data.positionAndType = glm::vec4(position, static_cast<float>(light->type));

        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
        if (light->type == LightType::Directional || light->type == LightType::Spot) {
            glm::quat rotationQuat = transform->transforms.getRotationQuat();
            direction = glm::normalize(rotationQuat * glm::vec3(0, 0, -1));
        }
        data.directionAndRange = glm::vec4(direction, light->range);

        data.colorAndIntensity = glm::vec4(light->color, light->intensity);

        float innerCos = 0.0f;
        float outerCos = 0.0f;
        if (light->type == LightType::Spot) {
            innerCos = std::cos(light->innerConeAngle);
            outerCos = std::cos(light->outerConeAngle);
        }
        data.spotAngles = glm::vec4(innerCos, outerCos, static_cast<float>(entity.getID()), 0.0f);
    };

    auto &staticPartition = m_lights.getPartition(MOBILITY_STATIC);
    if (staticPartition.hasDirty(frameIndex)) {
        staticPartition.forEachDirty(frameIndex, [&](uint32_t i) { packLight(staticPartition, i); });
    }

    auto &dynamicPartition = m_lights.getPartition(MOBILITY_DYNAMIC);
    for (uint32_t i = 0; i < dynamicPartition.getCount(); i++) {
        packLight(dynamicPartition, i);
    }
}

void SceneRenderData::updateCameras(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::updateCameras");
    (void)frameIndex;

    auto &partition = m_cameras.getPartition(MOBILITY_DYNAMIC);
    for (uint32_t i = 0; i < partition.getCount(); i++) {
        Entity entity(partition.getEntityId(i), m_scene);
        auto [transform, camera] = entity.tryGetComponents<TransformComponent, CameraComponent>();
        if (!transform || !camera) {
            continue;
        }

        auto &data = partition.getSlotData(i);

        data.view = camera->camera.getViewMatrix();
        data.projection = camera->camera.getProjectionMatrix();
        data.projection[1][1] *= -1;
    }
}

void SceneRenderData::updateShadows(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::updateShadows");

    auto packShadow = [&](RenderPartition<ShadowGPUData> &partition, uint32_t i) {
        Entity entity(partition.getEntityId(i), m_scene);

        auto *light = entity.tryGetComponent<LightComponent>();
        if (light == nullptr) {
            return;
        }

        auto &data = partition.getSlotData(i);

        auto *shadow = entity.tryGetComponent<ShadowComponent>();
        if (shadow != nullptr && shadow->shadowMap && shadow->isActive) {
            data.type = static_cast<int>(light->type);
            data.cascadeCount = 1;
            data.lightIndex = entity.getID();
            data.textureHandle = shadow->shadowMap->getTextureHandle();
            data.cascadeMatrices[0] = shadow->shadowMap->getLightViewProjection();
            data.cascadeSplitsViewSpace[0] = glm::vec4(0.0f);
            return;
        }

        auto *cascaded = entity.tryGetComponent<CascadedShadowComponent>();
        if (cascaded != nullptr && cascaded->cascadedShadowMap && cascaded->isActive) {
            data.type = static_cast<int>(light->type);
            data.cascadeCount = cascaded->cascadedShadowMap->getNumCascades();
            data.lightIndex = entity.getID();
            data.textureHandle = cascaded->cascadedShadowMap->getTextureHandle();

            auto splits = cascaded->cascadedShadowMap->getCascadeSplits();
            for (uint8_t c = 0; c < cascaded->cascadedShadowMap->getNumCascades(); c++) {
                data.cascadeMatrices[c] = cascaded->cascadedShadowMap->getLightViewProjections()[c];
                data.cascadeSplitsViewSpace[c] = glm::vec4(splits[c], splits[c + 1], 0.0f, -1.0f);
            }
        }
    };

    auto &dynamicPartition = m_shadows.getPartition(MOBILITY_DYNAMIC);
    for (uint32_t i = 0; i < dynamicPartition.getCount(); i++) {
        packShadow(dynamicPartition, i);
    }

    auto &staticPartition = m_shadows.getPartition(MOBILITY_STATIC);
    if (staticPartition.hasDirty(frameIndex)) {
        staticPartition.forEachDirty(frameIndex, [&](uint32_t i) { packShadow(staticPartition, i); });
    }
}

} // namespace Rapture
