#ifndef RAPTURE__MODULE_EDITOR_WORKSPACE_H
#define RAPTURE__MODULE_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <asset_manager/AssetCommon.h>

class ModulePropertiesPanel;

/**
 * @brief The workspace one open module asset is edited in.
 */
class ModuleEditorWorkspace : public Workspace {
  public:
    /**
     * @brief Opens a module asset in a tab of its own.
     * @param tabBar The workspace bar the tab is appended to.
     * @param services Services the panels are built against.
     * @param handle The module asset this workspace edits.
     */
    ModuleEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle);

    void saveLayout() override;

  private:
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
    ModulePropertiesPanel *m_properties = nullptr;
};

#endif // RAPTURE__MODULE_EDITOR_WORKSPACE_H
