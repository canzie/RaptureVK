#ifndef RAPTURE__ASSET_DETAILS_PANEL_H
#define RAPTURE__ASSET_DETAILS_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "inspector/AssetEditorBase.h"
#include "inspector/AssetEditors.h"
#include "inspector/PropertySections.h"
#include "panels/Panel.h"

#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/core/events/EventSignal.h>

#include <concepts>
#include <optional>

namespace Rapture {
class Bone3D;
} // namespace Rapture

/**
 * @brief The fields of the one asset a workspace has open.
 *
 * What it shows comes from the asset's type, so the workspaces share it rather than each carrying a
 * panel of their own.
 */
class AssetDetailsPanel : public Panel {
  public:
    std::string_view typeName() const override { return "AssetDetailsPanel"; }
    AssetDetailsPanel(const WorkspaceContext &context, Rapture::AssetHandle handle);

    /**
     * @brief Builds the panel for a mesh whose scale and up axis can be edited for baking
     * @param context The workspace the panel is built against
     * @param handle The asset shown
     * @param bakeTransform The transform the Transform section edits, which outlives the panel
     */
    AssetDetailsPanel(const WorkspaceContext &context, Rapture::AssetHandle handle, MeshBakeTransform &bakeTransform);

    void onUpdate(float dt) override;

  protected:
    void build(Amethyst::UIScope &scope) override;

  private:
    void setupSectionView();

    /**
     * @brief Ensures the section for T and points it at the open asset.
     * @tparam T An AssetEditorBase-derived editor type.
     * @param present Whether the open asset has the facet this editor edits.
     */
    template <std::derived_from<AssetEditorBase> T>
    void ensure(bool present)
    {
        m_sections->ensurePrepared<T>(present, [this](T &editor) { editor.setSubject(m_handle); });
    }

    void refresh();

    /**
     * @brief The bone selected in the workspace
     * @return The bone, or nullptr when nothing or something other than a bone is selected
     */
    Rapture::Bone3D *selectedBone() const;

  private:
    std::optional<PropertySectionList> m_sections;
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
    MeshBakeTransform *m_bakeTransform = nullptr;
    Rapture::EventConnection m_selectionChangedConn;
    bool m_selectionChanged = false;
};

#endif // RAPTURE__ASSET_DETAILS_PANEL_H
