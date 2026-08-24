#ifndef RAPTURE__VIEWPORT_H
#define RAPTURE__VIEWPORT_H

#include "core/ecs/entity_accessor.h"
#include "core/events/EventSignal.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "renderer/DrawManager.h"
#include "renderer/GizmoDrawList.h"
#include "renderer/RenderSettings.h"
#include "renderer/common.h"
#include "renderer/query/SceneQueryRenderer.h"
#include "scene/Scene.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Rapture {

class CameraController;

/**
 * @brief Creation-time configuration for a viewport and its render target.
 */
struct ViewportConfig {
    Scene *scene;
    ecs::EntityAccessor camera;
    std::string name;
    SceneRenderTarget::TargetType targetType;
    uint32_t width;
    uint32_t height;
    uint32_t framesInFlight;
    bool allowReadback = false;
    bool enableAccelerationStructures = true;
};

using ViewportId = uint32_t;
static constexpr ViewportId INVALID_VIEWPORT_ID = 0;

class Viewport;

struct ViewportContext {
    ViewportId id = INVALID_VIEWPORT_ID;
    std::string displayName;
    Viewport *viewport = nullptr;
};

class Viewport {
  public:
    Viewport(const ViewportConfig &config, RenderContext renderContext);

    ~Viewport();

    Viewport(const Viewport &) = delete;
    Viewport &operator=(const Viewport &) = delete;

    void setScene(Scene *scene);
    Scene *getScene() const { return m_scene; }

    void setCamera(ecs::EntityAccessor camera);
    ecs::EntityAccessor getCamera() const { return m_camera; }

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

    /**
     * @brief Append a renderer to this viewport's draw manager
     * @param type The kind of renderer to add
     */
    void createRenderer(RendererType type);

    DrawManager &getDrawManager() { return *m_drawManager; }
    RendererType getRendererType() const { return m_rendererType; }

    /**
     * @brief The shapes drawn over this viewport, cleared once each frame has been recorded
     * @return This viewport's draw list
     */
    GizmoDrawList &getGizmoDrawList() { return m_gizmoDrawList; }

    void drawFrame();

    /**
     * @brief Fires as this viewport is destroyed, before anything it owns is torn down
     */
    EventSignal<void()> onDestroy;

    /**
     * @brief Render a region of this viewport and report which entities cover each of its pixels
     *
     * Renders on demand and blocks on the result, so it belongs on the thread driving the editor.
     * What the hits mean is the caller's: which one wins, how far from the cursor still counts, and
     * how repeated queries cycle through what a pixel covers.
     *
     * @param region Region in this viewport's render target pixels
     * @return The region's hits, empty where the region lies outside the viewport
     */
    SceneQueryResult queryRegion(const SceneQuery &region);

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

    ViewportId getId() const { return m_id; }
    const std::string &getName() const { return m_config.name; }
    uint32_t getWidth() const { return m_config.width; }
    uint32_t getHeight() const { return m_config.height; }
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

  private:
    inline static ViewportId s_nextId = 1;

    const ViewportId m_id = s_nextId++;
    ViewportConfig m_config;
    RenderContext m_renderContext;

    std::unique_ptr<DrawManager> m_drawManager;
    std::unique_ptr<SceneQueryRenderer> m_queryRenderer;
    GizmoDrawList m_gizmoDrawList;
    RendererType m_rendererType = RendererType::DEFERRED;

    Scene *m_scene = nullptr;
    ecs::EntityAccessor m_camera;
    EditorBinding m_editorBinding;
    RenderSettings m_renderSettings;

    bool m_active = true;

    EventConnection m_windowResizeConn;
};

} // namespace Rapture

#endif // RAPTURE__VIEWPORT_H
