#pragma once

#include "imgui.h"
#include "imguiPanelStyleLinear.h"
#include "Renderer/DeferredShading/DeferredRenderer.h"
#include "Textures/Texture.h"

#include <vector>
#include <vulkan/vulkan.h>

class GBufferPanel {
public:
    GBufferPanel() = default;
    ~GBufferPanel();

    void render();
    void updateDescriptorSets();

private:
    void renderTexture(const char* label, std::shared_ptr<Rapture::Texture> texture, VkDescriptorSet& descriptorSet);

private:
    std::vector<VkDescriptorSet> m_gbufferDescriptorSets;
    bool m_initialized = false;
}; 