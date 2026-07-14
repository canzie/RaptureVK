#include "AmethystLayer.h"

#include "EditorLayout.h"
#include "buffers/command_buffers/CommandPool.h"
#include "layers/panels/FileBrowser.h"
#include "layers/panels/ImportPanel.h"
#include "layers/panels/components/tab_layouts.h"
#include "layers/workspaces/AnimationsWorkspace.h"
#include "layers/workspaces/LevelEditorWorkspace.h"
#include "layers/workspaces/MaterialEditorWorkspace.h"
#include "layers/workspaces/ScriptingWorkspace.h"
#include "layers/workspaces/TextureGeneratorWorkspace.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "render_targets/swap_chains/SwapChain.h"
#include "textures/Texture.h"
#include "utils/Timestep.h"
#include "viewport/ViewportManager.h"
#include "window_context/Application.h"

#include <components/common.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/image_label.h>
#include <components/ui_scope.h>
#include <modules/color.h>
#include <modules/style.h>
#include <parsers/config/layout_config.h>
#include <parsers/ttf/ttf_parser.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

VkDescriptorPool AmethystLayer::createUiDescriptorPool()
{
    auto &vulkanContext = Rapture::Application::getInstance().getVulkanContext();

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_BINDLESS_TEXTURES},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8},
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(vulkanContext.getLogicalDevice(), &poolInfo, nullptr, &pool);
    return pool;
}

AmethystLayer::AmethystLayer()
{
    Amethyst::Log::Init();

    auto &app = Rapture::Application::getInstance();
    auto rootPath = app.getProject().getProjectRootDirectory();
    auto themePath = rootPath / "assets/themes/theme.ams";
    Amethyst::Style::load(themePath);

    m_mainSwapchainRecreatedConn = app.getMainWindow().getSwapChain()->onRecreated.connect(
        [this]() { onResize(*Rapture::Application::getInstance().getMainWindow().getSwapChain()); });
}

AmethystLayer::~AmethystLayer()
{
    for (auto &ws : m_workspaces) {
        ws->saveLayout();
    }

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    vulkanContext.waitIdle();

    m_secondaryWindows.clear();

    m_backend.shutdown();
    vkDestroyDescriptorPool(vulkanContext.getLogicalDevice(), m_uiDescriptorPool, nullptr);
    Amethyst::Log::Shutdown();

    Rapture::RP_INFO("---Closing AmethystLayer---");
}

void AmethystLayer::onAttach()
{
    Rapture::RP_INFO("Attaching AmethystLayer...");

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto &window = app.getWindowContext();
    auto swapChain = app.getMainWindow().getSwapChain();

    m_uiDescriptorPool = createUiDescriptorPool();

    auto rootPath = app.getProject().getProjectRootDirectory();
    auto vertShaderPath = (rootPath / "Engine/vendor/Amethyst/backends/shaders/spirv/ui.vs.spv").string();
    auto fragShaderPath = (rootPath / "Engine/vendor/Amethyst/backends/shaders/spirv/ui.fs.spv").string();

    Amethyst::AmVulkanInitInfo initInfo{};
    initInfo.device = vulkanContext.getLogicalDevice();
    initInfo.instance = vulkanContext.getInstance();
    initInfo.physicalDevice = vulkanContext.getPhysicalDevice();
    initInfo.queue = vulkanContext.getVendorQueue()->getQueueVk();
    initInfo.queueFamiliy = vulkanContext.getGraphicsQueueIndex();
    initInfo.pool = m_uiDescriptorPool;
    initInfo.minImageCount = swapChain->getImageCount();
    initInfo.imageCount = swapChain->getImageCount();
    initInfo.colorFormat = swapChain->getImageFormat();
    initInfo.extent = swapChain->getExtent();
    initInfo.vertexShaderPath = vertShaderPath.c_str();
    initInfo.fragmentShaderPath = fragShaderPath.c_str();

    Amethyst::AmGlfwInitInfo glfwInfo{};
    glfwInfo.window = static_cast<GLFWwindow *>(window.getNativeWindowContext());
    glfwInfo.uiWindow = &m_window;

    m_backend.init(initInfo, glfwInfo);

    auto fontPath = rootPath / "assets/fonts/Roboto-Regular.ttf";
    if (!m_amCtx.loadFont(fontPath.string())) {
        Rapture::RP_WARN("Failed to load font from: {}", fontPath.string());
    }

    m_amCtx.init(m_backend);

    glm::vec2 screenSize = {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)};
    m_window.absoluteSize = screenSize;
    m_window.absoluteRotation = 0.0f;

    m_backgroundFrame = m_window.add<Amethyst::Frame>();
    m_backgroundFrame->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
        .zIndex = 0,
    });
    m_backgroundFrame->addClass("background-primary");

    setupMenuBar(screenSize);
    setupWorkspaces(screenSize);

    m_bottomBar = std::make_unique<BottomBar>(&m_window, buildServices());
}

void AmethystLayer::onDetach()
{
    Rapture::RP_INFO("Detaching AmethystLayer...");

    for (auto &ws : m_workspaces) {
        ws->saveLayout();
    }
}

void AmethystLayer::onUpdate(float dt)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &mainWindow = app.getMainWindow();

    auto frame = mainWindow.beginFrame();
    if (!frame.acquired) {
        return;
    }

    for (auto &ws : m_workspaces) {
        ws->onUpdate(dt);
    }

    m_window.tick(dt);

    VkCommandBuffer cmd = frame.commandBuffer->getCommandBufferVk();

    m_amCtx.draw(m_window);
    m_amCtx.syncShared(static_cast<void *>(cmd));
    m_amCtx.syncWindow(static_cast<void *>(cmd), m_window);

    beginDynamicRendering(frame.commandBuffer, frame.imageView, frame.imageIndex, *mainWindow.getSwapChain());

    m_backend.record(cmd, m_amCtx.getDrawList(m_window), mainWindow.getSwapChain()->getExtent());

    endDynamicRendering(frame.commandBuffer, frame.imageIndex, *mainWindow.getSwapChain());

    mainWindow.endFrame();
}

void AmethystLayer::setupMenuBar(glm::vec2 screenSize)
{
    Amethyst::UIScope(m_window).menuBar(
        {
            .classes = {"background-primary"},
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_MENU_BAR_HEIGHT),
                },
            .style =
                {
                    .backgroundTransparency = 0.0f,
                    .borderPixelSize = 0.0f,
                },
        },
        [this](Amethyst::MenuBarScope &mb) {
            m_menuBar = &mb.component;
            mb.menuItem("File", [](Amethyst::DropdownScope &d) {
                d.action("New Scene", [] {});
                d.action("Open Scene", [] {});
                d.action("Save Scene", [] {});
                d.separator();
                d.action("New Project", [] {});
                d.action("Open Project", [] {});
                d.separator();
                d.action("Exit", [] {});
            });
            mb.menuItem("Edit", [](Amethyst::DropdownScope &d) {
                d.action("Undo", [] {});
                d.action("Redo", [] {});
                d.separator();
                d.action("Cut", [] {});
                d.action("Copy", [] {});
                d.action("Paste", [] {});
                d.separator();
                d.action("Editor Preferences", [] {});
            });
            mb.menuItem("Window", [this](Amethyst::DropdownScope &d) {
                d.action("Viewport", [] {});
                d.action("Outliner", [] {});
                d.action("Properties", [] {});
                d.action("Content Browser", [] {});
                d.separator();
                d.action("New Window", [this] { openDemoWindow(); });
            });
            mb.menuItem("Help", [](Amethyst::DropdownScope &d) {
                d.action("Documentation", [] {});
                d.action("About", [] {});
            });
        });
}

void AmethystLayer::setupWorkspaces(glm::vec2 screenSize)
{
    m_workspaces.reserve(5);

    Amethyst::UIScope(m_window).tabBar(
        {
            .classes = {"background-primary"},
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, EDITOR_MENU_BAR_HEIGHT),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(EDITOR_MENU_BAR_HEIGHT + EDITOR_BOTTOM_BAR_HEIGHT)),
                },
            .tabBar =
                {
                    .mode = Amethyst::TabBarMode::INSIDE,
                    .barThickness = EDITOR_WORKSPACE_TAB_HEIGHT,
                },
        },
        [this](Amethyst::TabBarScope &tabs) {
            m_workspaceTabBar = &tabs.component;

            PanelServices services = buildServices();
            Rapture::Scene *levelScene =
                Rapture::Application::getInstance().getProject().getSceneManager().getScene(RAPTURE_DEFAULT_SCENE_NAME);
            Rapture::Viewport *primaryViewport = Rapture::Application::getInstance().getViewportManager().getPrimaryViewport();
            m_workspaces.push_back(std::make_unique<LevelEditorWorkspace>(tabs, services, levelScene, primaryViewport));
            m_workspaces.push_back(std::make_unique<TextureGeneratorWorkspace>(tabs, services));
            m_workspaces.push_back(std::make_unique<MaterialEditorWorkspace>(tabs, services));
            m_workspaces.push_back(std::make_unique<ScriptingWorkspace>(tabs, services));
            m_workspaces.push_back(std::make_unique<AnimationsWorkspace>(tabs, services));
        });

    glm::vec2 dockSize = {
        screenSize.x,
        screenSize.y - EDITOR_CONTENT_TOP - EDITOR_DOCK_SPACING - EDITOR_BOTTOM_BAR_HEIGHT - EDITOR_DOCK_SPACING,
    };
    for (auto &ws : m_workspaces) {
        if (ws->getDockingLayer() != nullptr) {
            ws->getDockingLayer()->absoluteSize = dockSize;
            ws->getDockingLayer()->markDirty();
        }
    }

    m_workspaces[0]->active = true;

    m_workspaceTabBar->onSelectionChanged = [this](int32_t index) {
        for (auto &ws : m_workspaces) {
            ws->active = false;
        }
        if (index >= 0 && index < static_cast<int32_t>(m_workspaces.size())) {
            m_workspaces[index]->active = true;
        }
        m_activeWorkspaceIndex = index;
    };
}

void AmethystLayer::beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView, uint32_t imageIndex,
                                          const Rapture::SwapChain &swapChain)
{
    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    VkRenderingAttachmentInfo colorAttachmentInfo{};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.pNext = nullptr;
    colorAttachmentInfo.imageView = targetImageView;
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachmentInfo.resolveImageView = VK_NULL_HANDLE;
    colorAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue.color = {{1.0f, 0.0f, 1.0f, 1.0f}};

    VkImageMemoryBarrier toColorAttachment{};
    toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toColorAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.image = swapChain.getImages()[imageIndex];
    toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toColorAttachment.subresourceRange.baseMipLevel = 0;
    toColorAttachment.subresourceRange.levelCount = 1;
    toColorAttachment.subresourceRange.baseArrayLayer = 0;
    toColorAttachment.subresourceRange.layerCount = 1;
    toColorAttachment.srcAccessMask = 0;
    toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(commandBufferVk, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toColorAttachment);

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.pNext = nullptr;
    renderingInfo.flags = 0;
    renderingInfo.renderArea = {{0, 0}, {swapChain.getExtent().width, swapChain.getExtent().height}};
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = nullptr;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBufferVk, &renderingInfo);
}

void AmethystLayer::endDynamicRendering(Rapture::CommandBuffer *commandBuffer, uint32_t imageIndex,
                                        const Rapture::SwapChain &swapChain)
{
    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    vkCmdEndRendering(commandBufferVk);

    VkImageMemoryBarrier presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.image = swapChain.getImages()[imageIndex];
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.baseMipLevel = 0;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.baseArrayLayer = 0;
    presentBarrier.subresourceRange.layerCount = 1;
    presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    presentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(commandBufferVk, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &presentBarrier);
}

void AmethystLayer::onResize(const Rapture::SwapChain &swapChain)
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    vulkanContext.waitIdle();

    glm::vec2 screenSize = {static_cast<float>(swapChain.getExtent().width), static_cast<float>(swapChain.getExtent().height)};

    if (&swapChain == app.getMainWindow().getSwapChain().get()) {
        m_window.absoluteSize = screenSize;
        m_window.markDirty();

        for (auto &ws : m_workspaces) {
            if (auto *dock = ws->getDockingLayer()) {
                dock->absoluteSize = {screenSize.x, screenSize.y - EDITOR_CONTENT_TOP - EDITOR_DOCK_SPACING -
                                                        EDITOR_BOTTOM_BAR_HEIGHT - EDITOR_DOCK_SPACING};
                dock->markDirty();
            }
        }
    } else {
        for (auto &context : m_secondaryWindows) {
            if (context->renderWindow->getSwapChain().get() == &swapChain) {
                context->window.absoluteSize = screenSize;
                context->window.markDirty();
                break;
            }
        }
    }
}

PanelServices AmethystLayer::buildServices(void)
{
    PanelServices services;
    services.openSecondaryWindow = [this](int32_t width, int32_t height, std::string_view title, std::function<void(Amethyst::Window &)> build) {
        openSecondaryWindow(width, height, title, std::move(build));
    };
    services.openFileExplorer = [this](FileBrowser::Mode mode, std::function<void(const std::filesystem::path &)> onConfirm) {
        openFileExplorer(mode, std::move(onConfirm));
    };
    services.registerTexture = [this](Rapture::Texture *tex) -> Amethyst::AmTextureId {
        if (tex == nullptr) return Amethyst::AM_INVALID_TEXTURE;
        return m_backend.registerTexture(tex->getImageView(), tex->getSampler().getSamplerVk());
    };
    services.unregisterTexture = [this](Amethyst::AmTextureId id) { m_backend.unregisterTexture(id); };
    services.openImportPanel = [this](const std::filesystem::path &path) {
        struct Session {
            std::unique_ptr<ImportPanel> panel;
            Amethyst::TickHandle watchTick;
            bool pendingDestroy = false;
        };
        auto session = std::make_shared<Session>();
        session->panel = std::make_unique<ImportPanel>(m_window, path);
        session->panel->onClose = [session](void) { session->pendingDestroy = true; };
        session->watchTick = m_window.registerTick([session](float) {
            if (session->pendingDestroy) {
                session->panel.reset();
                session->watchTick.unregister();
            }
        });
    };
    return services;
}

void AmethystLayer::openSecondaryWindow(int32_t width, int32_t height, std::string_view title, std::function<void(Amethyst::Window &)> build)
{
    auto &app = Rapture::Application::getInstance();

    Rapture::RenderWindow &renderWindow = app.createSecondaryWindow(width, height, std::string(title).c_str());
    auto swapChain = renderWindow.getSwapChain();

    auto context = std::make_unique<SecondaryWindowContext>();
    context->renderWindow = &renderWindow;

    glm::vec2 screenSize = {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)};
    context->window.absoluteSize = screenSize;
    context->window.absoluteRotation = 0.0f;

    m_backend.registerWindow(renderWindow.getWindowContext()->getNativeWindowContext(), &context->window);

    build(context->window);

    SecondaryWindowContext *contextPtr = context.get();
    m_secondaryWindows.push_back(std::move(context));

    contextPtr->swapchainRecreatedConn = renderWindow.getSwapChain()->onRecreated.connect(
        [this, contextPtr]() { onResize(*contextPtr->renderWindow->getSwapChain()); });

    renderWindow.onFrame = [this, contextPtr](Rapture::RenderWindow &window) { drawSecondaryWindow(*contextPtr, window); };
    renderWindow.onClose = [this, contextPtr]() { closeSecondaryWindow(contextPtr); };
}

void AmethystLayer::openFileExplorer(FileBrowser::Mode mode, std::function<void(const std::filesystem::path &)> onConfirm)
{
    auto &app = Rapture::Application::getInstance();

    Rapture::RenderWindow &renderWindow = app.createSecondaryWindow(880, 560, "Open File");
    auto swapChain = renderWindow.getSwapChain();

    auto context = std::make_unique<SecondaryWindowContext>();
    context->renderWindow = &renderWindow;

    glm::vec2 screenSize = {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)};
    context->window.absoluteSize = screenSize;
    context->window.absoluteRotation = 0.0f;

    auto fileBrowser = std::make_shared<FileBrowser>(context->window, mode);
    fileBrowser->onConfirm = std::move(onConfirm);

    m_backend.registerWindow(renderWindow.getWindowContext()->getNativeWindowContext(), &context->window);

    SecondaryWindowContext *contextPtr = context.get();
    m_secondaryWindows.push_back(std::move(context));

    contextPtr->swapchainRecreatedConn = renderWindow.getSwapChain()->onRecreated.connect(
        [this, contextPtr]() { onResize(*contextPtr->renderWindow->getSwapChain()); });

    fileBrowser->onClose = [contextPtr]() { contextPtr->renderWindow->getWindowContext()->requestClose(); };

    renderWindow.onFrame = [this, contextPtr](Rapture::RenderWindow &window) { drawSecondaryWindow(*contextPtr, window); };
    renderWindow.onClose = [this, contextPtr, fileBrowser]() mutable {
        // Destroy the browser (and its window tick) while its Window is still alive.
        fileBrowser.reset();
        closeSecondaryWindow(contextPtr);
    };
}

void AmethystLayer::openDemoWindow(void)
{
    openSecondaryWindow(480, 270, "Demo Window", [](Amethyst::Window &win) {
        auto *background = win.add<Amethyst::Frame>();
        background->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
            .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
            .zIndex = 0,
        });
        background->addClass("background-primary");

        Amethyst::UIScope(win).textLabel({
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
            .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER, .textYAlignment = Amethyst::TextYAlignment::CENTER},
            .label = "Hello from a second window!",
        });
    });
}

void AmethystLayer::drawSecondaryWindow(SecondaryWindowContext &context, Rapture::RenderWindow &window)
{
    auto frame = window.beginFrame();
    if (!frame.acquired) {
        return;
    }

    context.window.tick(Rapture::Timestep::deltaTime());

    VkCommandBuffer cmd = frame.commandBuffer->getCommandBufferVk();

    m_amCtx.draw(context.window);
    m_amCtx.syncWindow(static_cast<void *>(cmd), context.window);

    beginDynamicRendering(frame.commandBuffer, frame.imageView, frame.imageIndex, *window.getSwapChain());

    m_backend.record(cmd, m_amCtx.getDrawList(context.window), window.getSwapChain()->getExtent());

    endDynamicRendering(frame.commandBuffer, frame.imageIndex, *window.getSwapChain());

    window.endFrame();
}

void AmethystLayer::closeSecondaryWindow(SecondaryWindowContext *context)
{
    m_backend.unregisterWindow(context->renderWindow->getWindowContext()->getNativeWindowContext());
    for (auto it = m_secondaryWindows.begin(); it != m_secondaryWindows.end(); ++it) {
        if (it->get() == context) {
            m_secondaryWindows.erase(it);
            return;
        }
    }
}
