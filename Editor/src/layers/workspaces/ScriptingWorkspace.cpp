#include "ScriptingWorkspace.h"

#include "layers/panels/TextEditorPanel.h"
#include "layers/panels/components/asset_visuals.h"

#include <app/Application.h>
#include <assets/asset_manager/AssetImportData.h>
#include <assets/asset_manager/AssetManager.h>
#include <assets/asset_manager/AssetRegistry.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
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

ScriptingWorkspace::ScriptingWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services) : Workspace(staticKind())
{
    m_context.services = services;
    setupBase(tabBar, "Scripting", Asset_iconForType(Rapture::ASSET_SCRIPT));

    m_dockingLayer->name = "Scripting Dock";
    m_dockingLayer->tabBarClasses = {"panel-tab"};

    Amethyst::DockScope(*m_dockingLayer).panel([&](Amethyst::TabBarScope &tb) { m_editorTabBar = &tb.component; });

    if (m_editorTabBar != nullptr) {
        m_editorTabBar->onTabClosed = [this](Amethyst::Instance *content) { closeScript(content); };
    }

    setupHotbar();

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Scripting Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void ScriptingWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(6.0f);

    Amethyst::UIScope hotbar(*m_hotbar);

    hotbar.textButton(
        {.base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(90.0f, 28.0f)},
         .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER, .textYAlignment = Amethyst::TextYAlignment::CENTER},
         .label = "New Script"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                createAndOpenScript();
                return Amethyst::EventResult::CONSUMED;
            };
        });

    hotbar.textButton(
        {.base = {.layoutOrder = 1, .size = Amethyst::UDim2::fromOffset(64.0f, 28.0f)},
         .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER, .textYAlignment = Amethyst::TextYAlignment::CENTER},
         .label = "Save"},
        [this](Amethyst::TextButtonScope &b) {
            b.component.onMouseButton1ClickCb = [this]() {
                if (TextEditorPanel *editor = shownEditor()) {
                    editor->save();
                }
                return Amethyst::EventResult::CONSUMED;
            };
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
    auto panel = std::make_unique<TextEditorPanel>(m_editorTabBar, m_context, metadata.getName());
    panel->openScript(handle);

    TextEditorPanel *editor = panel.get();
    m_openScripts.emplace(handle, editor);
    m_panels.push_back(std::move(panel));
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

void ScriptingWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}
