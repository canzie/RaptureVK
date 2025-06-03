#pragma once

#include "Layers/Layer.h"

//#include "Renderer/ForwardRenderer/ForwardRenderer.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include <memory>
#include <vector>

#include "Textures/Texture.h"

#include "imguiPanels/ViewportPanel.h"
#include "imguiPanels/ProprtiesPanel.h"
#include "imguiPanels/BrowserPanel.h"

#include "RenderTargets/SwapChains/SwapChain.h"

class ImGuiLayer : public Rapture::Layer
{
public:
    ImGuiLayer();
    ~ImGuiLayer();
    
    virtual void onAttach() override;
    virtual void onDetach() override;
    virtual void onUpdate(float ts) override;

private:
    // ImGui Vulkan logic, sets up the dynamic rendering and ImGui draw commands
    void drawImGui(VkCommandBuffer commandBuffer, VkImageView targetImageView);

    // imgui logic loop
    void renderImGui();

    void beginDynamicRendering(VkCommandBuffer commandBuffer, VkImageView targetImageView);
    void endDynamicRendering(VkCommandBuffer commandBuffer);
    void onResize();

    void handleSwapChainRecreation(std::shared_ptr<Rapture::SwapChain> newSwapChain);

private:
    float m_Time = 0.0f;
    float m_FontScale = 1.5f; // Default font scale


    VkDescriptorPool m_imguiPool;
    VkDevice m_device;
    std::vector<std::shared_ptr<Rapture::CommandBuffer>> m_imguiCommandBuffers;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    uint32_t m_imageCount = 0;

    std::vector<std::shared_ptr<Rapture::Texture>> m_swapChainTextures;
    std::vector<VkDescriptorSet> m_defaultTextureDescriptorSets;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    //panels
    ViewportPanel m_viewportPanel;
    PropertiesPanel m_propertiesPanel;
    BrowserPanel m_browserPanel;
};