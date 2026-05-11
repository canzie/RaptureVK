#ifndef RAPTURE__AMETHYST_LAYER_H
#define RAPTURE__AMETHYST_LAYER_H

#include "layers/Layer.h"

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>
#include <components/docking_layer.h>

#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include <memory>
#include <vector>

class AmethystLayer : public Rapture::Layer {
  public:
    AmethystLayer();
    ~AmethystLayer();

    virtual void onAttach() override;
    virtual void onDetach() override;
    virtual void onUpdate(float ts) override;

  private:
    void beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView);
    void endDynamicRendering(Rapture::CommandBuffer *commandBuffer);
    void onResize();

  private:
    float m_Time = 0.0f;
    bool m_framebufferNeedsResize = false;
    size_t m_windowResizeEventListenerID = 0;

    Rapture::CommandPoolHash m_commandPoolHash = 0;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    uint32_t m_imageCount = 0;

    // Amethyst components
    Amethyst::VkBackend m_backend;
    Amethyst::FontLoader m_fontLoader;
    Amethyst::GlyphAtlas m_glyphAtlas;
    Amethyst::TextProcessor m_textProcessor;
    Amethyst::Window m_window;
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    Amethyst::DrawContext m_drawContext;

    std::vector<Amethyst::AmTextureId> m_viewportTextureIds;

    struct Panels;
    std::unique_ptr<Panels> m_panels;
};

#endif // RAPTURE__AMETHYST_LAYER_H
