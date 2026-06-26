#include "Application.h"

#include "asset_manager/AssetManager.h"
#include "events/ApplicationEvents.h"
#include "jobs/JobSystem.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "materials/Material.h"
#include "utils/Timestep.h"

namespace Rapture {

Application *Application::s_instance = nullptr;

Application::Application(int width, int height, const char *title) : m_running(true), m_isMinimized(false)
// m_vulkanContext is not initialized here yet
{
    if (s_instance) {
        RP_CORE_ERROR("Application already exists!");
        return;
    }
    s_instance = this;

    m_platformContext = PlatformContext::create();

    RP_CORE_INFO("Creating window...");
    auto window = std::unique_ptr<WindowContext>(WindowContext::createWindow(*m_platformContext, width, height, title));

    RP_CORE_INFO("Creating Vulkan context...");
    m_vulkanContext = std::make_unique<VulkanContext>(window.get());

    m_mainWindow = std::make_unique<RenderWindow>(std::move(window), *m_vulkanContext);
    m_vulkanContext->initDevice(m_mainWindow->getSurface());

    AssetManager::init();

    m_mainWindow->createSwapChain(*m_vulkanContext);
    m_vulkanContext->initManagers(m_mainWindow->getSwapChain()->getImageCount());
    m_mainWindow->getSwapChain()->invalidate();

    TracyProfiler::init();

#if RAPTURE_TRACY_PROFILING_ENABLED
    if (TracyProfiler::isEnabled()) {
        auto &vc = getVulkanContext();
        auto vendorQueue = vc.getVendorQueue();

        CommandPoolConfig config = {};
        config.queueFamilyIndex = vc.getGraphicsQueueIndex();
        config.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Pool reset handles this now
        auto &cmdPoolMgr = *vc.getRenderContext().commandPoolManager;
        auto tracyPoolHash = cmdPoolMgr.createCommandPool(config);
        auto tracyPool = cmdPoolMgr.getCommandPool(tracyPoolHash, 0);

        auto tempCmdBuffer = tracyPool->getPrimaryCommandBuffer();

        {
            auto queueLock = vendorQueue->acquireQueueLock();
            TracyProfiler::initGPUContext(vc.getPhysicalDevice(), vc.getLogicalDevice(), vendorQueue->getQueueVk(),
                                          tempCmdBuffer->getCommandBufferVk());
        }
    }
#endif // RAPTURE_TRACY_PROFILING_ENABLED

    JobSystem::init();

    m_project = std::make_unique<Project>();
    auto working_dir = std::filesystem::current_path();
    auto root_dir = working_dir;

    // Try to find the project root by looking for Engine folder
    const int max_steps = 4;
    int steps = 0;
    while (steps < max_steps) {
        // Check if Engine directory exists in current path
        if (std::filesystem::exists(root_dir / "Engine") && std::filesystem::exists(root_dir / "build")) {
            break;
        }
        // Go up one directory
        auto parent = root_dir.parent_path();
        if (parent == root_dir) { // We've hit the root
            break;
        }
        root_dir = parent;
        steps++;
    }

    m_project->setProjectRootDirectory(root_dir);
    m_project->setProjectShaderDirectory(root_dir / "Engine/assets/shaders/");

    m_viewportManager = std::make_unique<ViewportManager>(m_vulkanContext->getRenderContext());

    auto swapExtent = m_mainWindow->getSwapChain()->getExtent();
    auto *primaryViewport =
        m_viewportManager->createViewport("main", SceneRenderTarget::TargetType::OFFSCREEN, swapExtent.width, swapExtent.height);
    primaryViewport->createRenderer(RendererType::DEFERRED);

    MaterialManager::init();

    ApplicationEvents::onWindowFocus().addListener([]() { RP_CORE_INFO("Window focused"); });

    ApplicationEvents::onWindowLostFocus().addListener([]() { RP_CORE_INFO("Window lost focus"); });

    ApplicationEvents::onWindowResize().addListener([](uint32_t windowId, unsigned int width, unsigned int height) {
        RP_CORE_INFO("Window {0} resized to {1}x{2}", windowId, width, height);
    });

    RP_CORE_INFO("========== Application created ==========");
}

Application::~Application()
{

    m_vulkanContext->waitIdle();

    TracyProfiler::shutdown();

    m_layerStack.clear();
    m_project.reset();

    m_viewportManager.reset();

    MaterialManager::shutdown();
    AssetManager::shutdown();

    // Shutdown the event system and clear all listeners
    EventRegistry::getInstance().shutdown();

    JobSystem::shutdown();

    RP_CORE_INFO("Application shutting down...");
}

void Application::run()
{

    while (m_running) {
        TracyProfiler::beginFrame();

        Timestep::onUpdate();

        // Flush queues before beginFrame to handle any work queued during initialization
        m_vulkanContext->getComputeQueue()->flush();
        m_vulkanContext->getGraphicsQueue()->flush();
        m_vulkanContext->getTransferQueue()->flush();

        m_vulkanContext->getRenderContext().commandPoolManager->beginFrame();

        for (auto it = m_layerStack.layerBegin(); it != m_layerStack.layerEnd(); ++it) {
            (*it)->onUpdate(Timestep::deltaTime());
        }

        auto activeScene = m_project->getActiveScene();
        if (activeScene != nullptr) {
            activeScene->onUpdate(Timestep::deltaTime());
        }

        // TODO: scene-viewport binding should not live in the main loop
        auto *primaryViewport = m_viewportManager->getPrimaryViewport();
        if (primaryViewport != nullptr && activeScene != nullptr) {
            primaryViewport->setScene(activeScene);
        }

        m_viewportManager->drawAll();

        for (auto it = m_layerStack.overlayBegin(); it != m_layerStack.overlayEnd(); ++it) {
            (*it)->onUpdate(Timestep::deltaTime());
        }

        m_mainWindow->onUpdate();

        if (m_mainWindow->getWindowContext()->shouldClose()) {
            m_running = false;
        }

        for (auto it = m_secondaryWindows.begin(); it != m_secondaryWindows.end();) {
            if (!m_running || (*it)->getWindowContext()->shouldClose()) {
                m_vulkanContext->waitIdle();
                if ((*it)->onClose != nullptr) {
                    (*it)->onClose();
                }
                it = m_secondaryWindows.erase(it);
            } else {
                if ((*it)->onFrame != nullptr) {
                    (*it)->onFrame(**it);
                }
                ++it;
            }
        }

        m_vulkanContext->getRenderContext().commandPoolManager->endFrame();

        TracyProfiler::endFrame();
    }

    m_vulkanContext->waitIdle();
}

RenderWindow &Application::createSecondaryWindow(int width, int height, const char *title)
{
    auto window = std::unique_ptr<WindowContext>(WindowContext::createWindow(*m_platformContext, width, height, title, true));
    auto renderWindow = std::make_unique<RenderWindow>(std::move(window), *m_vulkanContext);
    renderWindow->createSwapChain(*m_vulkanContext);
    renderWindow->getSwapChain()->invalidate();

    RenderWindow &ref = *renderWindow;
    m_secondaryWindows.push_back(std::move(renderWindow));
    return ref;
}

void Application::pushLayer(Layer *layer)
{

    m_layerStack.pushLayer(layer);
    layer->onAttach();
}

void Application::pushOverlay(Layer *overlay)
{

    m_layerStack.pushOverlay(overlay);
    overlay->onAttach();
}

} // namespace Rapture
