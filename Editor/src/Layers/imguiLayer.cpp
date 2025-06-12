#include "imguiLayer.h"
#include "WindowContext/Application.h"
#include "Events/ApplicationEvents.h"
#include <stdlib.h>         // abort
#include "Logging/Log.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "RenderTargets/SwapChains/SwapChain.h"

#include "Events/ApplicationEvents.h"

#include "../imguiPanels/imguiPanelStyleLinear.h"
#include "Logging/TracyProfiler.h"

#include "vendor/ImGuizmo/ImGuizmo.h"


static void check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS)
        return;
    //Rapture::RP_ERROR("[vulkan] Error: VkResult = {0}", err);
    if (err < 0)
        abort();
}

ImGuiLayer::ImGuiLayer()
{


    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    Rapture::TextureSpecification spec;
    spec.width = swapChain->getExtent().width;
    spec.height = swapChain->getExtent().height;
    spec.depth = 1;
    spec.type = Rapture::TextureType::TEXTURE2D;
    spec.format = Rapture::TextureFormat::RGBA8;
    for (int i = 0; i < swapChain->getImageCount(); i++) {
        m_swapChainTextures.push_back(std::make_shared<Rapture::Texture>(spec));
    }

    m_defaultTextureDescriptorSets.clear();
    m_defaultTextureDescriptorSets.resize(swapChain->getImageCount());

    Rapture::ApplicationEvents::onSwapChainRecreated().addListener([this](std::shared_ptr<Rapture::SwapChain> swapChain) {
        m_gbufferPanel.updateDescriptorSets();
        //onResize();
    });

    m_windowResizeEventListenerID =
      Rapture::ApplicationEvents::onWindowResize().addListener(
          [this](unsigned int width, unsigned int height) {
            m_framebufferNeedsResize = true;
    });
}



ImGuiLayer::~ImGuiLayer()
{
    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    vulkanContext.waitIdle();

    for (auto& descriptorSet : m_defaultTextureDescriptorSets) {
        if (descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if(m_imguiPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);
    }

    Rapture::RP_INFO("---Closing ImGuiLayer---");

    m_imguiCommandBuffers.clear();

}

void ImGuiLayer::onAttach()
{
    RAPTURE_PROFILE_FUNCTION();

    Rapture::RP_INFO("Attaching ImGuiLayer...");

    // Create Framebuffers
    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto& window = app.getWindowContext();
    m_device = vulkanContext.getLogicalDevice();

	//1: create descriptor pool for IMGUI
	// the size of the pool is very oversize, but it's copied from imgui demo itself.
	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

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
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;       // Enable Docking
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport
    io.FontGlobalScale = m_FontScale;

    // Setup Dear ImGui style
    ImGuiPanelStyle::InitializeStyle();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan((GLFWwindow*)window.getNativeWindowContext(), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    //init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
    init_info.Instance = vulkanContext.getInstance();
    init_info.PhysicalDevice = vulkanContext.getPhysicalDevice();
    init_info.Device = vulkanContext.getLogicalDevice();
    init_info.QueueFamily = vulkanContext.getQueueFamilyIndices().graphicsFamily.value();
    init_info.Queue = vulkanContext.getGraphicsQueue()->getQueueVk();
    init_info.DescriptorPool = m_imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;

    auto swapChain = vulkanContext.getSwapChain();
    VkFormat colorFormat = swapChain->getImageFormat();
    
    init_info.UseDynamicRendering = true;
    init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info); 



    uint32_t imageCount = swapChain->getImageCount();

    Rapture::CommandPoolConfig config;
    config.queueFamilyIndex = vulkanContext.getQueueFamilyIndices().graphicsFamily.value();
    config.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    config.threadId = 0;

    auto commandPool = Rapture::CommandPoolManager::createCommandPool(config);
    m_imguiCommandBuffers = commandPool->getCommandBuffers(imageCount);
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

    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    // Setup docking space
    static bool dockspaceOpen = true;
    static bool opt_fullscreen = true;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    if (opt_fullscreen)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);

    if (opt_fullscreen)
        ImGui::PopStyleVar(2);
    

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    if (m_defaultTextureDescriptorSets[m_currentFrame] != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_defaultTextureDescriptorSets[m_currentFrame]);
    }
    m_defaultTextureDescriptorSets[m_currentFrame] = ImGui_ImplVulkan_AddTexture(m_swapChainTextures[m_currentFrame]->getSampler().getSamplerVk(), m_swapChainTextures[m_currentFrame]->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_viewportPanel.renderSceneViewport((ImTextureID)m_defaultTextureDescriptorSets[m_currentFrame]);

    m_propertiesPanel.render();
    m_browserPanel.render();
    m_gbufferPanel.render();
    m_contentBrowserPanel.render();
    m_imageViewerPanel.render();
    m_settingsPanel.render();
    
    //ImGui::ShowDemoWindow();

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit")) { /* Handle exit */ }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::End(); // End dockspace
    ImGui::Render();
    ImGui::EndFrame();
}

void ImGuiLayer::onUpdate(float ts)
{
    RAPTURE_PROFILE_FUNCTION();

    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();
    auto graphicsQueue = vulkanContext.getGraphicsQueue();

    // Start ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // Acquire next swapchain image
    int imageIndexi = swapChain->acquireImage(m_currentFrame);
    if (imageIndexi == -1) {
        //Rapture::ForwardRenderer::recreateSwapChain();
        m_currentFrame = 0;
        graphicsQueue->clear();
        this->onResize();
        m_framebufferNeedsResize = false;

        return;
    }
    
    m_currentImageIndex = static_cast<uint32_t>(imageIndexi);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore waitSemaphores[] = {swapChain->getImageAvailableSemaphore(m_currentFrame)};
    VkSemaphore signalSemaphores[] = {swapChain->getRenderFinishedSemaphore(m_currentFrame)};


    // First submit - render the scene
    VkSubmitInfo firstSubmitInfo{};
    firstSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    firstSubmitInfo.waitSemaphoreCount = 1;
    firstSubmitInfo.pWaitSemaphores = waitSemaphores;
    firstSubmitInfo.pWaitDstStageMask = waitStages;
    firstSubmitInfo.signalSemaphoreCount = 1;
    firstSubmitInfo.pSignalSemaphores = signalSemaphores;
    graphicsQueue->submitCommandBuffers(firstSubmitInfo);

    // Copy the swapchain image to the texture
    // Wait on the first render to complete, signal a new semaphore for ImGui
    m_swapChainTextures[m_currentFrame]->copyFromImage(
        swapChain->getImages()[m_currentImageIndex], 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        swapChain->getRenderFinishedSemaphore(m_currentFrame), 
        swapChain->getImageAvailableSemaphore(m_currentFrame));

    // ImGui commands
    renderImGui();

    m_imguiCommandBuffers[m_currentFrame]->reset();
    auto targetImageView = swapChain->getImageViews()[m_currentImageIndex];
    drawImGui(m_imguiCommandBuffers[m_currentFrame]->getCommandBufferVk(), targetImageView);
    graphicsQueue->addCommandBuffer(m_imguiCommandBuffers[m_currentFrame]);

    // Final submit - render ImGui
    VkSubmitInfo finalSubmitInfo{};
    finalSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    finalSubmitInfo.waitSemaphoreCount = 1;
    finalSubmitInfo.pWaitSemaphores = waitSemaphores;
    finalSubmitInfo.pWaitDstStageMask = waitStages;
    finalSubmitInfo.signalSemaphoreCount = 1;
    finalSubmitInfo.pSignalSemaphores = signalSemaphores;

    graphicsQueue->submitCommandBuffers(finalSubmitInfo, swapChain->getInFlightFence(m_currentFrame));

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain->getSwapChainVk()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_currentImageIndex; // imageIndex from vkAcquireNextImageKHR
    presentInfo.pResults = nullptr; // Optional

    VkResult result = vulkanContext.getPresentQueue()->presentQueue(presentInfo); 
    swapChain->signalImageAvailability(m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferNeedsResize) {

        //Rapture::ForwardRenderer::recreateSwapChain();
        Rapture::ApplicationEvents::onRequestSwapChainRecreation().publish();
        m_framebufferNeedsResize = false;
        m_currentFrame = 0;
        this->onResize();


        return; // Must return after recreating swap chain, as current frame's resources are invalid.
    } else if (result != VK_SUCCESS) {
        Rapture::RP_ERROR("failed to present swap chain image in ImGuiLayer!");
        throw std::runtime_error("failed to present swap chain image in ImGuiLayer!");
    }


    m_currentFrame = (m_currentFrame + 1) % m_imageCount;

}

void ImGuiLayer::drawImGui(VkCommandBuffer commandBuffer, VkImageView targetImageView)
{
    RAPTURE_PROFILE_FUNCTION();

    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();


    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer for imgui!");
    }

    beginDynamicRendering(commandBuffer, targetImageView);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    endDynamicRendering(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer for imgui!");
    }
}



void ImGuiLayer::beginDynamicRendering(VkCommandBuffer commandBuffer, VkImageView targetImageView)
{
        RAPTURE_PROFILE_FUNCTION();

    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
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

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
}

void ImGuiLayer::endDynamicRendering(VkCommandBuffer commandBuffer) {
    RAPTURE_PROFILE_FUNCTION();

    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    vkCmdEndRendering(commandBuffer);

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

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &presentBarrier
    );
}

void ImGuiLayer::onResize()
{
    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    auto swapChain = vulkanContext.getSwapChain();

    for (auto& descriptorSet : m_defaultTextureDescriptorSets) {
        ImGui_ImplVulkan_RemoveTexture(descriptorSet);
    }

    m_defaultTextureDescriptorSets.clear();
    m_defaultTextureDescriptorSets.resize(swapChain->getImageCount());

    m_swapChainTextures.clear();

    Rapture::TextureSpecification spec;
    spec.width = swapChain->getExtent().width;
    spec.height = swapChain->getExtent().height;
    spec.depth = 1;
    spec.type = Rapture::TextureType::TEXTURE2D;
    spec.format = Rapture::TextureFormat::RGBA8;
    for (int i = 0; i < swapChain->getImageCount(); i++) {
        m_swapChainTextures.push_back(std::make_shared<Rapture::Texture>(spec));
    }

}
