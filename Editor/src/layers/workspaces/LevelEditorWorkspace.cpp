#include "LevelEditorWorkspace.h"

#include "layers/panels/ImagePreviewPanel.h"
#include "layers/panels/OutlinerPanel.h"
#include "layers/panels/PropertiesPanel.h"
#include "layers/panels/ViewportPanel.h"

LevelEditorWorkspace::LevelEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services, Rapture::Scene *scene,
                                           Rapture::Viewport *viewport)
{
    m_context.services = services;
    m_context.scene = scene;
    m_context.viewport = viewport;
    setupBase(tabs, "Level Editor");

    m_dockingLayer->name = "Editor Dock";

    Amethyst::TabBar *viewportTabBar = nullptr;
    Amethyst::TabBar *outlinerTabBar = nullptr;
    Amethyst::TabBar *propertiesTabBar = nullptr;

    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, 0.25f,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { outlinerTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, 0.65f,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { viewportTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { propertiesTabBar = &tb.component; }); });
            });

    if (viewportTabBar != nullptr) {
        viewportTabBar->addClass("panel-tab-bar");
        m_panels.push_back(std::make_unique<ViewportPanel>(viewportTabBar, m_context));
    }
    if (outlinerTabBar != nullptr) {
        outlinerTabBar->addClass("panel-tab-bar");
        m_panels.push_back(std::make_unique<OutlinerPanel>(outlinerTabBar, m_context));
    }
    if (propertiesTabBar != nullptr) {
        propertiesTabBar->addClass("panel-tab-bar");
        m_panels.push_back(std::make_unique<PropertiesPanel>(propertiesTabBar, m_context));
        m_panels.push_back(std::make_unique<ImagePreviewPanel>(propertiesTabBar, m_context, "Texture Viewer",
                                                               ImagePreviewMode::ASSET_PICKER));
    }

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Editor Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void LevelEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}
