#pragma once

#include "Textures/Texture.h"
#include "AssetManager/AssetManager.h"

#include <imgui.h>
#include <memory>
#include <vulkan/vulkan.h>

class ImageViewerPanel {
    public:
        ImageViewerPanel();
        ~ImageViewerPanel();

        void render();

    private:
        void cleanupDescriptorSet();
        void createTextureDescriptor();

    private:
        std::shared_ptr<Rapture::Texture> m_texture;
        VkDescriptorSet m_textureDescriptorSet = VK_NULL_HANDLE;
        Rapture::AssetHandle m_currentTextureHandle;
};