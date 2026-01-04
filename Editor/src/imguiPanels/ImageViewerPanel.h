#ifndef RAPTURE__IMAGEVIEWERPANEL_H
#define RAPTURE__IMAGEVIEWERPANEL_H

#include "AssetManager/AssetManager.h"
#include "Textures/Texture.h"

#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

class ImageViewerPanel {
  public:
    ImageViewerPanel();
    ImageViewerPanel(Rapture::AssetHandle textureHandle, const std::string &uniqueId);
    ~ImageViewerPanel();

    void render();
    void setTextureHandle(Rapture::AssetHandle textureHandle);
    bool isOpen() const { return m_isOpen; }
    const std::string &getUniqueId() const { return m_uniqueId; }

    using DescriptorSetCleanupCallback = std::function<void(VkDescriptorSet)>;
    void setDescriptorSetCleanupCallback(DescriptorSetCleanupCallback callback) { m_cleanupCallback = callback; }

  private:
    void cleanupDescriptorSet();
    void createTextureDescriptor();

    // Rendering helpers
    void setupInitialWindowSize();
    ImVec2 calculateWindowSizeFromTexture() const;
    void handleDragAndDrop();
    void renderTextureInfo(const Rapture::TextureSpecification &spec);
    ImVec2 calculateDisplaySize(const Rapture::TextureSpecification &spec, const ImVec2 &availableRegion) const;
    void renderTextureImage(const ImVec2 &displaySize);
    void handleMouseWheelZoom();
    void renderEmptyState();
    bool isWindowDocked() const;

  private:
    Rapture::Texture *m_texture = nullptr;
    Rapture::AssetRef m_textureAsset;
    VkDescriptorSet m_textureDescriptorSet = VK_NULL_HANDLE;
    Rapture::AssetHandle m_currentTextureHandle;
    std::string m_uniqueId;
    bool m_isOpen = true;
    bool m_isFirstRender = true;
    float m_zoomFactor = 1.0f;
    DescriptorSetCleanupCallback m_cleanupCallback;
};

#endif // RAPTURE__IMAGEVIEWERPANEL_H
