#include "MaterialEditorWorkspace.h"

#include "layers/panels/NodeEditorPanel.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

MaterialEditorWorkspace::MaterialEditorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services)
{
    m_context.services = services;
    setupBase(tabs, "Material Editor");
    m_dockingLayer->name = "Material Editor Dock";

    Amethyst::TabBar *canvasTabBar = nullptr;
    Amethyst::DockScope(*m_dockingLayer).panel([&](Amethyst::TabBarScope &tb) { canvasTabBar = &tb.component; });

    if (canvasTabBar != nullptr) {
        canvasTabBar->addClass("panel-tab-bar");
        auto panel = std::make_unique<NodeEditorPanel>(canvasTabBar, m_context);
        m_nodeEditor = panel.get();
        m_panels.push_back(std::move(panel));
    }

    setupHotbar();

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Material Editor Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void MaterialEditorWorkspace::setupHotbar()
{
    if (m_hotbar == nullptr) {
        return;
    }

    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(6.0f);

    Amethyst::UIScope(*m_hotbar)
        .textButton({.base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(80.0f, 28.0f)},
                     .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER,
                              .textYAlignment = Amethyst::TextYAlignment::CENTER},
                     .label = "Compile"},
                    [this](Amethyst::TextButtonScope &b) {
                        b.component.onMouseButton1ClickCb = [this]() {
                            if (m_nodeEditor != nullptr) {
                                m_nodeEditor->compileGraph();
                            }
                            return Amethyst::EventResult::CONSUMED;
                        };
                    });
}

void MaterialEditorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}
