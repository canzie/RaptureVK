#ifndef IMGUI__LAYER_H
#define IMGUI__LAYER_H

#include "Layers/Layer.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <array>
#include <cstddef>
#include <iterator>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include <memory>
#include <vector>

#include "Textures/Texture.h"

#include "AssetManager/AssetManager.h"
#include "imguiPanels/BottomBarPanel.h"
#include "imguiPanels/BrowserPanel.h"
#include "imguiPanels/ContentBrowserPanel.h"
#include "imguiPanels/GBufferPanel.h"
#include "imguiPanels/GraphEditorPanel.h"
#include "imguiPanels/ImageViewerPanel.h"
#include "imguiPanels/PropertiesPanel.h"
#include "imguiPanels/SettingsPanel.h"
#include "imguiPanels/TextureGeneratorPanel.h"
#include "imguiPanels/ViewportPanel.h"
#include "imguiPanels/modules/FileExplorer.h"

#include "RenderTargets/SwapChains/SwapChain.h"

class ImGuiLayer : public Rapture::Layer {
  public:
    ImGuiLayer();
    ~ImGuiLayer();

    virtual void onAttach() override;
    virtual void onDetach() override;
    virtual void onUpdate(float ts) override;

  private:
    // ImGui Vulkan logic, sets up the dynamic rendering and ImGui draw commands
    void drawImGui(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView);

    // imgui logic loop
    void renderImGui();

    void beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView);
    void endDynamicRendering(Rapture::CommandBuffer *commandBuffer);
    void onResize();
    void updateViewportDescriptorSet();

    void handleSwapChainRecreation(std::shared_ptr<Rapture::SwapChain> newSwapChain);

  private:
    float m_Time = 0.0f;
    float m_FontScale = 1.0f; // Default font scale
    bool m_framebufferNeedsResize = false;
    bool m_showStyleEditor = false;
    size_t m_windowResizeEventListenerID = 0;

    VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    Rapture::CommandPoolHash m_commandPoolHash = 0;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    uint32_t m_imageCount = 0;

    // Descriptor sets for the scene render target textures (used in viewport panel)
    std::vector<VkDescriptorSet> m_viewportTextureDescriptorSets;
    std::vector<std::shared_ptr<Rapture::Texture>> m_cachedViewportTextures;

    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    // Cached formats used for ImGui dynamic rendering pipeline creation
    std::array<VkFormat, 1> m_imguiColorAttachmentFormats{};

    // panels
    ViewportPanel m_viewportPanel;
    PropertiesPanel m_propertiesPanel;
    BrowserPanel m_browserPanel;
    GBufferPanel m_gbufferPanel;
    ContentBrowserPanel m_contentBrowserPanel;
    ImageViewerPanel m_imageViewerPanel;
    SettingsPanel m_settingsPanel;
    TextureGeneratorPanel m_textureGeneratorPanel;
    GraphEditorPanel m_graphEditorPanel;
    FileExplorer m_fileExplorer;
    BottomBarPanel m_bottomBarPanel;

    std::vector<std::unique_ptr<ImageViewerPanel>> m_floatingImageViews;

    void openFloatingImageViewer(Rapture::AssetHandle textureHandle);
    void cleanupClosedImageViews();
    void requestDescriptorSetCleanup(VkDescriptorSet descriptorSet);

  private:
    void processPendingDescriptorSetCleanups();

    struct PendingDescriptorSetCleanup {
        VkDescriptorSet descriptorSet;
        uint32_t frameWhenRequested;
    };
    std::vector<PendingDescriptorSetCleanup> m_pendingDescriptorSetCleanups;
};

#endif // IMGUI__LAYER_H