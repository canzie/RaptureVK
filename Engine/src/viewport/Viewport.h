#ifndef RAPTURE__VIEWPORT_H
#define RAPTURE__VIEWPORT_H

#include "renderer/Renderer.h"
#include "renderer/common.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"
#include "window_context/vulkan_context/RenderContext.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Rapture {

class Viewport {
  public:
    Viewport(const std::string& name,
             RenderContext renderContext,
             SceneRenderTarget::TargetType targetType,
             uint32_t width,
             uint32_t height);

    ~Viewport();

    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    void setScene(std::shared_ptr<Scene> scene);
    std::shared_ptr<Scene> getScene() const { return m_scene; }

    void setCamera(Entity camera);
    Entity getCamera() const { return m_camera; }

    void createRenderer(RendererType type);
    Renderer* getRenderer() { return m_renderer.get(); }
    RendererType getRendererType() const { return m_rendererType; }

    void drawFrame();

    void resize(uint32_t width, uint32_t height);
    void onSwapChainRecreated();

    SceneRenderTarget* getSceneRenderTarget();
    const std::string& getName() const { return m_name; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

  private:
    std::string m_name;
    RenderContext m_renderContext;
    SceneRenderTarget::TargetType m_targetType;

    std::unique_ptr<Renderer> m_renderer;
    RendererType m_rendererType = RendererType::DEFERRED;

    std::shared_ptr<Scene> m_scene;
    Entity m_camera;

    uint32_t m_width;
    uint32_t m_height;
    bool m_active = true;
};

} // namespace Rapture

#endif // RAPTURE__VIEWPORT_H
