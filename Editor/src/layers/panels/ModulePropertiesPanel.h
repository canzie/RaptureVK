#ifndef RAPTURE__MODULE_PROPERTIES_PANEL_H
#define RAPTURE__MODULE_PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "asset_manager/AssetCommon.h"
#include "asset_manager/AssetHandle.h"
#include "layers/panels/Panel.h"
#include "layers/panels/components/property_sections.h"
#include "layers/panels/module_editors/ModuleEditorBase.h"

#include <concepts>
#include <optional>

/**
 * @brief Edits the module a module asset holds, one section per class in its ancestry.
 */
class ModulePropertiesPanel : public Panel {
  public:
    ModulePropertiesPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, Rapture::AssetHandle module);
    ~ModulePropertiesPanel() override;
    ModulePropertiesPanel(const ModulePropertiesPanel &) = delete;
    ModulePropertiesPanel &operator=(const ModulePropertiesPanel &) = delete;

    void onUpdate(float dt) override;

  private:
    void setupPlaceholder(void);
    void setupModuleView(void);

    /**
     * @brief Opens the module a handle names and builds its sections.
     * @param handle The module asset to edit.
     */
    void loadModule(Rapture::AssetHandle handle);

    /**
     * @brief Ensures the section for T and points it at the open module.
     * @tparam T A ModuleEditorBase-derived editor type.
     * @param present Whether the open module derives from the class this editor edits.
     */
    template <std::derived_from<ModuleEditorBase> T>
    void ensure(bool present)
    {
        if (T *editor = m_sections->ensure<T>(present)) {
            editor->module = m_module;
        }
    }

    void refresh(void);
    void showPlaceholder(void);

  private:
    Amethyst::TextLabel *m_placeholderText = nullptr;
    std::optional<PropertySectionList> m_sections;

    Rapture::AssetRef m_moduleRef;
    Rapture::ModuleClass *m_module = nullptr;
};

#endif // RAPTURE__MODULE_PROPERTIES_PANEL_H
