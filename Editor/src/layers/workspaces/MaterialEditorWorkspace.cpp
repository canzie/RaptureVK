#include "MaterialEditorWorkspace.h"

#include "layers/panels/NodeEditorPanel.h"

MaterialEditorWorkspace::MaterialEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services)
{
    m_services = services;
    setupBase(tabs, "Material Editor");
    m_dockingLayer->name = "Material Editor Dock";

    Amethyst::TabBar *canvasTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer).panel([&](Amethyst::TabBarScope &tb) { canvasTabBar = &tb.component; });

    if (canvasTabBar != nullptr) {
        canvasTabBar->addClass("panel-tab-bar");
        m_panels.push_back(std::make_unique<NodeEditorPanel>(canvasTabBar, m_services));
    }

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Material Editor Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void MaterialEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}
