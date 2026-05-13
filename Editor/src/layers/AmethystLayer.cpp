#include "AmethystLayer.h"

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
#include <modules/style.h>
#include <parsers/config/layout_config.h>
#include <parsers/ttf/ttf_parser.h>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

struct AmethystLayer::Panels {
    std::unique_ptr<ViewportPanel> viewportPanel;
    std::unique_ptr<PropertiesPanel> propertiesPanel;
    std::unique_ptr<OutlinerPanel> outlinerPanel;
    std::unique_ptr<ContentBrowserPanel> contentBrowserPanel;
};

AmethystLayer::AmethystLayer() : m_glyphAtlas(&m_fontLoader)
{
    Amethyst::Log::Init();

    auto &app = Rapture::Application::getInstance();
    auto rootPath = app.getProject().getProjectRootDirectory();
    auto themePath = rootPath / "Engine/vendor/Amethyst/libamethyst/assets/theme.toml";
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

    if (m_dockingLayer && !m_dockingLayer->name.empty()) {
        Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
        Amethyst::LayoutConfig::instance().save();
    }

    m_panels.reset();

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

    m_dockingLayer = m_window.add<Amethyst::DockingLayer>();
    m_dockingLayer->name = "Editor Dock";
    m_dockingLayer->absoluteSize = screenSize;
    m_dockingLayer->absolutePosition = {0.0f, 0.0f};
    m_dockingLayer->markDirty();

    m_panels = std::make_unique<Panels>();
    m_panels->viewportPanel = std::make_unique<ViewportPanel>(m_dockingLayer);
    m_panels->propertiesPanel = std::make_unique<PropertiesPanel>(m_dockingLayer);
    m_panels->outlinerPanel = std::make_unique<OutlinerPanel>(m_dockingLayer);
    m_panels->contentBrowserPanel = std::make_unique<ContentBrowserPanel>(m_dockingLayer);

    m_dockingLayer->persistLayout = true;
    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        Rapture::RP_TRACE("1");
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Editor Dock")) {
            Rapture::RP_TRACE("2");
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                Rapture::RP_TRACE("3");
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }

    auto activeScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (activeScene) {
        m_panels->outlinerPanel->setScene(activeScene);
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

    m_panels->viewportPanel->onUpdate();
    m_panels->contentBrowserPanel->onUpdate(ts);

    VkSemaphore imageAvailableSemaphore = swapChain->getImageAvailableSemaphore(m_currentFrame);
    VkSemaphore renderFinishedSemaphore = swapChain->getRenderFinishedSemaphore(m_currentImageIndex);

    auto* sceneViewport = app.getViewportManager().getPrimaryViewport();
    auto* sceneRenderTarget = sceneViewport != nullptr ? sceneViewport->getSceneRenderTarget() : nullptr;
    if (sceneRenderTarget != nullptr) {
        auto texture = sceneRenderTarget->getTexture(m_currentFrame);
        if (texture) {
            if (m_viewportTextureIds[m_currentFrame].isValid()) {
                m_backend.unregisterTexture(m_viewportTextureIds[m_currentFrame]);
            }

            m_viewportTextureIds[m_currentFrame] =
                m_backend.registerTexture(texture->getImageView(), texture->getSampler().getSamplerVk());
            m_panels->viewportPanel->setViewportImage(m_viewportTextureIds[m_currentFrame]);
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
    colorAttachmentInfo.clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};

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
    m_dockingLayer->absoluteSize = screenSize;
    m_backend.onResize(screenSize);
    m_dockingLayer->markDirty();

    m_imageCount = swapChain->getImageCount();
}
