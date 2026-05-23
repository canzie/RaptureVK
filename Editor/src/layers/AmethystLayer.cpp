#include "AmethystLayer.h"

#include "EditorLayout.h"
#include "buffers/command_buffers/CommandPool.h"
#include "events/ApplicationEvents.h"
#include "layers/panels/ContentBrowserPanel.h"
#include "layers/panels/OutlinerPanel.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/ViewportPanel.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "render_targets/swap_chains/SwapChain.h"
#include "scenes/SceneManager.h"
#include "viewport/ViewportManager.h"
#include "window_context/Application.h"

#include <components/common.h>
#include <components/docking_layer.h>
#include <components/extensions/ui_drag_detector.h>
#include <components/image_label.h>
#include <modules/color.h>
#include <modules/style.h>
#include <parsers/config/layout_config.h>
#include <parsers/ttf/ttf_parser.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

AmethystLayer::AmethystLayer() : m_glyphAtlas(&m_fontLoader)
{
    Amethyst::Log::Init();

    auto &app = Rapture::Application::getInstance();
    auto rootPath = app.getProject().getProjectRootDirectory();
    auto themePath = rootPath / "assets/themes/theme.toml";
    Amethyst::Style::load(themePath);

    m_windowResizeEventListenerID = Rapture::ApplicationEvents::onWindowResize().addListener(
        [this](unsigned int width, unsigned int height) { m_framebufferNeedsResize = true; });

    Rapture::ApplicationEvents::onSwapChainRecreated().addListener(
        [this](std::shared_ptr<Rapture::SwapChain> swapChain) { onResize(); });
}

AmethystLayer::~AmethystLayer()
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    vulkanContext.waitIdle();

    for (auto &ws : m_workspaces) {
        if (ws.dockingLayer != nullptr && !ws.dockingLayer->name.empty()) {
            Amethyst::LayoutConfig::instance().set(ws.dockingLayer->name, Amethyst::ConfigEntry(ws.dockingLayer->saveConfig()));
        }
        ws.panels.clear();
    }
    Amethyst::LayoutConfig::instance().save();

    for (auto &texId : m_viewportTextureIds) {
        if (texId.isValid()) {
            m_backend.unregisterTexture(texId);
        }
    }

    m_backend.shutdown();
    Amethyst::Log::Shutdown();

    Rapture::RP_INFO("---Closing AmethystLayer---");
}

void AmethystLayer::onAttach()
{
    Rapture::RP_INFO("Attaching AmethystLayer...");

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto &window = app.getWindowContext();
    auto swapChain = vulkanContext.getSwapChain();

    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
                                         {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                         {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(vulkanContext.getLogicalDevice(), &pool_info, nullptr, &descriptorPool);

    auto rootPath = app.getProject().getProjectRootDirectory();
    auto vertShaderPath = (rootPath / "Engine/vendor/Amethyst/backends/shaders/spirv/ui.vs.spv").string();
    auto fragShaderPath = (rootPath / "Engine/vendor/Amethyst/backends/shaders/spirv/ui.fs.spv").string();

    Amethyst::VulkanInitInfo initInfo{};
    initInfo.device = vulkanContext.getLogicalDevice();
    initInfo.instance = vulkanContext.getInstance();
    initInfo.physicalDevice = vulkanContext.getPhysicalDevice();
    initInfo.queue = vulkanContext.getVendorQueue()->getQueueVk();
    initInfo.queueFamiliy = vulkanContext.getGraphicsQueueIndex();
    initInfo.pool = descriptorPool;
    initInfo.minImageCount = swapChain->getImageCount();
    initInfo.imageCount = swapChain->getImageCount();
    initInfo.colorFormat = swapChain->getImageFormat();
    initInfo.extent = swapChain->getExtent();
    initInfo.vertexShaderPath = vertShaderPath.c_str();
    initInfo.fragmentShaderPath = fragShaderPath.c_str();

    Amethyst::GLFWInitInfo glfwInfo{};
    glfwInfo.window = static_cast<GLFWwindow *>(window.getNativeWindowContext());

    m_backend.init(initInfo, glfwInfo);

    // Load font
    auto fontPath = rootPath / "assets/fonts/Roboto-Regular.ttf";
    if (!m_fontLoader.loadFont(fontPath.string())) {
        Rapture::RP_WARN("Failed to load font from: {}", fontPath.string());
    }

    m_textProcessor.setGlyphAtlas(&m_glyphAtlas);

    m_backend.createAtlasTexture(m_glyphAtlas.getWidth(), m_glyphAtlas.getHeight());
    m_glyphAtlas.setTextureId(m_backend.getAtlasTextureId());

    m_drawContext.textProcessor = &m_textProcessor;
    m_drawContext.glyphAtlas = &m_glyphAtlas;

    glm::vec2 screenSize = {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)};
    m_window.absoluteSize = screenSize;
    m_window.absoluteRotation = 0.0f;

    m_backgroundFrame = m_window.add<Amethyst::Frame>();
    m_backgroundFrame->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
    m_backgroundFrame->position = Amethyst::UDim2::fromOffset(0.0f, 0.0f);
    m_backgroundFrame->backgroundColor = Amethyst::Color3::fromHex(0x181818);
    m_backgroundFrame->zIndex = 0;
    m_backgroundFrame->markDirty();

    setupMenuBar(screenSize);
    setupWorkspaces(screenSize);

    m_bottomBar = m_window.add<Amethyst::Frame>();
    m_bottomBar->position = Amethyst::UDim2(glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, -EDITOR_BOTTOM_BAR_HEIGHT));
    m_bottomBar->size = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, EDITOR_BOTTOM_BAR_HEIGHT));
    m_bottomBar->markDirty();

    auto activeScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (activeScene != nullptr) {
        for (auto &ws : m_workspaces) {
            for (auto &panel : ws.panels) {
                if (auto *outliner = dynamic_cast<OutlinerPanel *>(panel.get()); outliner != nullptr) {
                    outliner->setScene(activeScene);
                }
                if (auto *properties = dynamic_cast<PropertiesPanel *>(panel.get()); properties != nullptr) {
                    properties->setScene(activeScene);
                }
            }
        }
    }

    m_viewportTextureIds.resize(swapChain->getImageCount());

    Rapture::CommandPoolConfig config;
    config.queueFamilyIndex = vulkanContext.getGraphicsQueueIndex();
    config.flags = 0;
    config.threadId = 0;

    m_commandPoolHash = vulkanContext.getRenderContext().commandPoolManager->createCommandPool(config);
    m_imageCount = swapChain->getImageCount();
    m_currentFrame = 0;
}

void AmethystLayer::onDetach()
{
    Rapture::RP_INFO("Detaching AmethystLayer...");
}

void AmethystLayer::onUpdate(float ts)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();
    auto graphicsQueue = vulkanContext.getGraphicsQueue();

    int imageIndexi = swapChain->acquireImage(m_currentFrame);

    if (imageIndexi == -1) {
        m_currentFrame = 0;
        graphicsQueue->clear();
        onResize();
        m_framebufferNeedsResize = false;
        return;
    }

    m_currentImageIndex = static_cast<uint32_t>(imageIndexi);

    for (auto &ws : m_workspaces) {
        for (auto &panel : ws.panels) {
            panel->onUpdate(ts);
        }
    }

    VkSemaphore imageAvailableSemaphore = swapChain->getImageAvailableSemaphore(m_currentFrame);
    VkSemaphore renderFinishedSemaphore = swapChain->getRenderFinishedSemaphore(m_currentImageIndex);

    auto *sceneViewport = app.getViewportManager().getPrimaryViewport();
    auto *sceneRenderTarget = sceneViewport != nullptr ? sceneViewport->getSceneRenderTarget() : nullptr;
    if (sceneRenderTarget != nullptr) {
        auto texture = sceneRenderTarget->getTexture(m_currentFrame);
        if (texture != nullptr) {
            if (m_viewportTextureIds[m_currentFrame].isValid()) {
                m_backend.unregisterTexture(m_viewportTextureIds[m_currentFrame]);
            }

            m_viewportTextureIds[m_currentFrame] =
                m_backend.registerTexture(texture->getImageView(), texture->getSampler().getSamplerVk());

            for (auto &ws : m_workspaces) {
                for (auto &panel : ws.panels) {
                    if (auto *vp = dynamic_cast<ViewportPanel *>(panel.get()); vp != nullptr) {
                        vp->setViewportImage(m_viewportTextureIds[m_currentFrame]);
                    }
                }
            }
        }
    }

    {
        RAPTURE_PROFILE_SCOPE("Amethyst::Draw");
        m_window.draw(m_drawContext);
    }

    auto pool = vulkanContext.getRenderContext().commandPoolManager->getCommandPool(m_commandPoolHash, m_currentFrame);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    if (commandBuffer->begin(0) != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to begin recording command buffer for Amethyst!");
        return;
    }

    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    if (m_glyphAtlas.isDirty()) {
        m_backend.uploadAtlasData(cmd, m_glyphAtlas.getPixels(), m_glyphAtlas.getWidth(), m_glyphAtlas.getHeight());
        m_glyphAtlas.clearDirty();
    }

    auto targetImageView = swapChain->getImageViews()[m_currentImageIndex];

    beginDynamicRendering(commandBuffer, targetImageView);

    m_backend.record(cmd);

    endDynamicRendering(commandBuffer);

    if (commandBuffer->end() != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to record command buffer for Amethyst!");
        return;
    }

    std::span<VkSemaphore> waitSemaphoresSpan(&imageAvailableSemaphore, 1);
    std::span<VkSemaphore> signalSemaphoresSpan(&renderFinishedSemaphore, 1);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    graphicsQueue->submitAndFlushQueue(commandBuffer, &signalSemaphoresSpan, &waitSemaphoresSpan, &waitStage,
                                       swapChain->getInFlightFence(m_currentFrame));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

    VkSwapchainKHR swapChains[] = {swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = vulkanContext.getPresentQueue()->presentQueue(presentInfo);
    swapChain->signalImageAvailability(m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferNeedsResize) {
        Rapture::ApplicationEvents::onRequestSwapChainRecreation().publish();
        m_framebufferNeedsResize = false;
        m_currentFrame = 0;
        onResize();
        return;
    } else if (result != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to present swap chain image!");
        return;
    }

    m_currentFrame = (m_currentFrame + 1) % m_imageCount;
}

AmethystLayer::Workspace &AmethystLayer::addWorkspace(const std::string &name, glm::vec2 screenSize)
{
    Workspace ws;

    auto containerFrame = std::make_unique<Amethyst::Frame>();
    containerFrame->name = name;
    containerFrame->backgroundColor = Amethyst::Color3::fromHex(0x181818);
    ws.container = static_cast<Amethyst::Frame *>(m_workspaceTabBar->addChild(std::move(containerFrame)));

    ws.hotbar = ws.container->add<Amethyst::Frame>();
    ws.hotbar->position = Amethyst::UDim2::fromOffset(0.0f, 0.0f);
    ws.hotbar->size = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, EDITOR_HOTBAR_HEIGHT));
    ws.hotbar->backgroundColor = Amethyst::Color3::fromHex(0x252525);
    ws.hotbar->backgroundTransparency = 0.0f;
    ws.hotbar->borderPixelSize = 0.0f;
    ws.hotbar->markDirty();

    ws.dockingLayer = ws.container->add<Amethyst::DockingLayer>();
    ws.dockingLayer->absolutePosition = {0.0f, EDITOR_CONTENT_TOP + EDITOR_DOCK_SPACING};
    ws.dockingLayer->absoluteSize = {screenSize.x, screenSize.y - EDITOR_CONTENT_TOP - EDITOR_DOCK_SPACING -
                                                       EDITOR_BOTTOM_BAR_HEIGHT - EDITOR_DOCK_SPACING};
    ws.dockingLayer->markDirty();

    m_workspaces.push_back(std::move(ws));
    return m_workspaces.back();
}

void AmethystLayer::setupMenuBar(glm::vec2 screenSize)
{
    m_menuBar = m_window.add<Amethyst::MenuBar>();
    m_menuBar->size = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, EDITOR_MENU_BAR_HEIGHT));
    m_menuBar->position = Amethyst::UDim2::fromOffset(0.0f, 0.0f);
    m_menuBar->backgroundColor = Amethyst::Color3::fromHex(0x181818);
    m_menuBar->backgroundTransparency = 0.0f;
    m_menuBar->borderPixelSize = 0.0f;

    using DI = Amethyst::DropdownItem;

    m_menuBar->addMenu("File", {
                                   DI::action("New Scene", [] {}).withShortcut("Ctrl+N"),
                                   DI::action("Open Scene", [] {}).withShortcut("Ctrl+O"),
                                   DI::action("Save Scene", [] {}).withShortcut("Ctrl+S"),
                                   DI::separator(),
                                   DI::action("New Project", [] {}),
                                   DI::action("Open Project", [] {}),
                                   DI::separator(),
                                   DI::action("Exit", [] {}).withShortcut("Alt+F4"),
                               });

    m_menuBar->addMenu("Edit", {
                                   DI::action("Undo", [] {}).withShortcut("Ctrl+Z"),
                                   DI::action("Redo", [] {}).withShortcut("Ctrl+Y"),
                                   DI::separator(),
                                   DI::action("Cut", [] {}).withShortcut("Ctrl+X"),
                                   DI::action("Copy", [] {}).withShortcut("Ctrl+C"),
                                   DI::action("Paste", [] {}).withShortcut("Ctrl+V"),
                                   DI::separator(),
                                   DI::action("Editor Preferences", [] {}),
                               });

    m_menuBar->addMenu("Window", {
                                     DI::action("Viewport", [] {}),
                                     DI::action("Outliner", [] {}),
                                     DI::action("Properties", [] {}),
                                     DI::action("Content Browser", [] {}),
                                 });

    m_menuBar->addMenu("Help", {
                                   DI::action("Documentation", [] {}),
                                   DI::action("About", [] {}),
                               });

    m_menuBar->markDirty();
}

void AmethystLayer::setupWorkspaces(glm::vec2 screenSize)
{
    m_workspaceTabBar = m_window.add<Amethyst::TabBar>();
    m_workspaceTabBar->position = Amethyst::UDim2::fromOffset(0.0f, EDITOR_MENU_BAR_HEIGHT);
    m_workspaceTabBar->size =
        Amethyst::UDim2(glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, -(EDITOR_MENU_BAR_HEIGHT + EDITOR_BOTTOM_BAR_HEIGHT)));
    m_workspaceTabBar->mode = Amethyst::TabBarMode::INSIDE;
    m_workspaceTabBar->barThickness = EDITOR_WORKSPACE_TAB_HEIGHT;
    m_workspaceTabBar->markDirty();

    m_workspaces.reserve(4);

    auto &levelEditor = addWorkspace("Level Editor", screenSize);
    levelEditor.dockingLayer->name = "Editor Dock";
    levelEditor.dockingLayer->persistLayout = true;
    levelEditor.panels.push_back(std::make_unique<ViewportPanel>(levelEditor.dockingLayer));
    levelEditor.panels.push_back(std::make_unique<OutlinerPanel>(levelEditor.dockingLayer));
    levelEditor.panels.push_back(std::make_unique<PropertiesPanel>(levelEditor.dockingLayer));
    levelEditor.panels.push_back(std::make_unique<ContentBrowserPanel>(levelEditor.dockingLayer));

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Editor Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                levelEditor.dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }

    addWorkspace("Material Editor", screenSize);
    addWorkspace("Scripting", screenSize);
    addWorkspace("Animations", screenSize);
}

void AmethystLayer::beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView)
{
    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

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
    toColorAttachment.image = swapChain->getImages()[m_currentImageIndex];
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
    renderingInfo.renderArea = {{0, 0}, {swapChain->getExtent().width, swapChain->getExtent().height}};
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = nullptr;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBufferVk, &renderingInfo);
}

void AmethystLayer::endDynamicRendering(Rapture::CommandBuffer *commandBuffer)
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    vkCmdEndRendering(commandBufferVk);

    VkImageMemoryBarrier presentBarrier{};
    presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    presentBarrier.image = swapChain->getImages()[m_currentImageIndex];
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

void AmethystLayer::onResize()
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    vulkanContext.waitIdle();

    for (auto &texId : m_viewportTextureIds) {
        if (texId.isValid()) {
            m_backend.unregisterTexture(texId);
        }
    }

    m_viewportTextureIds.clear();
    m_viewportTextureIds.resize(swapChain->getImageCount());

    glm::vec2 screenSize = {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)};
    m_window.absoluteSize = screenSize;

    for (auto &ws : m_workspaces) {
        if (ws.dockingLayer != nullptr) {
            ws.dockingLayer->absoluteSize = {screenSize.x, screenSize.y - EDITOR_CONTENT_TOP - EDITOR_DOCK_SPACING -
                                                               EDITOR_BOTTOM_BAR_HEIGHT - EDITOR_DOCK_SPACING};
        }
    }

    m_backend.onResize(screenSize);
    m_window.markDirty();

    m_imageCount = swapChain->getImageCount();
}
