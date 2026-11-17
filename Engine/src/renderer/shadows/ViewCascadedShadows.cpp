#include "renderer/shadows/ViewCascadedShadows.h"

#include "gpu/descriptors/DescriptorManager.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/gpu/vulkan_context/VulkanContext.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/systems/Transforms.h>

#include <algorithm>

namespace Rapture {

static constexpr uint32_t MAX_CASCADED_SHADOWS_PER_VIEW = 8;

static DescriptorBindingSSBO *s_shadowDataBinding(const RenderContext &renderContext)
{
    auto set = renderContext.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::SHADOW_DATA_SSBO);
    return set != nullptr ? set->getSSBOBinding(DescriptorSetBindingLocation::SHADOW_DATA_SSBO).get() : nullptr;
}

static ShadowGPUData s_packCascadedShadow(ecs::Entity light, LightType lightType, const CascadedShadowMap &map)
{
    ShadowGPUData data{};
    data.type = static_cast<int>(lightType);
    data.lightIndex = light;
    data.textureHandle = map.getTextureHandle();

    const std::vector<glm::mat4> &matrices = map.getLightViewProjections();
    const std::vector<float> &splits = map.getCascadeSplits();
    size_t count = std::min<size_t>(map.getNumCascades(), matrices.size());
    count = std::min<size_t>(count, splits.empty() ? 0 : splits.size() - 1);
    count = std::min<size_t>(count, MAX_CASCADES);

    data.cascadeCount = static_cast<uint32_t>(count);
    for (size_t c = 0; c < count; c++) {
        data.cascadeMatrices[c] = matrices[c];
        data.cascadeSplitsViewSpace[c] = glm::vec4(splits[c], splits[c + 1], 0.0f, -1.0f);
    }

    return data;
}

ViewCascadedShadows::ViewCascadedShadows(const RenderContext &renderContext, uint32_t framesInFlight)
    : m_renderContext(renderContext)
{
    VmaAllocator allocator = m_renderContext.vulkanContext->getVmaAllocator();
    DescriptorBindingSSBO *binding = s_shadowDataBinding(m_renderContext);

    m_buffers.resize(framesInFlight);
    m_descriptorIndices.resize(framesInFlight, UINT32_MAX);
    for (uint32_t i = 0; i < framesInFlight; i++) {
        m_buffers[i] = std::make_unique<StorageBuffer>(sizeof(ShadowGPUData) * MAX_CASCADED_SHADOWS_PER_VIEW, BufferUsage::STREAM,
                                                       allocator);
        if (binding != nullptr) {
            m_descriptorIndices[i] = binding->add(*m_buffers[i]);
        }
    }

    m_shadowData.reserve(MAX_CASCADED_SHADOWS_PER_VIEW);
    m_fittedMaps.reserve(MAX_CASCADED_SHADOWS_PER_VIEW);
}

ViewCascadedShadows::~ViewCascadedShadows()
{
    m_renderContext.vulkanContext->waitIdle();

    DescriptorBindingSSBO *binding = s_shadowDataBinding(m_renderContext);
    if (binding == nullptr) {
        return;
    }

    for (uint32_t index : m_descriptorIndices) {
        if (index != UINT32_MAX) {
            binding->free(index);
        }
    }
}

void ViewCascadedShadows::releaseMaps()
{
    if (m_maps.empty()) {
        return;
    }

    m_renderContext.vulkanContext->waitIdle();
    m_maps.clear();
}

void ViewCascadedShadows::update(Scene &scene, ecs::EntityAccessor camera, uint32_t frameIndex)
{
    RAPTURE_PROFILE_FUNCTION();

    if (&scene != m_scene) {
        releaseMaps();
        m_scene = &scene;
    }

    m_fittedMaps.clear();
    m_shadowData.clear();
    m_shadowCount = 0;

    const CameraComponent *cameraComponent = camera.isValid() ? camera.tryRead<CameraComponent>() : nullptr;
    const TransformComponent *cameraTransform = camera.isValid() ? camera.tryRead<TransformComponent>() : nullptr;
    if (cameraTransform != nullptr) {
        m_cameraPosition = transform::translation(cameraTransform->world);
    }

    ecs::Registry &registry = scene.getRegistry();

    m_unseenLights.clear();
    for (const auto &[light, map] : m_maps) {
        m_unseenLights.push_back(light);
    }

    for (auto [entity, light, transform, shadow] : registry.read<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>()) {
        std::erase(m_unseenLights, entity);

        if (!shadow.isActive || cameraComponent == nullptr) {
            continue;
        }

        if (m_fittedMaps.size() == MAX_CASCADED_SHADOWS_PER_VIEW) {
            RP_CORE_WARN("a view draws at most {} cascaded shadows, so light {} casts none", MAX_CASCADED_SHADOWS_PER_VIEW,
                         static_cast<uint32_t>(entity));
            continue;
        }

        std::unique_ptr<CascadedShadowMap> &map = m_maps[entity];
        if (map != nullptr && (map->getResolution() != shadow.resolution || map->getNumCascades() != shadow.numCascades)) {
            m_renderContext.vulkanContext->waitIdle();
            map.reset();
        }
        if (map == nullptr) {
            float resolution = static_cast<float>(shadow.resolution);
            map = std::make_unique<CascadedShadowMap>(resolution, resolution, shadow.numCascades, shadow.lambda);
        }

        map->setLambda(shadow.lambda);
        map->setShadowDistance(shadow.shadowDistance);
        map->updateViewMatrix(light, transform, *cameraComponent);

        m_fittedMaps.push_back(map.get());
        m_shadowData.push_back(s_packCascadedShadow(entity, Light_getLightType(ecs::EntityAccessor(entity, &registry)), *map));
    }

    if (!m_unseenLights.empty()) {
        m_renderContext.vulkanContext->waitIdle();
        for (ecs::Entity light : m_unseenLights) {
            m_maps.erase(light);
        }
    }

    m_shadowCount = static_cast<uint32_t>(m_shadowData.size());
    if (m_shadowCount > 0) {
        m_buffers[frameIndex]->addData(m_shadowData.data(), sizeof(ShadowGPUData) * m_shadowCount, 0);
    }
}

} // namespace Rapture
