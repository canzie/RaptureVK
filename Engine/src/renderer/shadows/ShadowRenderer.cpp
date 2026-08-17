#include "renderer/shadows/ShadowRenderer.h"

#include "core/utils/TracyProfiler.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

#include <unordered_set>

namespace Rapture {

ShadowRenderer::ShadowRenderer(RenderContext renderContext, const RendererConfig &config) : Renderer(renderContext, config) {}

void ShadowRenderer::onResize(uint32_t width, uint32_t height)
{
    // a shadow map is sized by its light, not by the view it is drawn for
    (void)width;
    (void)height;
}

void ShadowRenderer::recordSecondaries(const RenderPassContext &context, JobContext &jobContext)
{
    RAPTURE_PROFILE_SCOPE("ShadowRenderer::recordSecondaries");

    (void)jobContext;

    m_recordedMaps.clear();
    m_recordedCascades.clear();

    Scene &activeScene = *context.scene;
    auto &registry = activeScene.getRegistry();
    SceneRenderData *renderData = activeScene.getRenderData();
    if (renderData == nullptr) {
        return;
    }

    ecs::Journal &journal = registry.getJournal();
    ecs::Batch transforms = journal.readSince(CHANNEL_TRANSFORM_WORLD, m_transformBookmark);
    ecs::Batch lights = journal.readSince(CHANNEL_LIGHT_PARAMS, m_lightBookmark);
    ecs::Batch settings = journal.readSince(CHANNEL_SHADOW_SETTINGS, m_settingsBookmark);

    bool renderEveryShadow = transforms.needsRebuild() || lights.needsRebuild() || settings.needsRebuild();

    std::unordered_set<ecs::Entity> staleShadows;
    if (!renderEveryShadow) {
        staleShadows.insert(transforms.begin(), transforms.end());
        staleShadows.insert(lights.begin(), lights.end());
        staleShadows.insert(settings.begin(), settings.end());
    }

    for (auto [entity, shadowComp] : registry.read<ShadowComponent>().with<TransformComponent>()) {
        ecs::EntityAccessor lightEntity(entity, &registry);
        if (Light_tryReadLight(lightEntity) == nullptr) {
            continue;
        }

        if (!renderEveryShadow && staleShadows.count(entity) == 0) {
            continue;
        }

        ShadowMap *shadowMap = renderData->getShadowMap(entity);
        if (shadowMap == nullptr) {
            continue;
        }

        auto shadowBuffer = shadowMap->recordSecondary(activeScene, context.frameInFlight);
        if (shadowBuffer) {
            m_recordedMaps.push_back({shadowMap, shadowBuffer});
        }
    }

    // a cascade follows the camera, so it is re-rendered every frame regardless
    for (auto [entity, lightComp, transformComp, shadowComp] :
         registry.read<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>()) {
        CascadedShadowMap *cascadedShadowMap = renderData->getCascadedShadowMap(entity);
        if (cascadedShadowMap == nullptr) {
            continue;
        }

        auto shadowBuffer = cascadedShadowMap->recordSecondary(activeScene, context.frameInFlight, context.terrain);
        if (shadowBuffer) {
            m_recordedCascades.push_back({cascadedShadowMap, shadowBuffer});
        }
    }
}

void ShadowRenderer::replay(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    (void)context;

    RAPTURE_PROFILE_GPU_SCOPE(primaryCb->getCommandBufferVk(), "Shadow Maps");

    for (const RecordedShadow<ShadowMap> &recorded : m_recordedMaps) {
        recorded.map->beginDynamicRendering(primaryCb);
        primaryCb->executeSecondary(*recorded.secondary);
        recorded.map->endDynamicRendering(primaryCb);
    }

    for (const RecordedShadow<CascadedShadowMap> &recorded : m_recordedCascades) {
        recorded.map->beginDynamicRendering(primaryCb);
        primaryCb->executeSecondary(*recorded.secondary);
        recorded.map->endDynamicRendering(primaryCb);
    }
}

} // namespace Rapture
