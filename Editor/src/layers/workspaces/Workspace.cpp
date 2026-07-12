#include "Workspace.h"

#include "layers/EditorLayout.h"

void Workspace::onUpdate(float dt)
{
    for (auto &panel : m_panels) {
        panel->onUpdate(dt);
    }
}

void Workspace::setupBase(Amethyst::TabBarScope &tabs, std::string_view label)
{
    tabs.tab(label, [this](Amethyst::FrameScope &f) {
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
}
