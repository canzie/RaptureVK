#ifndef RAPTURE__AMETHYST_LAYER_H
#define RAPTURE__AMETHYST_LAYER_H

#include "layers/Layer.h"

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>

#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "layers/BottomBar.h"
#include "layers/panels/Panel.h"
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
    struct Workspace {
        Amethyst::Frame *container = nullptr;
        Amethyst::Frame *hotbar = nullptr;
        Amethyst::DockingLayer *dockingLayer = nullptr;
        std::vector<std::unique_ptr<Panel>> panels;
    };

    void setupMenuBar(glm::vec2 screenSize);
    void setupWorkspaces(glm::vec2 screenSize);
    void beginDynamicRendering(Rapture::CommandBuffer *commandBuffer, VkImageView targetImageView, uint32_t imageIndex);
    void endDynamicRendering(Rapture::CommandBuffer *commandBuffer, uint32_t imageIndex);
    void onResize();

  private:
    float m_Time = 0.0f;

    // Amethyst components
    Amethyst::AmVulkanBackend m_backend;
    Amethyst::AmethystContext m_amCtx;
    Amethyst::Window m_window;
    Amethyst::Frame *m_backgroundFrame = nullptr;
    Amethyst::MenuBar *m_menuBar = nullptr;
    Amethyst::TabBar *m_workspaceTabBar = nullptr;
    std::unique_ptr<BottomBar> m_bottomBar;

    int m_activeWorkspaceIndex = 0;
    std::vector<Workspace> m_workspaces;

    std::vector<Amethyst::AmTextureId> m_viewportTextureIds;
};

#endif // RAPTURE__AMETHYST_LAYER_H
