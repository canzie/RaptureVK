#include "Workspace.h"

#include "layers/EditorLayout.h"
#include "layers/panels/components/tab_layouts.h"
#include <components/ui_scope.h>
#include <memory>

Workspace::~Workspace()
{
    m_teardown = true;
}

void Workspace::setMode(EditorMode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    onModeChanged.fire(mode);
}

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
        tab.content([this](Amethyst::FrameScope &f) { buildContainer(f.component); });
    });
}

void Workspace::setupBase(Amethyst::TabBar &tabBar, std::string_view label, std::string_view iconSvg)
{
    auto content = std::make_unique<Amethyst::Frame>();
    Amethyst::Frame *container = content.get();
    tabBar.addTab(std::move(content), iconTabLayout(label, iconSvg));
    buildContainer(*container);
}

void Workspace::buildContainer(Amethyst::Frame &container)
{
    m_container = &container;
    m_container->addClass("workspace");

    m_hotbar = m_container->add<Amethyst::Frame>();
    m_hotbar->setBaseProperties({
        .padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(EDITOR_HOTBAR_PADDING)},
        .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_HOTBAR_HEIGHT),
    });
    m_hotbar->addClass("workspace-hotbar");

    m_dockingLayer = m_container->add<Amethyst::DockingLayer>();
    m_dockingLayer->setDisplayOrder(1);
    m_dockingLayer->innerSpacing = EDITOR_DOCK_INNER_SPACING;
    m_dockingLayer->absolutePosition = {0.0f, EDITOR_CONTENT_TOP + EDITOR_DOCK_SPACING};
    m_dockingLayer->markDirty();

    m_context.dockingLayer = m_dockingLayer;
}

void Workspace::addHotbarSeparator(uint32_t layoutOrder)
{
    Amethyst::UIScope(*m_hotbar).frame({
        .classes = {"hotbar-separator"},
        .base =
            {
                .layoutOrder = layoutOrder,
                .size = Amethyst::UDim2(0.0f, EDITOR_HOTBAR_SEPARATOR_WIDTH, 1.0f, 0.0f),
            },
    });
}

void Workspace::addPanel(std::unique_ptr<Panel> panel, Amethyst::DockZone zone)
{
    panel->attach(m_dockingLayer->dockNewRegion(zone));

    Amethyst::Frame *root = panel->root();
    Panel *panelPtr = panel.get();
    panel->setContext(m_context);
    root->onDestroy.detachedOnce([this, panelPtr](Amethyst::Instance *) {
        if (m_teardown) {
            return;
        }
        std::erase_if(m_panels, [panelPtr](const std::unique_ptr<Panel> &p) { return p.get() == panelPtr; });
    });

    m_panels.push_back(std::move(panel));
}
