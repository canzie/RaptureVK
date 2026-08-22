#include "Viewport.h"

#include "scene/components/Components.h"
#include "renderer/DepthPrepassRenderer.h"
#include "renderer/ImmediateShapesRenderer.h"
#include "renderer/deferred/DeferredRenderer.h"
#include "renderer/shadows/ShadowRenderer.h"
#include "core/utils/rp_assert.h"
#include "app/Application.h"

namespace Rapture {

static constexpr uint32_t DRAW_ORDER_SHADOWS = 0;
static constexpr uint32_t DRAW_ORDER_DEPTH_PREPASS = 1;
static constexpr uint32_t DRAW_ORDER_SCENE = 2;
static constexpr uint32_t DRAW_ORDER_IMMEDIATE_SHAPES = 0;

Viewport::Viewport(const ViewportConfig &config, RenderContext renderContext) : m_config(config), m_renderContext(renderContext)
{
    DrawManagerConfig drawConfig{
        .targetType = m_config.targetType,
        .width = m_config.width,
        .height = m_config.height,
        .framesInFlight = m_config.framesInFlight,
        .allowReadback = m_config.allowReadback,
    };

    m_scene = m_config.scene;
    m_camera = m_config.camera;

    m_drawManager = std::make_unique<DrawManager>(m_renderContext, drawConfig);

    if (m_config.targetType == SceneRenderTarget::TargetType::SWAPCHAIN) {
        m_windowResizeConn = Application::getInstance().getMainWindow().getWindowContext()->onResize.connect(
            [this](uint32_t width, uint32_t height) { resize(width, height); });
    }
}

Viewport::~Viewport()
{
    onDestroy.fire();

    m_drawManager.reset();
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
    m_rendererType = type;

    RendererConfig rendererConfig{
        .width = m_config.width,
        .height = m_config.height,
        .framesInFlight = m_config.framesInFlight,
        .outputFormat = m_drawManager->getOutputFormat(),
        .sceneColorFormat = m_drawManager->getSceneColorFormat(),
        .enableAccelerationStructures = m_config.enableAccelerationStructures,
    };

    m_drawManager->addRenderer(std::make_unique<ShadowRenderer>(m_renderContext, rendererConfig), DRAW_PHASE_PRE_COMPOSITE,
                               DRAW_ORDER_SHADOWS);

    m_drawManager->addRenderer(
        std::make_unique<DepthPrepassRenderer>(m_renderContext, rendererConfig, m_drawManager->getDepthFormat()),
        DRAW_PHASE_PRE_COMPOSITE, DRAW_ORDER_DEPTH_PREPASS);

    std::unique_ptr<Renderer> renderer;

    switch (type) {
    case RendererType::DEFERRED:
        renderer = std::make_unique<DeferredRenderer>(m_renderContext, rendererConfig);
        break;
    }

    RP_ASSERT(renderer != nullptr, "Failed to create renderer");

    m_drawManager->addRenderer(std::move(renderer), DRAW_PHASE_PRE_COMPOSITE, DRAW_ORDER_SCENE);

    m_drawManager->addRenderer(std::make_unique<ImmediateShapesRenderer>(m_renderContext, rendererConfig,
                                                                        m_drawManager->getDepthFormat(),
                                                                        &m_immediateDrawList),
                               DRAW_PHASE_POST_COMPOSITE, DRAW_ORDER_IMMEDIATE_SHAPES);
}

void Viewport::drawFrame()
{
    if (m_scene == nullptr) {
        return;
    }

    m_drawManager->drawFrame(*m_scene, m_camera, m_renderSettings);
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
    m_drawManager->resize(width, height);

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
    m_drawManager->onSwapChainRecreated();
}

SceneRenderTarget *Viewport::getSceneRenderTarget()
{
    return &m_drawManager->getSceneRenderTarget();
}

uint32_t Viewport::getLastRenderedFrameIndex() const
{
    return m_drawManager->getLastRenderedFrameIndex();
}

} // namespace Rapture
