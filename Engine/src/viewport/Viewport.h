#ifndef RAPTURE__VIEWPORT_H
#define RAPTURE__VIEWPORT_H

#include "events/EventSignal.h"
#include "renderer/RenderSettings.h"
#include "renderer/Renderer.h"
#include "renderer/common.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"
#include "window_context/vulkan_context/RenderContext.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Rapture {

class CameraController;

/**
 * @brief Creation-time configuration for a viewport and its render target.
 */
struct ViewportConfig {
    std::string name;
    SceneRenderTarget::TargetType targetType;
    uint32_t width;
    uint32_t height;
    bool allowReadback = false;
    bool enableAccelerationStructures = true;
};

class Viewport {
  public:
    Viewport(const ViewportConfig &config, RenderContext renderContext);

    ~Viewport();

    Viewport(const Viewport &) = delete;
    Viewport &operator=(const Viewport &) = delete;

    void setScene(Scene *scene);
    Scene *getScene() const { return m_scene; }

    void setCamera(Entity camera);
    Entity getCamera() const { return m_camera; }

    /**
     * @brief Editor-only conveniences attached to this viewport, not required for rendering
     */
    struct EditorBinding {
        CameraController *controller = nullptr; ///< Non-owning controller driving this viewport's camera
        bool hovered = false;                   ///< Cursor is over this viewport's on-screen display
        bool displayed = false;                 ///< A ViewportPanel is bound to this viewport
    };

    EditorBinding &editorBinding() { return m_editorBinding; }
    const EditorBinding &editorBinding() const { return m_editorBinding; }

    RenderSettings &renderSettings() { return m_renderSettings; }
    const RenderSettings &renderSettings() const { return m_renderSettings; }

    void createRenderer(RendererType type);
    Renderer *getRenderer() { return m_renderer.get(); }
    RendererType getRendererType() const { return m_rendererType; }

    void drawFrame();

    /**
     * @brief Entity drawn at a pixel of this viewport's most recently rendered frame
     * @param x Pixel x in render target space
     * @param y Pixel y in render target space
     * @return The entity, or an invalid entity where nothing was drawn
     */
    Entity pickEntity(uint32_t x, uint32_t y);

    void resize(uint32_t width, uint32_t height);
    void onSwapChainRecreated();

    SceneRenderTarget *getSceneRenderTarget();

    /**
     * @brief Index of the render target slot most recently rendered by this viewport.
     *
     * Consumers that sample the viewport output should bind this slot so they
     * stay aligned with the renderer even across skipped frames.
     *
     * @return The slot index of the most recently rendered frame, or UINT32_MAX if no renderer exists.
     */
    uint32_t getLastRenderedFrameIndex() const;

    const std::string &getName() const { return m_config.name; }
    uint32_t getWidth() const { return m_config.width; }
    uint32_t getHeight() const { return m_config.height; }
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

  private:
    ViewportConfig m_config;
    RenderContext m_renderContext;

    std::unique_ptr<Renderer> m_renderer;
    RendererType m_rendererType = RendererType::DEFERRED;

    Scene *m_scene = nullptr;
    Entity m_camera;
    EditorBinding m_editorBinding;
    RenderSettings m_renderSettings;

    bool m_active = true;

    EventConnection m_windowResizeConn;
};

} // namespace Rapture

#endif // RAPTURE__VIEWPORT_H
