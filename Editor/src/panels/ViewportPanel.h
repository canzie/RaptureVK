#ifndef RAPTURE__VIEWPORT_PANEL_H
#define RAPTURE__VIEWPORT_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/radio_button.h>
#include <components/ui_scope.h>

#include "gizmos/TransformGizmo.h"
#include "panels/Panel.h"
#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/core/events/EventSignal.h>

#include <cstdint>
#include <math/math.h>
#include <memory>
#include <vector>

namespace Rapture {
class CameraController;
class Viewport;
class Texture;
} // namespace Rapture

class IconLabelButton;
class NumberMenuItem;
class OrientationGizmo;

enum ViewportLightingMode {
    VLM_LIT,
    VLM_RAW_IRRADIANCE,
    VLM_INDIRECT_LIGHTING,
    VLM_DIRECT_LIGHTING,
    VLM_SHADOW_CASCADES,
    VLM_NORMALS,
    VLM_MOTION,
    VLM_AMBIENT_OCCLUSION,
    VLM_METTALIC,
    VLM_ROUGHNESS,
    VLM_AO,
    VLM_ALBEDO,
    VLM_MATERIAL_ID,
    VLM_DEPTH,
    VLM_COUNT
};

/**
 * @brief Which overlays a viewport panel shows over its image
 */
struct ViewportOverlays {
    bool toolbar;
    bool orientationGizmo;
};

class ViewportPanel : public Panel {
  public:
    std::string_view typeName() const override { return "ViewportPanel"; }
    ViewportPanel(const WorkspaceContext &context, ViewportOverlays overlays);
    ~ViewportPanel() override;

    void setViewportImage(Amethyst::AmTextureId imageId);

    /**
     * @brief Outlines the viewport image to show its world is playing
     * @param visible Whether the outline shows
     */
    void setPlayingOutlineVisible(bool visible);

    /**
     * @brief Hands back every image this panel holds for its viewport's targets
     */
    void releaseSlotImages();

    void onUpdate(float dt) override;

    gizmo::TransformGizmo::Operation getGizmoOperation() const { return m_gizmoOperation; }
    void setGizmoOperation(gizmo::TransformGizmo::Operation op)
    {
        m_gizmoOperation = op;
        m_gizmoOpGroup.value = static_cast<int32_t>(op);
    }
    Rapture::Viewport *getViewport() const { return m_viewport; }

    gizmo::TransformGizmo::Space getGizmoSpace() const { return m_gizmoSpace; }
    void setGizmoSpace(gizmo::TransformGizmo::Space space)
    {
        m_gizmoSpace = space;
        m_gizmoSpaceGroup.value = static_cast<int32_t>(space);
    }

  public:
    Rapture::EventSignal<void()> onImageClicked;

  protected:
    void build(Amethyst::UIScope &scope) override;

    /**
     * @brief Starts the viewport drawing, since what it draws is now on screen
     */
    void onShown() override;

    /**
     * @brief Stops the viewport drawing, since nothing is looking at what it draws
     */
    void onHidden() override;

  private:
    void updateGizmo(void);

    /**
     * @brief Handles a press on the viewport image
     * @param input The press
     */
    void onViewportPressed(const Amethyst::InputObject &input);

    /**
     * @brief Handles the cursor moving over the viewport image
     * @param input The movement
     */
    void onViewportCursorMoved(const Amethyst::InputObject &input);

    /**
     * @brief Handles a mouse button releasing over the viewport image
     * @param input The release
     */
    void onViewportMouseReleased(const Amethyst::InputObject &input);

    /**
     * @brief Where a window position lands in this viewport's render target
     * @param windowPosition Position Amethyst reported an input at
     * @param[out] pixel The viewport pixel the position maps to, y down
     * @return True where the position lies inside the viewport image
     */
    bool toViewportPixel(const Amethyst::vec2 &windowPosition, glm::vec2 &pixel) const;

    void setupOverlayButtons(void);

    /**
     * @brief Shows the world axes over the image, clicking one turns the editor camera to look along it
     */
    void setupOrientationGizmo(void);

    void buildTransformMenu(void);
    void buildRenderMenu(void);

    /**
     * @brief Points the camera rows of the transform menu at the editor camera's current values
     */
    void syncCameraMenuItems();
    void applyLightingMode(ViewportLightingMode mode);
    Rapture::CameraController *cameraController(void) const;

    /**
     * @brief Flies the editor camera with this frame's controls while the cursor is over the image
     * @param dt Seconds since the last frame
     */
    void updateEditorCamera(float dt);
    void updateViewportImage(void);

  private:
    Amethyst::ImageLabel *m_viewportImage = nullptr;

    Amethyst::EventConnection m_viewportImageDestroyConn;

    Amethyst::Frame *m_viewportToolbar = nullptr;
    IconLabelButton *m_transformMenuBtn = nullptr;
    IconLabelButton *m_renderMenuBtn = nullptr;
    Amethyst::ContextMenu *m_transformMenu = nullptr;
    Amethyst::ContextMenu *m_renderMenu = nullptr;
    NumberMenuItem *m_fieldOfViewItem = nullptr;
    NumberMenuItem *m_exposureItem = nullptr;
    OrientationGizmo *m_orientationGizmo = nullptr;
    ViewportOverlays m_overlays;

    Amethyst::RadioGroup m_gizmoOpGroup;
    Amethyst::RadioGroup m_gizmoSpaceGroup;
    Amethyst::RadioGroup m_cameraModeGroup;
    Amethyst::RadioGroup m_lightingModeGroup;
    Amethyst::RadioGroup m_probeDebugModeGroup;

    Amethyst::EventConnection m_gizmoOpGroupConn;
    Amethyst::EventConnection m_gizmoSpaceGroupConn;
    Amethyst::EventConnection m_cameraModeGroupConn;
    Amethyst::EventConnection m_lightingModeGroupConn;
    Amethyst::EventConnection m_probeDebugModeGroupConn;

    bool m_viewportHovered = false;

    std::unique_ptr<gizmo::TransformGizmo> m_transformGizmo;
    gizmo::TransformGizmo::Operation m_gizmoOperation = gizmo::TransformGizmo::OPERATION_TRANSLATE;
    gizmo::TransformGizmo::Space m_gizmoSpace = gizmo::TransformGizmo::SPACE_WORLD;

    glm::vec2 m_cursorInViewport{0.0f};
    bool m_gizmoPressed = false;
    bool m_gizmoReleased = false;
    bool m_gizmoCapturing = false;

    Rapture::ecs::EntityAccessor m_selectedEntity;
    Rapture::ecs::EntityAccessor m_previousSelectedEntity;
    Rapture::EventConnection m_selectionChangedConn;
    Amethyst::vec2 m_lastViewportSize = {};
    Amethyst::vec2 m_pendingViewportSize = {};
    float m_resizeStableTime = 0.0f;
    bool m_resizePending = false;

    Rapture::Viewport *m_viewport = nullptr;

    struct SlotImage {
        Amethyst::AmTextureId id = Amethyst::AM_INVALID_TEXTURE;
        Rapture::Texture *texture = nullptr;
    };
    std::vector<SlotImage> m_slotImages;
};

#endif // RAPTURE__VIEWPORT_PANEL_H
