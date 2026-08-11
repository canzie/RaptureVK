#include "SceneRenderData.h"

#include "components/Components.h"
#include "components/systems/Transforms.h"
#include "ecs/entity_accessor.h"
#include "logging/TracyProfiler.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"
#include "scenes/Scene.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace Rapture {

SceneRenderData::SceneRenderData(const RenderContext &renderContext, Scene &scene, uint32_t frameCount)
    : m_renderContext(renderContext), m_scene(&scene), m_frameCount(frameCount)
{
    m_meshes.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::MESH_DATA_SSBO);
    m_lights.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::LIGHT_DATA_SSBO);
    m_cameras.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::CAMERA_DATA_SSBO);
    m_shadows.init(frameCount, &m_renderContext, DescriptorSetBindingLocation::SHADOW_DATA_SSBO);

    // a bookmark belongs to a destination, and every frame in flight has its own buffer
    m_meshBookmarks.resize(frameCount);
    m_lightBookmarks.resize(frameCount);
    m_cameraBookmarks.resize(frameCount);
    m_shadowBookmarks.resize(frameCount);

    ecs::Registry &registry = m_scene->getRegistry();

    // a slot is bookkeeping about where the data went, not a change to the data, so it announces nothing
    auto meshSwapCb = [this, &registry](ecs::Entity entity, uint32_t newSlot) {
        if (!registry.has<MeshComponent>(entity)) {
            return;
        }
        auto mesh = registry.write<MeshComponent>(entity, 0);
        mesh->renderDataSlot = m_meshes.getGlobalSlot(mesh->mobility, newSlot);
    };
    auto lightSwapCb = [this, &registry](ecs::Entity entity, uint32_t newSlot) {
        LightComponent *light = Light_tryWriteLight(ecs::EntityAccessor(entity, &registry), 0);
        if (light != nullptr) {
            light->renderDataSlot = m_lights.getGlobalSlot(light->mobility, newSlot);
        }
    };
    auto cameraSwapCb = [this, &registry](ecs::Entity entity, uint32_t newSlot) {
        if (!registry.has<CameraComponent>(entity)) {
            return;
        }
        auto camera = registry.write<CameraComponent>(entity, 0);
        camera->renderDataSlot = m_cameras.getGlobalSlot(MOBILITY_DYNAMIC, newSlot);
    };
    auto shadowSwapCb = [this, &registry](ecs::Entity entity, uint32_t newSlot) {
        if (registry.has<ShadowComponent>(entity)) {
            auto shadow = registry.write<ShadowComponent>(entity, 0);
            shadow->renderDataSlot = m_shadows.getGlobalSlot(shadow->mobility, newSlot);
            return;
        }
        if (registry.has<CascadedShadowComponent>(entity)) {
            auto cascaded = registry.write<CascadedShadowComponent>(entity, 0);
            cascaded->renderDataSlot = m_shadows.getGlobalSlot(cascaded->mobility, newSlot);
        }
    };

    for (int i = 0; i < MOBILITY_COUNT; i++) {
        m_meshes.getPartition(static_cast<Mobility>(i)).init(frameCount, meshSwapCb);
        m_lights.getPartition(static_cast<Mobility>(i)).init(frameCount, lightSwapCb);
        m_cameras.getPartition(static_cast<Mobility>(i)).init(frameCount, cameraSwapCb);
        m_shadows.getPartition(static_cast<Mobility>(i)).init(frameCount, shadowSwapCb);
    }

    m_connections.push_back(registry.onConstructScoped<MeshComponent>([this](ecs::Entity entity) { onMeshAdded(entity); }));
    m_connections.push_back(registry.onDestroyScoped<MeshComponent>([this](ecs::Entity entity) { onMeshRemoved(entity); }));

    connectLightSignals<DirectionalLightComponent>(registry);
    connectLightSignals<PointLightComponent>(registry);
    connectLightSignals<SpotLightComponent>(registry);

    m_connections.push_back(registry.onConstructScoped<CameraComponent>([this](ecs::Entity entity) { onCameraAdded(entity); }));
    m_connections.push_back(registry.onDestroyScoped<CameraComponent>([this](ecs::Entity entity) { onCameraRemoved(entity); }));

    m_connections.push_back(registry.onConstructScoped<ShadowComponent>([this](ecs::Entity entity) {
        onShadowAdded(entity);
        createShadowMap(entity);
    }));
    m_connections.push_back(registry.onDestroyScoped<ShadowComponent>([this](ecs::Entity entity) {
        destroyShadowMap(entity);
        onShadowRemoved(entity);
    }));

    m_connections.push_back(registry.onConstructScoped<CascadedShadowComponent>([this](ecs::Entity entity) {
        onCascadedShadowAdded(entity);
        createCascadedShadowMap(entity);
    }));
    m_connections.push_back(registry.onDestroyScoped<CascadedShadowComponent>([this](ecs::Entity entity) {
        destroyCascadedShadowMap(entity);
        onCascadedShadowRemoved(entity);
    }));

    for (auto [entity, mesh] : registry.read<MeshComponent>()) {
        onMeshAdded(entity);
    }
    for (auto [entity, light] : registry.read<DirectionalLightComponent>()) {
        onLightAdded(entity);
    }
    for (auto [entity, light] : registry.read<PointLightComponent>()) {
        onLightAdded(entity);
    }
    for (auto [entity, light] : registry.read<SpotLightComponent>()) {
        onLightAdded(entity);
    }
    for (auto [entity, camera] : registry.read<CameraComponent>()) {
        onCameraAdded(entity);
    }
    for (auto [entity, shadow] : registry.read<ShadowComponent>()) {
        onShadowAdded(entity);
        createShadowMap(entity);
    }
    for (auto [entity, shadow] : registry.read<CascadedShadowComponent>()) {
        onCascadedShadowAdded(entity);
        createCascadedShadowMap(entity);
    }
}

template <typename T>
void SceneRenderData::connectLightSignals(ecs::Registry &registry)
{
    m_connections.push_back(registry.onConstructScoped<T>([this](ecs::Entity entity) { onLightAdded(entity); }));
    m_connections.push_back(registry.onDestroyScoped<T>([this](ecs::Entity entity) { onLightRemoved(entity); }));
}

SceneRenderData::~SceneRenderData() = default;

void SceneRenderData::onMeshAdded(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<MeshComponent>(entityId)) {
        return;
    }

    auto mesh = registry.write<MeshComponent>(entityId, 0);
    uint32_t localSlot = m_meshes.getPartition(mesh->mobility).allocateSlot(entityId);
    mesh->renderDataSlot = m_meshes.getGlobalSlot(mesh->mobility, localSlot);
}

void SceneRenderData::onMeshRemoved(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<MeshComponent>(entityId)) {
        return;
    }

    auto mesh = registry.write<MeshComponent>(entityId, 0);
    if (mesh->renderDataSlot == UINT32_MAX) {
        return;
    }

    uint32_t localSlot = m_meshes.getLocalSlot(mesh->mobility, mesh->renderDataSlot);
    m_meshes.getPartition(mesh->mobility).freeSlot(localSlot);
    mesh->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::setMeshMobility(EntityID entityId, Mobility mobility)
{
    ecs::Registry &registry = m_scene->getRegistry();
    const MeshComponent *mesh = registry.tryRead<MeshComponent>(entityId);
    if (mesh == nullptr || mesh->mobility == mobility) {
        return;
    }

    // The slot is freed against the old mobility and reallocated against the new one, so the
    // component's mobility is only changed between the two.
    onMeshRemoved(entityId);
    registry.write<MeshComponent>(entityId)->mobility = mobility;
    onMeshAdded(entityId);
}

void SceneRenderData::setLightMobility(EntityID entityId, Mobility mobility)
{
    ecs::EntityAccessor entity(entityId, &m_scene->getRegistry());
    const LightComponent *light = Light_tryReadLight(entity);
    if (light == nullptr || light->mobility == mobility) {
        return;
    }

    onLightRemoved(entityId);
    Light_tryWriteLight(entity, ecs::ChannelBit(CHANNEL_LIGHT_PARAMS))->mobility = mobility;
    onLightAdded(entityId);
}

void SceneRenderData::setShadowMobility(EntityID entityId, Mobility mobility)
{
    ecs::Registry &registry = m_scene->getRegistry();
    const ShadowComponent *shadow = registry.tryRead<ShadowComponent>(entityId);
    if (shadow == nullptr || shadow->mobility == mobility) {
        return;
    }

    onShadowRemoved(entityId);
    registry.write<ShadowComponent>(entityId)->mobility = mobility;
    onShadowAdded(entityId);
}

void SceneRenderData::setCascadedShadowMobility(EntityID entityId, Mobility mobility)
{
    ecs::Registry &registry = m_scene->getRegistry();
    const CascadedShadowComponent *shadow = registry.tryRead<CascadedShadowComponent>(entityId);
    if (shadow == nullptr || shadow->mobility == mobility) {
        return;
    }

    onCascadedShadowRemoved(entityId);
    registry.write<CascadedShadowComponent>(entityId)->mobility = mobility;
    onCascadedShadowAdded(entityId);
}

void SceneRenderData::onLightAdded(EntityID entityId)
{
    LightComponent *light = Light_tryWriteLight(ecs::EntityAccessor(entityId, &m_scene->getRegistry()), 0);
    if (light == nullptr) {
        return;
    }
    uint32_t localSlot = m_lights.getPartition(light->mobility).allocateSlot(entityId);
    light->renderDataSlot = m_lights.getGlobalSlot(light->mobility, localSlot);
}

void SceneRenderData::onLightRemoved(EntityID entityId)
{
    LightComponent *light = Light_tryWriteLight(ecs::EntityAccessor(entityId, &m_scene->getRegistry()), 0);
    if (light == nullptr || light->renderDataSlot == UINT32_MAX) {
        return;
    }
    uint32_t localSlot = m_lights.getLocalSlot(light->mobility, light->renderDataSlot);
    m_lights.getPartition(light->mobility).freeSlot(localSlot);
    light->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onCameraAdded(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<CameraComponent>(entityId)) {
        return;
    }

    uint32_t localSlot = m_cameras.getPartition(MOBILITY_DYNAMIC).allocateSlot(entityId);
    registry.write<CameraComponent>(entityId, 0)->renderDataSlot = m_cameras.getGlobalSlot(MOBILITY_DYNAMIC, localSlot);
}

void SceneRenderData::onCameraRemoved(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<CameraComponent>(entityId)) {
        return;
    }

    auto camera = registry.write<CameraComponent>(entityId, 0);
    if (camera->renderDataSlot == UINT32_MAX) {
        return;
    }

    uint32_t localSlot = m_cameras.getLocalSlot(MOBILITY_DYNAMIC, camera->renderDataSlot);
    m_cameras.getPartition(MOBILITY_DYNAMIC).freeSlot(localSlot);
    camera->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onShadowAdded(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<ShadowComponent>(entityId)) {
        return;
    }

    auto shadow = registry.write<ShadowComponent>(entityId, 0);
    uint32_t localSlot = m_shadows.getPartition(shadow->mobility).allocateSlot(entityId);
    shadow->renderDataSlot = m_shadows.getGlobalSlot(shadow->mobility, localSlot);
}

void SceneRenderData::onShadowRemoved(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<ShadowComponent>(entityId)) {
        return;
    }

    auto shadow = registry.write<ShadowComponent>(entityId, 0);
    if (shadow->renderDataSlot == UINT32_MAX) {
        return;
    }

    uint32_t localSlot = m_shadows.getLocalSlot(shadow->mobility, shadow->renderDataSlot);
    m_shadows.getPartition(shadow->mobility).freeSlot(localSlot);
    shadow->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::onCascadedShadowAdded(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<CascadedShadowComponent>(entityId)) {
        return;
    }

    auto shadow = registry.write<CascadedShadowComponent>(entityId, 0);
    uint32_t localSlot = m_shadows.getPartition(shadow->mobility).allocateSlot(entityId);
    shadow->renderDataSlot = m_shadows.getGlobalSlot(shadow->mobility, localSlot);
}

void SceneRenderData::onCascadedShadowRemoved(EntityID entityId)
{
    ecs::Registry &registry = m_scene->getRegistry();
    if (!registry.has<CascadedShadowComponent>(entityId)) {
        return;
    }

    auto shadow = registry.write<CascadedShadowComponent>(entityId, 0);
    if (shadow->renderDataSlot == UINT32_MAX) {
        return;
    }

    uint32_t localSlot = m_shadows.getLocalSlot(shadow->mobility, shadow->renderDataSlot);
    m_shadows.getPartition(shadow->mobility).freeSlot(localSlot);
    shadow->renderDataSlot = UINT32_MAX;
}

void SceneRenderData::createShadowMap(EntityID entityId)
{
    const ShadowComponent *shadow = m_scene->getRegistry().tryRead<ShadowComponent>(entityId);
    if (shadow == nullptr) {
        return;
    }

    float resolution = static_cast<float>(shadow->resolution);
    m_shadowMaps[entityId] = std::make_unique<ShadowMap>(resolution, resolution);
}

void SceneRenderData::destroyShadowMap(EntityID entityId)
{
    m_shadowMaps.erase(entityId);
}

void SceneRenderData::createCascadedShadowMap(EntityID entityId)
{
    const CascadedShadowComponent *shadow = m_scene->getRegistry().tryRead<CascadedShadowComponent>(entityId);
    if (shadow == nullptr) {
        return;
    }

    float resolution = static_cast<float>(shadow->resolution);
    auto map = std::make_unique<CascadedShadowMap>(resolution, resolution, shadow->numCascades, shadow->lambda);
    map->setShadowDistance(shadow->shadowDistance);
    m_cascadedShadowMaps[entityId] = std::move(map);
}

void SceneRenderData::destroyCascadedShadowMap(EntityID entityId)
{
    m_cascadedShadowMaps.erase(entityId);
}

ShadowMap *SceneRenderData::getShadowMap(EntityID entityId) const
{
    auto it = m_shadowMaps.find(entityId);
    return it != m_shadowMaps.end() ? it->second.get() : nullptr;
}

CascadedShadowMap *SceneRenderData::getCascadedShadowMap(EntityID entityId) const
{
    auto it = m_cascadedShadowMaps.find(entityId);
    return it != m_cascadedShadowMaps.end() ? it->second.get() : nullptr;
}

void SceneRenderData::consumeChanges(StoreBookmarks &bookmarks, SceneChannel paramsChannel,
                                     const std::function<void(EntityID)> &repackEntity, const std::function<void()> &repackAll)
{
    ecs::Journal &journal = m_scene->getRegistry().getJournal();

    ecs::Batch transforms = journal.readSince(CHANNEL_TRANSFORM_WORLD, bookmarks.transform);
    ecs::Batch params = journal.readSince(paramsChannel, bookmarks.params);

    if (transforms.needsRebuild() || params.needsRebuild()) {
        repackAll();
        return;
    }

    for (EntityID entity : transforms) {
        repackEntity(entity);
    }
    for (EntityID entity : params) {
        repackEntity(entity);
    }
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

    auto packMesh = [this](RenderPartition<MeshGPUData> &partition, uint32_t i) {
        ecs::Registry &registry = m_scene->getRegistry();
        EntityID entityId = partition.getEntityId(i);

        const TransformComponent *transform = registry.tryRead<TransformComponent>(entityId);
        const MeshComponent *mesh = registry.tryRead<MeshComponent>(entityId);
        if (transform == nullptr || mesh == nullptr || !mesh->mesh) {
            return;
        }

        MeshGPUData &data = partition.getSlotData(i);
        data.modelMatrix = transform->world;
        data.vertexBufferFlags = mesh->mesh->getVertexBuffer()->getBufferLayout().getFlags();
        data.entityId = entityId;
        data.materialIndex = 0;
    };

    // a slot the partition moved needs repacking whether or not its entity changed
    for (uint32_t m = 0; m < MOBILITY_COUNT; m++) {
        auto &partition = m_meshes.getPartition(static_cast<Mobility>(m));
        partition.forEachDirty(frameIndex, [&](uint32_t i) { packMesh(partition, i); });
    }

    ecs::Registry &registry = m_scene->getRegistry();

    auto repackEntity = [&](EntityID entityId) {
        const MeshComponent *mesh = registry.tryRead<MeshComponent>(entityId);
        if (mesh == nullptr || mesh->renderDataSlot == UINT32_MAX) {
            return;
        }

        auto &partition = m_meshes.getPartition(mesh->mobility);
        uint32_t localSlot = m_meshes.getLocalSlot(mesh->mobility, mesh->renderDataSlot);
        packMesh(partition, localSlot);
        partition.markDirty(frameIndex, localSlot);
    };

    auto repackAll = [&]() {
        for (uint32_t m = 0; m < MOBILITY_COUNT; m++) {
            auto &partition = m_meshes.getPartition(static_cast<Mobility>(m));
            for (uint32_t i = 0; i < partition.getCount(); i++) {
                packMesh(partition, i);
                partition.markDirty(frameIndex, i);
            }
        }
    };

    consumeChanges(m_meshBookmarks[frameIndex], CHANNEL_MESH_BINDING, repackEntity, repackAll);
}

void SceneRenderData::updateLights(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::updateLights");

    auto packLight = [&](RenderPartition<LightGPUData> &partition, uint32_t i) {
        ecs::Registry &registry = m_scene->getRegistry();
        EntityID entityId = partition.getEntityId(i);
        ecs::EntityAccessor entity(entityId, &registry);

        const TransformComponent *transform = registry.tryRead<TransformComponent>(entityId);
        const LightComponent *light = Light_tryReadLight(entity);
        if (transform == nullptr || light == nullptr) {
            return;
        }

        LightType type = Light_getLightType(entity);

        auto &data = partition.getSlotData(i);

        glm::vec3 position = transform::translation(transform->world);
        if (type == LightType::DIRECTIONAL) {
            position = glm::vec3(0.0f);
        }
        data.positionAndType = glm::vec4(position, static_cast<float>(type));

        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
        if (type == LightType::DIRECTIONAL || type == LightType::SPOT) {
            direction = transform::forward(transform->world);
        }

        float range = 0.0f;
        float innerCos = 0.0f;
        float outerCos = 0.0f;
        if (const SpotLightComponent *spot = registry.tryRead<SpotLightComponent>(entityId)) {
            range = spot->range;
            innerCos = std::cos(spot->innerConeAngle);
            outerCos = std::cos(spot->outerConeAngle);
        } else if (const PointLightComponent *point = registry.tryRead<PointLightComponent>(entityId)) {
            range = point->range;
        }
        data.directionAndRange = glm::vec4(direction, range);

        data.colorAndIntensity = glm::vec4(light->color, light->intensity);

        data.spotAngles = glm::vec4(innerCos, outerCos, static_cast<float>(entityId), 0.0f);
    };

    for (uint32_t m = 0; m < MOBILITY_COUNT; m++) {
        auto &partition = m_lights.getPartition(static_cast<Mobility>(m));
        partition.forEachDirty(frameIndex, [&](uint32_t i) { packLight(partition, i); });
    }

    ecs::Registry &registry = m_scene->getRegistry();

    auto repackEntity = [&](EntityID entityId) {
        const LightComponent *light = Light_tryReadLight(ecs::EntityAccessor(entityId, &registry));
        if (light == nullptr || light->renderDataSlot == UINT32_MAX) {
            return;
        }

        auto &partition = m_lights.getPartition(light->mobility);
        uint32_t localSlot = m_lights.getLocalSlot(light->mobility, light->renderDataSlot);
        packLight(partition, localSlot);
        partition.markDirty(frameIndex, localSlot);
    };

    auto repackAll = [&]() {
        for (uint32_t m = 0; m < MOBILITY_COUNT; m++) {
            auto &partition = m_lights.getPartition(static_cast<Mobility>(m));
            for (uint32_t i = 0; i < partition.getCount(); i++) {
                packLight(partition, i);
                partition.markDirty(frameIndex, i);
            }
        }
    };

    consumeChanges(m_lightBookmarks[frameIndex], CHANNEL_LIGHT_PARAMS, repackEntity, repackAll);
}

void SceneRenderData::updateCameras(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::updateCameras");
    (void)frameIndex;

    ecs::Registry &registry = m_scene->getRegistry();
    auto &partition = m_cameras.getPartition(MOBILITY_DYNAMIC);

    for (uint32_t i = 0; i < partition.getCount(); i++) {
        EntityID entityId = partition.getEntityId(i);
        if (!registry.hasAll<TransformComponent, CameraComponent>(entityId)) {
            continue;
        }

        // hasRenderData records what this slot holds, not a change to the camera
        auto camera = registry.write<CameraComponent>(entityId, 0);
        auto &data = partition.getSlotData(i);

        // Carried from the values this slot still holds, so it lags exactly one update
        const glm::mat4 previousViewProj = data.projection * data.view;

        data.view = camera->camera.getViewMatrix();
        data.projection = camera->camera.getProjectionMatrix();
        data.invViewProj = glm::inverse(data.projection * data.view);

        // A slot that has never been updated holds a zero matrix, which would reproject to w = 0
        data.prevViewProj = camera->hasRenderData ? previousViewProj : (data.projection * data.view);
        camera->hasRenderData = true;

        // prevViewProj is per frame state rather than a mirror of the component, so a camera is
        // repacked every frame instead of when something announces a change
        partition.markDirty(frameIndex, i);
    }
}

void SceneRenderData::updateShadows(uint32_t frameIndex)
{
    RAPTURE_PROFILE_SCOPE("SceneRenderData::updateShadows");

    auto packShadow = [&](RenderPartition<ShadowGPUData> &partition, uint32_t i) {
        ecs::Registry &registry = m_scene->getRegistry();
        EntityID entityId = partition.getEntityId(i);
        ecs::EntityAccessor entity(entityId, &registry);

        if (Light_tryReadLight(entity) == nullptr) {
            return;
        }

        auto &data = partition.getSlotData(i);

        const ShadowComponent *shadow = registry.tryRead<ShadowComponent>(entityId);
        ShadowMap *shadowMap = getShadowMap(entityId);
        if (shadow != nullptr && shadowMap != nullptr && shadow->isActive) {
            data.type = static_cast<int>(Light_getLightType(entity));
            data.cascadeCount = 1;
            data.lightIndex = entityId;
            data.textureHandle = shadowMap->getTextureHandle();
            data.cascadeMatrices[0] = shadowMap->getLightViewProjection();
            data.cascadeSplitsViewSpace[0] = glm::vec4(0.0f);
            return;
        }

        const CascadedShadowComponent *cascaded = registry.tryRead<CascadedShadowComponent>(entityId);
        CascadedShadowMap *cascadedMap = getCascadedShadowMap(entityId);
        if (cascaded != nullptr && cascadedMap != nullptr && cascaded->isActive) {
            data.type = static_cast<int>(Light_getLightType(entity));
            data.lightIndex = entityId;
            data.textureHandle = cascadedMap->getTextureHandle();

            // both are filled by the first cascade update, which a light packed before its shadow
            // pass has ever run has not had yet
            std::vector<glm::mat4> matrices = cascadedMap->getLightViewProjections();
            std::vector<float> splits = cascadedMap->getCascadeSplits();
            size_t count = std::min<size_t>(cascadedMap->getNumCascades(), matrices.size());
            count = std::min<size_t>(count, splits.empty() ? 0 : splits.size() - 1);
            count = std::min<size_t>(count, MAX_CASCADES);

            data.cascadeCount = static_cast<uint32_t>(count);
            for (size_t c = 0; c < count; c++) {
                data.cascadeMatrices[c] = matrices[c];
                data.cascadeSplitsViewSpace[c] = glm::vec4(splits[c], splits[c + 1], 0.0f, -1.0f);
            }
        }
    };

    for (uint32_t m = 0; m < MOBILITY_COUNT; m++) {
        auto &partition = m_shadows.getPartition(static_cast<Mobility>(m));
        partition.forEachDirty(frameIndex, [&](uint32_t i) { packShadow(partition, i); });
    }

    ecs::Registry &registry = m_scene->getRegistry();

    auto repackEntity = [&](EntityID entityId) {
        Mobility mobility = MOBILITY_DYNAMIC;
        uint32_t globalSlot = UINT32_MAX;

        if (const ShadowComponent *shadow = registry.tryRead<ShadowComponent>(entityId)) {
            mobility = shadow->mobility;
            globalSlot = shadow->renderDataSlot;
        } else if (const CascadedShadowComponent *cascaded = registry.tryRead<CascadedShadowComponent>(entityId)) {
            mobility = cascaded->mobility;
            globalSlot = cascaded->renderDataSlot;
        }

        if (globalSlot == UINT32_MAX) {
            return;
        }

        auto &partition = m_shadows.getPartition(mobility);
        uint32_t localSlot = m_shadows.getLocalSlot(mobility, globalSlot);
        packShadow(partition, localSlot);
        partition.markDirty(frameIndex, localSlot);
    };

    auto repackAll = [&]() {
        for (uint32_t m = 0; m < MOBILITY_COUNT; m++) {
            auto &partition = m_shadows.getPartition(static_cast<Mobility>(m));
            for (uint32_t i = 0; i < partition.getCount(); i++) {
                packShadow(partition, i);
                partition.markDirty(frameIndex, i);
            }
        }
    };

    consumeChanges(m_shadowBookmarks[frameIndex], CHANNEL_SHADOW_SETTINGS, repackEntity, repackAll);
}

} // namespace Rapture
