#include "Application.h"

#include "asset_manager/AssetManager.h"
#include "events/Events.h"
#include "jobs/JobSystem.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "materials/Material.h"
#include "scenes/instances/InstanceRegistry.h"
#include "utils/EnginePaths.h"
#include "utils/Timestep.h"
#include "utils/rp_assert.h"

#if defined(__linux__)
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/sysinfo.h>
#endif

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

    EnginePaths::init();

    m_platformContext = PlatformContext::create();

    RP_CORE_INFO("Creating window...");
    auto window = std::unique_ptr<WindowContext>(WindowContext::createWindow(*m_platformContext, width, height, title));

    RP_CORE_INFO("Creating Vulkan context...");
    m_vulkanContext = std::make_unique<VulkanContext>(window.get());

    m_mainWindow = std::make_unique<RenderWindow>(std::move(window), *m_vulkanContext);
    m_vulkanContext->initDevice(m_mainWindow->getSurface());

    AssetManager::init(&m_telemetry);

    m_mainWindow->createSwapChain(*m_vulkanContext);
    m_framesInFlight = m_mainWindow->getSwapChain()->getImageCount();
    RP_ASSERT(m_framesInFlight > 0, "the main window's swapchain reported no images");
    m_vulkanContext->initManagers(m_framesInFlight);
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
    InstanceRegistry::init();

    m_project = Project::empty();

    m_viewportManager = std::make_unique<ViewportManager>(m_vulkanContext->getRenderContext());

    auto swapExtent = m_mainWindow->getSwapChain()->getExtent();
    auto *primaryViewport = m_viewportManager->createViewport({
        .name = "main",
        .targetType = SceneRenderTarget::TargetType::OFFSCREEN,
        .width = swapExtent.width,
        .height = swapExtent.height,
    });
    primaryViewport->createRenderer(RendererType::DEFERRED);

    MaterialManager::init();

    AssetManager::registerBuiltinAssets();

    RP_CORE_INFO("========== Application created ==========");
}

Application::~Application()
{

    m_vulkanContext->waitIdle();

    TracyProfiler::shutdown();

    m_layerStack.clear();
    m_project.reset();

    m_viewportManager.reset();

    MaterialManager::releaseGraphResources();
    AssetManager::shutdown();
    MaterialManager::shutdown();

    // Shutdown the event system and clear all listeners
    EventRegistry::getInstance().shutdown();

    InstanceRegistry::shutdown();
    JobSystem::shutdown();

    RP_CORE_INFO("Application shutting down...");
}

void Application::pollTelemetry()
{
    m_vulkanContext->getDeviceLocalMemoryUsage(m_telemetry.vramUsedBytes, m_telemetry.vramBudgetBytes);

#if defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        m_telemetry.ramTotalBytes = static_cast<uint64_t>(info.totalram) * info.mem_unit;
        m_telemetry.ramUsedBytes = static_cast<uint64_t>(info.totalram - info.freeram) * info.mem_unit;
    }

    // sysinfo's freeram counts reclaimable cache and buffers as used, so it overreports; MemAvailable
    // is the kernel's estimate of allocatable memory, matching what system monitors report as free
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            uint64_t availableBytes = std::strtoull(line.c_str() + 13, nullptr, 10) * 1024;
            m_telemetry.ramUsedBytes =
                m_telemetry.ramTotalBytes > availableBytes ? m_telemetry.ramTotalBytes - availableBytes : 0;
            break;
        }
    }
#endif
}

bool Application::openProject(const std::filesystem::path &projectPath)
{
    m_project = std::make_unique<Project>(projectPath.parent_path(), projectPath.stem().string());

    // before the load, so the startup scene's asset is registered by the time the project asks for it
    AssetManager::registerAssetDirectory(m_project->getContentDirectory());

    if (!m_project->loadProject(projectPath)) {
        m_project = Project::empty();
        return false;
    }

    return true;
}

std::filesystem::path Application::createProject(const std::filesystem::path &projectDirectory, std::string_view name)
{
    // the project only has to exist on disk, loading it seeds the world it starts with
    Project project(projectDirectory, name);
    std::filesystem::path projectPath = project.getProjectFilePath();

    if (!project.saveProject(projectPath)) {
        return {};
    }

    return projectPath;
}

void Application::requestRelaunch(const std::filesystem::path &projectPath)
{
    m_relaunchRequested = true;
    m_relaunchProject = projectPath;
    m_running = false;
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

        m_telemetryPollAccum += Timestep::deltaTime();
        if (m_telemetryPollAccum >= 1.0f) {
            m_telemetryPollAccum = 0.0f;
            pollTelemetry();
        }

        AssetManager::onUpdate();

        for (auto it = m_layerStack.layerBegin(); it != m_layerStack.layerEnd(); ++it) {
            if ((*it)->isAttached()) {
                (*it)->onUpdate(Timestep::deltaTime());
            }
        }

        for (auto *scene : m_project->getActiveScenes()) {
            if (scene != nullptr) {
                scene->onUpdate(Timestep::deltaTime());
            }
        }

        m_viewportManager->drawAll();

        for (auto it = m_layerStack.overlayBegin(); it != m_layerStack.overlayEnd(); ++it) {
            if ((*it)->isAttached()) {
                (*it)->onUpdate(Timestep::deltaTime());
            }
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

        m_frameCount++;

        TracyProfiler::endFrame();
    }

    m_vulkanContext->waitIdle();
}

RenderWindow &Application::createSecondaryWindow(int32_t width, int32_t height, const char *title)
{
    auto window = std::unique_ptr<WindowContext>(WindowContext::createWindow(*m_platformContext, width, height, title, true));
    auto renderWindow = std::make_unique<RenderWindow>(std::move(window), *m_vulkanContext);
    renderWindow->createSwapChain(*m_vulkanContext);
    renderWindow->getSwapChain()->invalidate();

    RenderWindow &ref = *renderWindow;
    m_secondaryWindows.push_back(std::move(renderWindow));
    return ref;
}

Layer *Application::pushLayer(std::unique_ptr<Layer> layer)
{
    return m_layerStack.pushLayer(std::move(layer));
}

Layer *Application::pushOverlay(std::unique_ptr<Layer> overlay)
{
    return m_layerStack.pushOverlay(std::move(overlay));
}

Layer *Application::getLayer(std::string_view name) const
{
    for (auto it = m_layerStack.layerBegin(); it != m_layerStack.layerEnd(); ++it) {
        if ((*it)->name() == name) {
            return it->get();
        }
    }

    for (auto it = m_layerStack.overlayBegin(); it != m_layerStack.overlayEnd(); ++it) {
        if ((*it)->name() == name) {
            return it->get();
        }
    }

    return nullptr;
}

} // namespace Rapture
