#include "ModuleEditorWorkspace.h"

#include "Icons.h"
#include "layers/panels/ModulePropertiesPanel.h"

#include <asset_manager/AssetManager.h>

static constexpr const char *DOCK_NAME = "Module Editor Dock";

ModuleEditorWorkspace::ModuleEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services,
                                             Rapture::AssetHandle handle)
    : m_handle(handle)
{
    m_context.services = services;

    // TODO: follow the asset's name once renaming reaches the workspaces an asset is open in
    setupBase(tabBar, Rapture::AssetManager::getAssetMetadata(handle).getName(), Icons::SVG_MODULE);

    m_dockingLayer->name = DOCK_NAME;
    m_dockingLayer->tabBarClasses = {"panel", "panel-tab"};

    Amethyst::TabBar *propertiesTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer).panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; });

    if (propertiesTabBar != nullptr) {
        auto panel = std::make_unique<ModulePropertiesPanel>(propertiesTabBar, m_context, m_handle);
        m_properties = panel.get();
        m_panels.push_back(std::move(panel));
    }

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get(DOCK_NAME)) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void ModuleEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}
