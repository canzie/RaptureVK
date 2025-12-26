#include "Application.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include "Loaders/glTF2.0/glTFLoader.h"
#include "Renderer/DeferredShading/DeferredRenderer.h"

#include "Buffers/BufferPool.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Utils/Timestep.h"

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

    RP_CORE_INFO("Creating window...");
    m_window = std::unique_ptr<WindowContext>(WindowContext::createWindow(width, height, title));

    RP_CORE_INFO("Creating Vulkan context...");
    m_vulkanContext = std::unique_ptr<VulkanContext>(new VulkanContext(m_window.get()));

    CommandPoolManager::init(m_vulkanContext->getSwapChain()->getImageCount());
    BufferPoolManager::init(m_vulkanContext->getVmaAllocator());
    m_vulkanContext->createRecourses();

    TracyProfiler::init();

#if RAPTURE_TRACY_PROFILING_ENABLED
    if (TracyProfiler::isEnabled()) {
        auto &vc = getVulkanContext();
        auto vendorQueue = vc.getVendorQueue();

        CommandPoolConfig config = {};
        config.queueFamilyIndex = vc.getGraphicsQueueIndex();
        config.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Pool reset handles this now
        auto tracyPoolHash = CommandPoolManager::createCommandPool(config);
        auto tracyPool = CommandPoolManager::getCommandPool(tracyPoolHash, 0);

        auto tempCmdBuffer = tracyPool->getPrimaryCommandBuffer();

        {
            auto queueLock = vendorQueue->acquireQueueLock();
            TracyProfiler::initGPUContext(vc.getPhysicalDevice(), vc.getLogicalDevice(), vendorQueue->getQueueVk(),
                                          tempCmdBuffer->getCommandBufferVk());
        }
    }
#endif // RAPTURE_TRACY_PROFILING_ENABLED

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

    AssetManager::init();
    DescriptorManager::init();
    MaterialManager::init();

    DeferredRenderer::init();

    ModelLoadersCache::init();

    ApplicationEvents::onWindowClose().addListener([this]() { m_running = false; });

    ApplicationEvents::onWindowFocus().addListener([this]() { RP_CORE_INFO("Window focused"); });

    ApplicationEvents::onWindowLostFocus().addListener([this]() { RP_CORE_INFO("Window lost focus"); });

    ApplicationEvents::onWindowResize().addListener(
        [this](unsigned int width, unsigned int height) { RP_CORE_INFO("Window resized to {0}x{1}", width, height); });

    RP_CORE_INFO("========== Application created ==========");
}

Application::~Application()
{

    m_vulkanContext->waitIdle();

    TracyProfiler::shutdown();

    ModelLoadersCache::clear();

    m_layerStack.clear();
    m_project.reset();

    DeferredRenderer::shutdown();
    DescriptorManager::shutdown();

    MaterialManager::shutdown();
    AssetManager::shutdown();

    BufferPoolManager::shutdown();

    CommandPoolManager::shutdown();

    // Shutdown the event system and clear all listeners
    EventRegistry::getInstance().shutdown();

    RP_CORE_INFO("Application shutting down...");
}

void Application::run()
{

    while (m_running) {
        TracyProfiler::beginFrame();

        Timestep::onUpdate();

        CommandPoolManager::beginFrame();

        for (auto it = m_layerStack.layerBegin(); it != m_layerStack.layerEnd(); ++it) {
            (*it)->onUpdate(Timestep::deltaTime());
        }

        auto activeScene = m_project->getActiveScene();
        if (activeScene) {
            activeScene->onUpdate(Timestep::deltaTime());

            DeferredRenderer::drawFrame(activeScene);
        }

        for (auto it = m_layerStack.overlayBegin(); it != m_layerStack.overlayEnd(); ++it) {
            (*it)->onUpdate(Timestep::deltaTime());
        }

        m_window->onUpdate();

        CommandPoolManager::endFrame();

        TracyProfiler::endFrame();
    }

    m_vulkanContext->waitIdle();
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
