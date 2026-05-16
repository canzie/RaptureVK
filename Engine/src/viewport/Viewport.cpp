#include "Viewport.h"

#include "renderer/DeferredRenderer.h"
#include "utils/rp_assert.h"

namespace Rapture {

Viewport::Viewport(const std::string &name, RenderContext renderContext, SceneRenderTarget::TargetType targetType, uint32_t width,
                   uint32_t height)
    : m_name(name), m_renderContext(renderContext), m_targetType(targetType), m_width(width), m_height(height)
{
}

Viewport::~Viewport()
{
    m_renderer.reset();
}

void Viewport::setScene(std::shared_ptr<Scene> scene)
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

    switch (type) {
    case RendererType::DEFERRED:
        m_renderer = std::make_unique<DeferredRenderer>(m_renderContext, m_targetType);
        break;
    }

    RP_ASSERT(m_renderer, "Failed to create renderer");
}

void Viewport::drawFrame()
{
    if (!m_active || m_renderer == nullptr || m_scene == nullptr) {
        return;
    }

    Entity camera = m_camera.isValid() ? m_camera : m_scene->getMainCamera();
    m_renderer->drawFrame(m_scene, camera);
}

void Viewport::resize(uint32_t width, uint32_t height)
{
    if (m_width == width && m_height == height) {
        return;
    }
    m_width = width;
    m_height = height;
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

} // namespace Rapture
