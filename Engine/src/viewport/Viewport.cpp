#include "Viewport.h"

#include "components/Components.h"
#include "renderer/DeferredRenderer.h"
#include "utils/rp_assert.h"
#include "window_context/Application.h"

namespace Rapture {

Viewport::Viewport(const ViewportConfig &config, RenderContext renderContext) : m_config(config), m_renderContext(renderContext) {}

Viewport::~Viewport()
{
    m_renderer.reset();
}

void Viewport::setScene(Scene *scene)
{
    m_scene = scene;
}

void Viewport::setCamera(ecs::EntityAccessor camera)
{
    m_camera = camera;
}

void Viewport::createRenderer(RendererType type)
{
    m_renderer.reset();
    m_rendererType = type;

    RendererConfig rendererConfig{
        .targetType = m_config.targetType,
        .allowReadback = m_config.allowReadback,
        .enableAccelerationStructures = m_config.enableAccelerationStructures,
    };

    switch (type) {
    case RendererType::DEFERRED:
        m_renderer = std::make_unique<DeferredRenderer>(m_renderContext, rendererConfig);
        break;
    }

    RP_ASSERT(m_renderer, "Failed to create renderer");

    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
        m_windowResizeConn = Application::getInstance().getMainWindow().getWindowContext()->onResize.connect(
            [this](uint32_t width, uint32_t height) { resize(width, height); });
    }
}

void Viewport::drawFrame()
{
    if (!m_active || m_renderer == nullptr || m_scene == nullptr) {
        return;
    }

    m_renderer->drawFrame(*m_scene, m_camera, m_renderSettings);
}

SceneQueryResult Viewport::queryRegion(const SceneQuery &region)
{
    if (m_scene == nullptr) {
        return {};
    }

    if (m_queryRenderer == nullptr) {
        m_queryRenderer = std::make_unique<SceneQueryRenderer>(m_renderContext);
    }

    return m_queryRenderer->query(*m_scene, m_camera, m_config.width, m_config.height, region);
}

void Viewport::resize(uint32_t width, uint32_t height)
{
    if (m_config.width == width && m_config.height == height) {
        return;
    }
    m_config.width = width;
    m_config.height = height;
    m_renderer->resizeRenderTarget(width, height);

    if (height == 0 || !m_camera.isValid()) {
        return;
    }
    if (m_camera.has<CameraComponent>()) {
        auto camera = m_camera.write<CameraComponent>();
        float aspect = static_cast<float>(width) / static_cast<float>(height);
        camera->updateProjectionMatrix(camera->fov, aspect, camera->nearPlane, camera->farPlane);
    }
}

void Viewport::onSwapChainRecreated()
{
    if (m_renderer) {
        m_renderer->onSwapChainRecreated();
    }
}

SceneRenderTarget *Viewport::getSceneRenderTarget()
{
    if (!m_renderer) {
        return nullptr;
    }
    return &m_renderer->getSceneRenderTarget();
}

uint32_t Viewport::getLastRenderedFrameIndex() const
{
    if (m_renderer == nullptr) {
        return UINT32_MAX;
    }
    return m_renderer->getLastRenderedFrameIndex();
}

} // namespace Rapture
