#include "imguiLayer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Events/ApplicationEvents.h"
#include "Logging/Log.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Renderer/DeferredShading/DeferredRenderer.h"
#include "WindowContext/Application.h"
#include <algorithm>
#include <stdlib.h> // abort

#include "Events/ApplicationEvents.h"

#include "../imguiPanels/imguiPanelStyleLinear.h"
#include "Logging/TracyProfiler.h"

#include "vendor/ImGuizmo/ImGuizmo.h"

static void s_checkVkResult(VkResult err)
{
    if (err == VK_SUCCESS) return;
    if (err < 0) {
        Rapture::RP_ERROR("ImGuiLayer: VkResult error: {0}", static_cast<int>(err));
        abort();
    }
}

ImGuiLayer::ImGuiLayer()
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    m_contentBrowserPanel.setProjectAssetsPath(app.getProject().getProjectRootDirectory().string());

    m_contentBrowserPanel.setOpenImageViewerCallback([this](Rapture::AssetHandle handle) { openFloatingImageViewer(handle); });

    m_imageViewerPanel.setDescriptorSetCleanupCallback([this](VkDescriptorSet ds) { requestDescriptorSetCleanup(ds); });

    m_viewportTextureDescriptorSets.clear();
    m_viewportTextureDescriptorSets.resize(swapChain->getImageCount(), VK_NULL_HANDLE);

    Rapture::ApplicationEvents::onSwapChainRecreated().addListener([this](std::shared_ptr<Rapture::SwapChain> swapChain) {
        m_gbufferPanel.updateDescriptorSets();
        onResize();
    });

    m_windowResizeEventListenerID = Rapture::ApplicationEvents::onWindowResize().addListener(
        [this](unsigned int width, unsigned int height) { m_framebufferNeedsResize = true; });
}

ImGuiLayer::~ImGuiLayer()
{
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    vulkanContext.waitIdle();

    for (auto &descriptorSet : m_viewportTextureDescriptorSets) {
        if (descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);
    }

    Rapture::RP_INFO("---Closing ImGuiLayer---");
}

void ImGuiLayer::onAttach()
{
    RAPTURE_PROFILE_FUNCTION();

    Rapture::RP_INFO("Attaching ImGuiLayer...");

    // Create Framebuffers
    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto &window = app.getWindowContext();
    m_device = vulkanContext.getLogicalDevice();

    // 1: create descriptor pool for IMGUI
    //  the size of the pool is very oversize, but it's copied from imgui demo itself.
    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
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
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imguiPool);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport
    io.FontGlobalScale = m_FontScale;

    // Setup Dear ImGui style
    ImGuiPanelStyle::InitializeStyle();

    ImGuiPanelStyle::InitializeFonts(app.getProject().getProjectRootDirectory().string());

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan((GLFWwindow *)window.getNativeWindowContext(), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vulkanContext.getInstance();
    init_info.PhysicalDevice = vulkanContext.getPhysicalDevice();
    init_info.Device = vulkanContext.getLogicalDevice();
    init_info.QueueFamily = vulkanContext.getGraphicsQueueIndex();
    init_info.Queue = vulkanContext.getVendorQueue()->getQueueVk();
    init_info.DescriptorPool = m_imguiPool;

    auto swapChain = vulkanContext.getSwapChain();
    const uint32_t swapchainImageCount = swapChain->getImageCount();
    init_info.MinImageCount = swapchainImageCount;
    init_info.ImageCount = swapchainImageCount;

    // Dynamic rendering configuration
    init_info.UseDynamicRendering = true;
    m_imguiColorAttachmentFormats[0] = swapChain->getImageFormat();
    init_info.PipelineRenderingCreateInfo = {};
    init_info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = m_imguiColorAttachmentFormats.data();
    init_info.PipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    init_info.PipelineRenderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = s_checkVkResult;

    {
        auto queueLock = vulkanContext.getVendorQueue()->acquireQueueLock();
        ImGui_ImplVulkan_Init(&init_info);
    }

    uint32_t imageCount = swapChain->getImageCount();

    Rapture::CommandPoolConfig config;
    config.queueFamilyIndex = vulkanContext.getGraphicsQueueIndex();
    config.flags = 0;
    config.threadId = 0;

    m_commandPoolHash = Rapture::CommandPoolManager::createCommandPool(config);
    m_imageCount = imageCount;
    m_currentFrame = 0;
}

void ImGuiLayer::onDetach()
{

    Rapture::RP_INFO("Detaching ImGuiLayer...");
}

void ImGuiLayer::renderImGui()
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    // Setup docking space
    static bool dockspaceOpen = true;
    static bool opt_fullscreen = true;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    if (opt_fullscreen) {
        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |=
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode) window_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::Begin("RaptureVK Editor", &dockspaceOpen, window_flags);

    if (opt_fullscreen) ImGui::PopStyleVar(2);

    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    // Update viewport texture descriptor from scene render target
    updateViewportDescriptorSet();

    {
        RAPTURE_PROFILE_SCOPE("UI Panels Rendering");
        m_viewportPanel.renderSceneViewport((ImTextureID)m_viewportTextureDescriptorSets[m_currentFrame]);
        m_propertiesPanel.render();
        m_browserPanel.render();
        m_gbufferPanel.render();
        m_contentBrowserPanel.render();
        m_imageViewerPanel.render();
        m_settingsPanel.render();
        m_textureGeneratorPanel.render();
        m_graphEditorPanel.render();

        for (auto &viewer : m_floatingImageViews) {
            if (viewer && viewer->isOpen()) {
                viewer->render();
            }
        }

        cleanupClosedImageViews();
    }

    // ImGui::ShowDemoWindow();

    {
        RAPTURE_PROFILE_SCOPE("Menu Bar Rendering");
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) { /* Handle exit */
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Viewport")) { /* Handle exit */
                }
                if (ImGui::MenuItem("Browser")) { /* Handle exit */
                }
                if (ImGui::MenuItem("Properties")) { /* Handle exit */
                }
                if (ImGui::MenuItem("GBuffer Viewer")) { /* Handle exit */
                }
                if (ImGui::MenuItem("Content Browser")) { /* Handle exit */
                }
                if (ImGui::MenuItem("Image Viewer")) { /* Handle exit */
                }
                if (ImGui::MenuItem("Settings")) { /* Handle exit */
                }

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    {
        RAPTURE_PROFILE_SCOPE("ImGui Frame Finalization");
        ImGui::End(); // End dockspace
        ImGui::Render();
        ImGui::EndFrame();
    }
}

void ImGuiLayer::updateViewportDescriptorSet()
{
    RAPTURE_PROFILE_SCOPE("Viewport Descriptor Update");

    auto sceneRenderTarget = Rapture::DeferredRenderer::getSceneRenderTarget();
    if (!sceneRenderTarget) {
        return;
    }

    auto texture = sceneRenderTarget->getTexture(m_currentFrame);
    if (!texture) {
        return;
    }

    if (m_cachedViewportTextures.size() != m_viewportTextureDescriptorSets.size()) {
        m_cachedViewportTextures.resize(m_viewportTextureDescriptorSets.size());
    }

    if (m_cachedViewportTextures[m_currentFrame] != texture) {
        if (m_viewportTextureDescriptorSets[m_currentFrame] != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_viewportTextureDescriptorSets[m_currentFrame]);
            m_viewportTextureDescriptorSets[m_currentFrame] = VK_NULL_HANDLE;
        }

        m_viewportTextureDescriptorSets[m_currentFrame] = ImGui_ImplVulkan_AddTexture(
            texture->getSampler().getSamplerVk(), texture->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_cachedViewportTextures[m_currentFrame] = texture;
    }
}

void ImGuiLayer::onUpdate(float ts)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();
    auto graphicsQueue = vulkanContext.getGraphicsQueue();

    {
        RAPTURE_PROFILE_SCOPE("ImGui Frame Setup");
        // Start ImGui frame - lock queue in case of lazy font upload
        {
            auto queueLock = vulkanContext.getVendorQueue()->acquireQueueLock();
            ImGui_ImplVulkan_NewFrame();
        }
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    processPendingDescriptorSetCleanups();

    int imageIndexi;
    {
        RAPTURE_PROFILE_SCOPE("SwapChain Image Acquisition");
        imageIndexi = swapChain->acquireImage(m_currentFrame);
    }

    if (imageIndexi == -1) {
        RAPTURE_PROFILE_SCOPE("SwapChain Recreation");
        m_currentFrame = 0;
        graphicsQueue->clear();
        this->onResize();
        m_framebufferNeedsResize = false;
        return;
    }

    m_currentImageIndex = static_cast<uint32_t>(imageIndexi);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore imageAvailableSemaphore = swapChain->getImageAvailableSemaphore(m_currentFrame);
    VkSemaphore renderFinishedSemaphore = swapChain->getRenderFinishedSemaphore(m_currentImageIndex);

    // Build ImGui render data first
    {
        RAPTURE_PROFILE_SCOPE("ImGui Render Commands");
        renderImGui();
    }

    // Record ImGui command buffer
    auto pool = Rapture::CommandPoolManager::getCommandPool(m_commandPoolHash, m_currentFrame);
    auto imguiCommandBuffer = pool->getPrimaryCommandBuffer();

    if (imguiCommandBuffer->begin(0) != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to begin recording command buffer for ImGui!");
        return;
    }

    // Draw ImGui directly to swapchain image
    auto targetImageView = swapChain->getImageViews()[m_currentImageIndex];

    {
        RAPTURE_PROFILE_SCOPE("ImGui Command Buffer Setup");
        drawImGui(imguiCommandBuffer, targetImageView);
    }

    if (imguiCommandBuffer->end() != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to record command buffer for ImGui!");
        return;
    }

    {
        RAPTURE_PROFILE_SCOPE("Combined Render Submit");
        std::span<VkSemaphore> waitSemaphoresSpan(&imageAvailableSemaphore, 1);
        std::span<VkSemaphore> signalSemaphoresSpan(&renderFinishedSemaphore, 1);
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        graphicsQueue->submitAndFlushQueue(imguiCommandBuffer, &signalSemaphoresSpan, &waitSemaphoresSpan, &waitStage,
                                           swapChain->getInFlightFence(m_currentFrame));
    }

    // Present - waits for all rendering (scene + imgui) to complete
    VkResult result;
    {
        RAPTURE_PROFILE_SCOPE("SwapChain Present");

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

        VkSwapchainKHR swapChains[] = {swapChain->getSwapChainVk()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &m_currentImageIndex;
        presentInfo.pResults = nullptr;

        result = vulkanContext.getPresentQueue()->presentQueue(presentInfo);
        swapChain->signalImageAvailability(m_currentImageIndex);
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferNeedsResize) {
        RAPTURE_PROFILE_SCOPE("SwapChain Recreation (Present)");
        Rapture::ApplicationEvents::onRequestSwapChainRecreation().publish();
        m_framebufferNeedsResize = false;
        m_currentFrame = 0;
        this->onResize();
        return;
    } else if (result != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to present swap chain image!");
        return;
    }

    m_currentFrame = (m_currentFrame + 1) % m_imageCount;
}

void ImGuiLayer::drawImGui(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    {
        RAPTURE_PROFILE_GPU_SCOPE(commandBufferVk, "ImGui Layer");

        {
            RAPTURE_PROFILE_GPU_SCOPE(commandBufferVk, "Dynamic Rendering Setup");
            beginDynamicRendering(commandBuffer, targetImageView);
        }

        {
            RAPTURE_PROFILE_GPU_SCOPE(commandBufferVk, "ImGui Draw Data Rendering");
            ImDrawData *drawData = ImGui::GetDrawData();
            if (drawData && drawData->CmdListsCount > 0) {
                ImGui_ImplVulkan_RenderDrawData(drawData, commandBufferVk);
            }
        }

        {
            RAPTURE_PROFILE_GPU_SCOPE(commandBufferVk, "Dynamic Rendering End");
            endDynamicRendering(commandBuffer);
        }

        RAPTURE_PROFILE_GPU_COLLECT(commandBufferVk);
    }
}

void ImGuiLayer::beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView)
{
    RAPTURE_PROFILE_FUNCTION();

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
    colorAttachmentInfo.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

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

void ImGuiLayer::endDynamicRendering(Rapture::CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    VkCommandBuffer commandBufferVk = commandBuffer->getCommandBufferVk();

    vkCmdEndRendering(commandBufferVk);

    {
        RAPTURE_PROFILE_SCOPE("Image Layout Transition");
        // Add pipeline barrier to transition to present layout
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

        vkCmdPipelineBarrier(commandBufferVk, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &presentBarrier);
    }
}

void ImGuiLayer::onResize()
{
    RAPTURE_PROFILE_FUNCTION();

    auto &app = Rapture::Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();
    uint32_t newImageCount = swapChain->getImageCount();

    vulkanContext.waitIdle();

    // Clean up old descriptor sets
    for (auto &descriptorSet : m_viewportTextureDescriptorSets) {
        if (descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
    }

    // Resize descriptor set array to match new swapchain image count
    m_viewportTextureDescriptorSets.clear();
    m_viewportTextureDescriptorSets.resize(newImageCount, VK_NULL_HANDLE);
    m_cachedViewportTextures.clear();
    m_cachedViewportTextures.resize(newImageCount);

    m_imageCount = newImageCount;
}

void ImGuiLayer::openFloatingImageViewer(Rapture::AssetHandle textureHandle)
{
    static uint32_t nextImageViewerId = 0;
    std::string uniqueId = "Image Viewer " + std::to_string(nextImageViewerId++);

    auto viewer = std::make_unique<ImageViewerPanel>(textureHandle, uniqueId);
    viewer->setDescriptorSetCleanupCallback([this](VkDescriptorSet ds) { requestDescriptorSetCleanup(ds); });

    m_floatingImageViews.push_back(std::move(viewer));
}

void ImGuiLayer::cleanupClosedImageViews()
{
    m_floatingImageViews.erase(
        std::remove_if(m_floatingImageViews.begin(), m_floatingImageViews.end(),
                       [](const std::unique_ptr<ImageViewerPanel> &viewer) { return !viewer || !viewer->isOpen(); }),
        m_floatingImageViews.end());
}

void ImGuiLayer::requestDescriptorSetCleanup(VkDescriptorSet descriptorSet)
{
    PendingDescriptorSetCleanup cleanup;
    cleanup.descriptorSet = descriptorSet;
    cleanup.frameWhenRequested = m_currentFrame;
    m_pendingDescriptorSetCleanups.push_back(cleanup);
}

void ImGuiLayer::processPendingDescriptorSetCleanups()
{
    uint32_t framesToWait = m_imageCount;

    auto it = m_pendingDescriptorSetCleanups.begin();
    while (it != m_pendingDescriptorSetCleanups.end()) {
        uint32_t framesSinceRequest = (m_currentFrame >= it->frameWhenRequested)
                                          ? (m_currentFrame - it->frameWhenRequested)
                                          : (m_imageCount - it->frameWhenRequested + m_currentFrame);

        if (framesSinceRequest >= framesToWait) {
            ImGui_ImplVulkan_RemoveTexture(it->descriptorSet);
            it = m_pendingDescriptorSetCleanups.erase(it);
        } else {
            ++it;
        }
    }
}
