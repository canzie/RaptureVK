#include <rapture/app/Application.h>

#include "core/jobs/JobSystem.h"
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/materials/Material.h>
#include <rapture/core/events/Events.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/Timestep.h>
#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/scene/instances/InstanceRegistry.h>

#if defined(__linux__)
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/sysinfo.h>
#endif

namespace Rapture {

static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

Application *Application::s_instance = nullptr;

Application::Application(const ApplicationConfig &config) : m_running(true), m_isMinimized(false)
// m_vulkanContext is not initialized here yet
{
    if (s_instance) {
        RP_CORE_ERROR("Application already exists!");
        return;
    }
    s_instance = this;

    EnginePaths::init();

    if (config.mainWindow.has_value()) {
        createMainWindowAndDevice(*config.mainWindow, config.queueFamilyAssignment);
    } else {
        createWindowlessDevice(config.queueFamilyAssignment);
    }

    m_framesInFlight = FRAMES_IN_FLIGHT;
    m_frameSlotSubmittedValues.assign(m_framesInFlight, 0);

    AssetManager::init(&m_telemetry, m_framesInFlight);

    m_vulkanContext->initManagers();
    if (m_mainWindow != nullptr) {
        m_mainWindow->getSwapChain()->invalidate();
    }

    TracyProfiler::init();

#if RAPTURE_TRACY_PROFILING_ENABLED
    if (TracyProfiler::isEnabled()) {
        auto &vc = getVulkanContext();
        auto vendorQueue = vc.getVendorQueue();

        // Tracy begins this buffer several times without resetting it, so it needs a pool that allows that
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = vc.getGraphicsQueueIndex();

        VkCommandPool tracyPool = VK_NULL_HANDLE;
        vkCreateCommandPool(vc.getLogicalDevice(), &poolInfo, nullptr, &tracyPool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = tracyPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer tracyCommandBuffer = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(vc.getLogicalDevice(), &allocInfo, &tracyCommandBuffer);

        {
            auto queueLock = vendorQueue->acquireQueueLock();
            TracyProfiler::initGPUContext(vc.getPhysicalDevice(), vc.getLogicalDevice(), vendorQueue->getQueueVk(),
                                          tracyCommandBuffer);
        }

        vkDestroyCommandPool(vc.getLogicalDevice(), tracyPool, nullptr);
    }
#endif // RAPTURE_TRACY_PROFILING_ENABLED

    JobSystem::init();
    InstanceRegistry::init();

    m_project = Project::empty();

    m_viewportManager = std::make_unique<ViewportManager>(m_vulkanContext->getRenderContext());

    MaterialManager::init();

    AssetManager::registerBuiltinAssets();

    RP_CORE_INFO("========== Application created ==========");
}

void Application::createMainWindowAndDevice(const MainWindowConfig &config, QueueFamilyAssignment queueFamilyAssignment)
{
    m_platformContext = PlatformContext::create();

    RP_CORE_INFO("Creating window...");
    auto window = std::unique_ptr<WindowContext>(
        WindowContext::createWindow(*m_platformContext, config.width, config.height, config.title.c_str()));

    RP_CORE_INFO("Creating Vulkan context...");
    const char **surfaceExtensions = window->getExtensions();
    m_vulkanContext = std::make_unique<VulkanContext>(
        std::vector<const char *>(surfaceExtensions, surfaceExtensions + window->getExtensionCount()));

    m_mainWindow = std::make_unique<RenderWindow>(std::move(window), *m_vulkanContext);
    m_vulkanContext->initDevice(m_mainWindow->getSurface(), queueFamilyAssignment);

    m_mainWindow->createSwapChain(*m_vulkanContext);
}

void Application::createWindowlessDevice(QueueFamilyAssignment queueFamilyAssignment)
{
    RP_CORE_INFO("Creating windowless Vulkan context...");
    m_vulkanContext = std::make_unique<VulkanContext>(std::vector<const char *>{});
    m_vulkanContext->initDevice(VK_NULL_HANDLE, queueFamilyAssignment);
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
            m_telemetry.ramUsedBytes = m_telemetry.ramTotalBytes > availableBytes ? m_telemetry.ramTotalBytes - availableBytes : 0;
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
        runFrame();
    }

    m_vulkanContext->waitIdle();
}

void Application::runFrame()
{
    TracyProfiler::beginFrame();

    Timestep::onUpdate();

    // Flush queues before beginFrame to handle any work queued during initialization
    m_vulkanContext->getComputeQueue()->flush();
    m_vulkanContext->getGraphicsQueue()->flush();
    m_vulkanContext->getTransferQueue()->flush();

    waitForFrameSlotToFinish();

    m_telemetryPollAccum += Timestep::deltaTime();
    if (m_telemetryPollAccum >= 1.0f) {
        m_telemetryPollAccum = 0.0f;
        pollTelemetry();
    }

    AssetManager::onUpdate();
    MaterialManager::bindLoadedPendingTextures();

    m_layerStack.updateLayers(Timestep::deltaTime());

    m_project->onUpdate(Timestep::deltaTime());

    m_viewportManager->drawAll();

    m_layerStack.updateOverlays(Timestep::deltaTime());

    if (m_mainWindow != nullptr) {
        m_mainWindow->onUpdate();

        if (m_mainWindow->getWindowContext()->shouldClose()) {
            m_running = false;
        }
    }

    // by index, since a window's callbacks may open another one
    for (size_t i = 0; i < m_secondaryWindows.size();) {
        RenderWindow &window = *m_secondaryWindows[i];
        if (!m_running || window.getWindowContext()->shouldClose()) {
            m_vulkanContext->waitIdle();
            if (window.onClose != nullptr) {
                window.onClose();
            }
            m_secondaryWindows.erase(m_secondaryWindows.begin() + static_cast<ptrdiff_t>(i));
        } else {
            if (window.onFrame != nullptr) {
                window.onFrame(window);
            }
            i++;
        }
    }

    m_vulkanContext->getGraphicsQueue()->flush();
    m_frameSlotSubmittedValues[getFrameInFlightIndex()] = m_vulkanContext->getGraphicsQueue()->getLastSubmittedValue();

    m_frameCount++;

    TracyProfiler::endFrame();
}

void Application::waitForFrameSlotToFinish()
{
    RAPTURE_PROFILE_FUNCTION();

    m_vulkanContext->getGraphicsQueue()->waitForTimelineValue(m_frameSlotSubmittedValues[getFrameInFlightIndex()]);
}

RenderWindow &Application::createSecondaryWindow(int32_t width, int32_t height, const char *title)
{
    RP_ASSERT(!isWindowless(), "a windowless application cannot open windows");

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
