#ifndef RAPTURE__ASSET_DETAILS_PANEL_H
#define RAPTURE__ASSET_DETAILS_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "inspector/AssetEditorBase.h"
#include "inspector/PropertySections.h"
#include "panels/Panel.h"

#include <rapture/assets/asset_manager/AssetCommon.h>

#include <concepts>
#include <optional>

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
        if (T *editor = m_sections->ensure<T>(present)) {
            editor->setSubject(m_handle);
        }
    }

    void refresh();

  private:
    std::optional<PropertySectionList> m_sections;
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
};

#endif // RAPTURE__ASSET_DETAILS_PANEL_H
