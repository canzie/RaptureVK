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

void Viewport::setCamera(Entity camera)
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

    Entity camera = m_camera.isValid() ? m_camera : m_scene->getMainCamera();
    m_renderer->drawFrame(*m_scene, camera, m_renderSettings);
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
    auto *cc = m_camera.tryGetComponent<CameraComponent>();
    if (cc != nullptr) {
        float aspect = static_cast<float>(width) / static_cast<float>(height);
        cc->updateProjectionMatrix(cc->fov, aspect, cc->nearPlane, cc->farPlane);
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
