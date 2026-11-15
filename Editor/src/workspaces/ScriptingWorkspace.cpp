#include "ScriptingWorkspace.h"

#include "panels/TextEditorPanel.h"
#include "ui/AssetVisuals.h"

#include <app/Application.h>
#include <assets/asset_manager/AssetImportData.h>
#include <assets/asset_manager/AssetManager.h>
#include <assets/asset_manager/AssetRegistry.h>
#include <core/utils/Log.h>
#include <scene/Project.h>

static std::string s_uniqueScriptName(const std::filesystem::path &folder)
{
    std::string extension(Rapture::AssetRegistry::fileExtension(Rapture::ASSET_SCRIPT));
    std::string name = "Script";
    for (uint32_t suffix = 2; std::filesystem::exists(folder / (name + extension)); suffix++) {
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
    Rapture::AssetRef script = Rapture::AssetManager::importAsset(Rapture::AssetImportDataRequest{
        .data = Rapture::ScriptImportData{},
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
