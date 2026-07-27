#ifndef RAPTURE__VIEWPORT_PANEL_H
#define RAPTURE__VIEWPORT_PANEL_H

#include <amethyst/Amethyst.h>
#include <amethyst__vk13_glfw.h>
#include <components/context_menu.h>
#include <components/radio_button.h>
#include <components/ui_scope.h>
#include <components/widgets/gizmo.h>

#include "layers/panels/Panel.h"
#include "scenes/entities/Entity.h"

#include <cstdint>
#include <math/math.h>
#include <memory>
#include <vector>

namespace Rapture {
class CameraController;
class Viewport;
class Texture;
} // namespace Rapture

enum ViewportLightingMode {
    VLM_LIT,
    VLM_RAW_IRRADIANCE,
    VLM_INDIRECT_LIGHTING,
    VLM_DIRECT_LIGHTING,
    VLM_SHADOW_CASCADES,
    VLM_NORMALS,
    VLM_METTALIC,
    VLM_ROUGHNESS,
    VLM_AO,
    VLM_ALBEDO,
    VLM_MATERIAL_ID,
    VLM_DEPTH,
    VLM_COUNT
};

class ViewportPanel : public Panel {
  public:
    ViewportPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context);
    ~ViewportPanel();
    ViewportPanel(const ViewportPanel &) = delete;
    ViewportPanel &operator=(const ViewportPanel &) = delete;
    ViewportPanel(ViewportPanel &&) = delete;
    ViewportPanel &operator=(ViewportPanel &&) = delete;

    void setViewportImage(Amethyst::AmTextureId imageId);
    void onUpdate(float dt) override;

    Amethyst::GizmoOperation getGizmoOperation() const { return m_gizmoOperation; }
    void setGizmoOperation(Amethyst::GizmoOperation op)
    {
        m_gizmoOperation = op;
        m_gizmoOpGroup.value = static_cast<int32_t>(op);
    }
    Amethyst::GizmoSpace getGizmoSpace() const { return m_gizmoSpace; }
    void setGizmoSpace(Amethyst::GizmoSpace space)
    {
        m_gizmoSpace = space;
        m_gizmoSpaceGroup.value = static_cast<int32_t>(space);
    }

  private:
    void updateGizmo(void);
    void setupOverlayButtons(void);
    void buildTransformMenu(void);
    void buildRenderMenu(void);
    void applyLightingMode(ViewportLightingMode mode);
    Rapture::CameraController *cameraController(void) const;
    void updateViewportImage(void);

  private:
    Amethyst::ImageLabel *m_viewportImage = nullptr;

    Amethyst::EventConnection m_viewportImageDestroyConn;

    Amethyst::TextButton *m_transformMenuBtn = nullptr;
    Amethyst::TextButton *m_renderMenuBtn = nullptr;
    Amethyst::ContextMenu *m_transformMenu = nullptr;
    Amethyst::ContextMenu *m_renderMenu = nullptr;

    Amethyst::RadioGroup m_gizmoOpGroup;
    Amethyst::RadioGroup m_gizmoSpaceGroup;
    Amethyst::RadioGroup m_cameraModeGroup;
    Amethyst::RadioGroup m_lightingModeGroup;

    Amethyst::EventConnection m_gizmoOpGroupConn;
    Amethyst::EventConnection m_gizmoSpaceGroupConn;
    Amethyst::EventConnection m_cameraModeGroupConn;
    Amethyst::EventConnection m_lightingModeGroupConn;

    bool m_viewportHovered = false;

    std::unique_ptr<Amethyst::Gizmo> m_gizmo;
    Amethyst::GizmoOperation m_gizmoOperation = Amethyst::GizmoOperation::TRANSLATE;
    Amethyst::GizmoSpace m_gizmoSpace = Amethyst::GizmoSpace::WORLD;

    std::shared_ptr<Rapture::Entity> m_selectedEntity;
    std::shared_ptr<Rapture::Entity> m_previousSelectedEntity;
    size_t m_entitySelectedListenerId = 0;
    size_t m_entityDeselectedListenerId = 0;
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
