#ifndef RAPTURE__SCRIPTING_WORKSPACE_H
#define RAPTURE__SCRIPTING_WORKSPACE_H

#include "Workspace.h"

#include <assets/asset_manager/AssetCommon.h>

#include <filesystem>
#include <string_view>
#include <unordered_map>

class TextEditorPanel;

/**
 * @brief The workspace the project's scripts are written in, holding as many at once as are opened
 */
class ScriptingWorkspace : public Workspace {
  public:
    static constexpr std::string_view LABEL = "Scripting";

    explicit ScriptingWorkspace(const EditorContext &context);

    std::string_view tabLabel() const override { return LABEL; }
    std::string_view tabIcon() const override;

    /**
     * @brief Opens a script in a tab of its own, focusing the tab it is already open in
     * @param handle The script to open
     */
    void openScript(Rapture::AssetHandle handle);

    static Rapture::AssetHandle createScript(const std::filesystem::path &folder, std::string_view name);

  protected:
    void build() override;
    void buildHotbar() override;
    std::string dockLayoutName() const override { return "Scripting Dock"; }

  private:
    void createAndOpenScript();
    TextEditorPanel *shownEditor() const;
    void closeScript(Amethyst::Instance *content);

  private:
    Amethyst::TabBar *m_editorTabBar = nullptr;
    std::unordered_map<Rapture::AssetHandle, TextEditorPanel *> m_openScripts;
};

#endif // RAPTURE__SCRIPTING_WORKSPACE_H
