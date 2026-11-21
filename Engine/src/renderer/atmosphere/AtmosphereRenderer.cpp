#include "renderer/atmosphere/AtmosphereRenderer.h"

#include <rapture/app/Application.h>
#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Environment.h>

namespace Rapture {

AtmosphereRenderer::AtmosphereRenderer(RenderContext renderContext, const RendererConfig &config) : Renderer(renderContext, config)
{
    m_lookupTablesPass = std::make_unique<ViewAtmosphereLookupTablesPass>(m_config.framesInFlight);
    m_skyAndAerialPerspectivePass = std::make_unique<SkyAndAerialPerspectivePass>(m_config.sceneColorFormat, *m_lookupTablesPass);
}

AtmosphereRenderer::~AtmosphereRenderer()
{
    m_renderContext.vulkanContext->waitIdle();

    m_skyAndAerialPerspectivePass.reset();
    m_lookupTablesPass.reset();
}

void AtmosphereRenderer::onResize(uint32_t width, uint32_t height)
{
    // the passes read the shared targets and own nothing sized to the view
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);
}

void AtmosphereRenderer::recordSecondaries(const RenderPassContext &context, JobContext &jobContext)
{
    RAPTURE_PROFILE_SCOPE("AtmosphereRenderer::recordSecondaries");

    (void)jobContext;

    m_secondary = {};

    Environment *environment = context.scene->environment();
    SkyAtmosphere *atmosphere = nullptr;
    if (environment != nullptr && environment->isSkyboxEnabled()) {
        atmosphere = environment->getSkyAtmosphere();
        m_skyAndAerialPerspectivePass->setSkyIntensity(environment->skyIntensity());
    }

    m_drawAtmosphere = m_lookupTablesPass->prepare(context, atmosphere);
    if (!m_drawAtmosphere) {
        return;
    }

    m_secondary = m_skyAndAerialPerspectivePass->record(context, m_skyAndAerialPerspectivePass->getInheritance(context));
}

void AtmosphereRenderer::replay(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    if (!m_drawAtmosphere || !m_secondary.isValid()) {
        return;
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(primaryCb->getCommandBufferVk(), "Atmosphere Lookup Tables Pass");
        m_lookupTablesPass->execute(context, primaryCb);
    }

    {
        RAPTURE_PROFILE_GPU_SCOPE(primaryCb->getCommandBufferVk(), "Sky And Aerial Perspective Pass");
        m_skyAndAerialPerspectivePass->beginRendering(context, primaryCb);
        primaryCb->executeSecondary(std::move(m_secondary));
        m_skyAndAerialPerspectivePass->endRendering(primaryCb);
    }
}

} // namespace Rapture
