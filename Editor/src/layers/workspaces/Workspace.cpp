#include "Workspace.h"

#include "layers/EditorLayout.h"
#include "layers/panels/components/tab_layouts.h"
#include <components/ui_scope.h>
#include <memory>

void Workspace::onUpdate(float dt)
{
    for (auto &panel : m_panels) {
        panel->onUpdate(dt);
    }
}

void Workspace::setupBase(Amethyst::TabBarScope &tabs, std::string_view label)
{
    tabs.tab([this, label](Amethyst::TabScope &tab) {
        tab.label(iconTabLayoutScope(label));
        tab.content([this](Amethyst::FrameScope &f) {
            m_container = &f.component;
            m_container->addClass("background-primary");

            m_hotbar = m_container->add<Amethyst::Frame>();
            m_hotbar->setBaseProperties({
                .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_HOTBAR_HEIGHT),
            });
            m_hotbar->addClass("background-tertiary");

            m_dockingLayer = m_container->add<Amethyst::DockingLayer>();
            m_dockingLayer->setDisplayOrder(1);
            m_dockingLayer->innerSpacing = EDITOR_DOCK_INNER_SPACING;
            m_dockingLayer->absolutePosition = {0.0f, EDITOR_CONTENT_TOP + EDITOR_DOCK_SPACING};
            m_dockingLayer->markDirty();

            m_context.dockingLayer = m_dockingLayer;
        });
    });
}

void Workspace::addPanel(std::unique_ptr<Panel> panel, Amethyst::DockZone zone)
{
    panel->attach(m_dockingLayer->dockNewRegion(zone));

    Amethyst::Frame *root = panel->root();
    Panel *panelPtr = panel.get();
    panel->setContext(m_context);
    root->onDestroy.detachedOnce([this, panelPtr](Amethyst::Instance *) {
        std::erase_if(m_panels, [panelPtr](const std::unique_ptr<Panel> &p) { return p.get() == panelPtr; });
    });

    m_panels.push_back(std::move(panel));
}
