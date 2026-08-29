#ifndef RAPTURE__SCRIPTING_WORKSPACE_H
#define RAPTURE__SCRIPTING_WORKSPACE_H

#include "Workspace.h"

#include <assets/asset_manager/AssetCommon.h>

#include <filesystem>
#include <string_view>
#include <unordered_map>

class TextEditorPanel;

class ScriptingWorkspace : public Workspace {
  public:
    ScriptingWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services);

    void saveLayout() override;

    static constexpr std::string_view staticKind() { return "scripting"; }

    void openScript(Rapture::AssetHandle handle);

    static Rapture::AssetHandle createScript(const std::filesystem::path &folder, std::string_view name);

  private:
    void setupHotbar();
    void createAndOpenScript();
    TextEditorPanel *shownEditor() const;
    void closeScript(Amethyst::Instance *content);

  private:
    Amethyst::TabBar *m_editorTabBar = nullptr;
    std::unordered_map<Rapture::AssetHandle, TextEditorPanel *> m_openScripts;
};

#endif // RAPTURE__SCRIPTING_WORKSPACE_H
