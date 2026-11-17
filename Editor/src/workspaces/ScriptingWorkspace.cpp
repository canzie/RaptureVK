#include "ScriptingWorkspace.h"

#include "panels/TextEditorPanel.h"
#include "ui/AssetVisuals.h"

#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetImportConfig.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/asset_manager/AssetRegistry.h>
#include <rapture/core/utils/Log.h>
#include <rapture/scene/Project.h>

#include <fstream>

static constexpr std::string_view LUA_EXTENSION = ".lua";

static bool s_scriptNameIsTaken(const std::filesystem::path &folder, const std::string &name)
{
    std::string assetExtension(Rapture::AssetRegistry::fileExtension(Rapture::ASSET_SCRIPT));
    return std::filesystem::exists(folder / (name + assetExtension)) ||
           std::filesystem::exists(folder / (name + std::string(LUA_EXTENSION)));
}

static std::string s_uniqueScriptName(const std::filesystem::path &folder)
{
    std::string name = "Script";
    for (uint32_t suffix = 2; s_scriptNameIsTaken(folder, name); suffix++) {
        name = "Script " + std::to_string(suffix);
    }
    return name;
}

ScriptingWorkspace::ScriptingWorkspace(const EditorContext &context) : Workspace(context) {}

std::string_view ScriptingWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_SCRIPT);
}

void ScriptingWorkspace::build()
{
    Amethyst::DockScope(*m_contentDock).panel([&](Amethyst::TabBarScope &tb) { m_editorTabBar = &tb.component; });
    m_editorTabBar->onTabClosed = [this](Amethyst::Instance *content) { closeScript(content); };
}

void ScriptingWorkspace::buildHotbar()
{
    addHotbarButton("New Script", [this]() { createAndOpenScript(); });
    addHotbarButton("Save", [this]() {
        if (TextEditorPanel *editor = shownEditor()) {
            editor->save();
        }
    });
}

Rapture::AssetHandle ScriptingWorkspace::createScript(const std::filesystem::path &folder, std::string_view name)
{
    const std::filesystem::path source = folder / (std::string(name) + std::string(LUA_EXTENSION));
    if (std::filesystem::exists(source)) {
        RP_ERROR("'{}' already exists", source.string());
        return Rapture::INVALID_ASSET_HANDLE;
    }

    if (!std::ofstream(source, std::ios::binary)) {
        RP_ERROR("Could not create '{}'", source.string());
        return Rapture::INVALID_ASSET_HANDLE;
    }

    Rapture::AssetRef script = Rapture::AssetManager::importAsset(Rapture::AssetImportFileRequest{
        .source = source,
        .output = folder,
        .name = std::string(name),
    });

    if (!script) {
        RP_ERROR("Could not create a script named '{}' in '{}'", name, folder.string());
        return Rapture::INVALID_ASSET_HANDLE;
    }

    return script->handle();
}

void ScriptingWorkspace::createAndOpenScript()
{
    const std::filesystem::path folder = Rapture::Application::getInstance().getProject().getContentDirectory();
    Rapture::AssetHandle script = createScript(folder, s_uniqueScriptName(folder));
    if (script != Rapture::INVALID_ASSET_HANDLE) {
        openScript(script);
    }
}

void ScriptingWorkspace::openScript(Rapture::AssetHandle handle)
{
    if (m_editorTabBar == nullptr) {
        return;
    }

    auto open = m_openScripts.find(handle);
    if (open != m_openScripts.end()) {
        m_editorTabBar->select(open->second->root());
        return;
    }

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(handle);
    TextEditorPanel *editor = addPanel(std::make_unique<TextEditorPanel>(m_context, metadata.getName()), m_editorTabBar);
    editor->openScript(handle);

    m_openScripts.emplace(handle, editor);
    m_editorTabBar->select(editor->root());
}

TextEditorPanel *ScriptingWorkspace::shownEditor() const
{
    if (m_editorTabBar == nullptr) {
        return nullptr;
    }

    Amethyst::Instance *shown = m_editorTabBar->getSelectedContent();
    for (const auto &entry : m_openScripts) {
        if (entry.second->root() == shown) {
            return entry.second;
        }
    }
    return nullptr;
}

void ScriptingWorkspace::closeScript(Amethyst::Instance *content)
{
    std::erase_if(m_openScripts, [content](const auto &entry) { return entry.second->root() == content; });
    std::erase_if(m_panels, [content](const std::unique_ptr<Panel> &panel) { return panel->root() == content; });
}
