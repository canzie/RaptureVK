#include "Workspace.h"

#include "app/EditorConfig.h"
#include "ui/chrome/TabLabels.h"
#include "ui/controls/IconLabelButton.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <parsers/config/layout_config.h>
#include <rapture/core/utils/rp_assert.h>

static constexpr std::string_view WORKSPACE_CLASS = "workspace";
static constexpr std::string_view PANEL_TAB_CLASS = "panel-tab";
static constexpr std::string_view HOTBAR_CLASS = "workspace-hotbar";
static constexpr std::string_view HOTBAR_SEPARATOR_CLASS = "hotbar-separator";
static constexpr std::string_view HOTBAR_BUTTON_CLASS = "hotbar-button";
static constexpr std::string_view HOTBAR_ITEM_CLASS = "hotbar-item";

static constexpr float HOTBAR_LABEL_MARGIN = 10.0f;
static constexpr float HOTBAR_CONTROL_HEIGHT = 28.0f;

Workspace::Workspace(const EditorContext &context) : m_editor(context)
{
    m_context.editor = &m_editor;
    m_context.selection = &m_selection;
}

Workspace::~Workspace()
{
    destroyPanels();
}

void Workspace::destroyPanels()
{
    m_teardown = true;
    m_panels.clear();
}

const EditorConfig &Workspace::config() const
{
    RP_ASSERT(m_editor.config != nullptr, "a workspace was built without a config");
    return *m_editor.config;
}

void Workspace::attach(Amethyst::TabBar &tabBar)
{
    RP_ASSERT(m_container == nullptr, "a workspace can only be attached once");

    auto content = std::make_unique<Amethyst::Frame>();
    Amethyst::Frame *container = content.get();
    std::optional<Amethyst::Color4> iconColor = tabIconColor();
    if (iconColor.has_value()) {
        tabBar.addTab(std::move(content), iconTabLabel(tabLabel(), tabIcon(), *iconColor, config()));
    } else {
        tabBar.addTab(std::move(content), iconTabLabel(tabLabel(), tabIcon(), config()));
    }

    buildContainer(*container);
    buildHotbar();
    build();
    resolveLayout();
    restoreDockLayout();
}

void Workspace::buildContainer(Amethyst::Frame &container)
{
    m_container = &container;
    m_container->addClass(WORKSPACE_CLASS);

    m_hotbar = m_container->add<Amethyst::Frame>();
    m_hotbar->addClass(HOTBAR_CLASS);

    m_hotbarLayout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    m_hotbarLayout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    m_hotbarLayout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;

    m_contentDock = m_container->add<Amethyst::DockingLayer>();
    m_contentDock->setDisplayOrder(1);
    m_contentDock->name = dockLayoutName();
    m_contentDock->tabBarClasses = {std::string(PANEL_TAB_CLASS)};

    m_context.contentDock = m_contentDock;
}

void Workspace::resolveLayout()
{
    const float hotbarHeight = config().getFloat(EDITOR_CONFIG_HOTBAR_HEIGHT);

    m_hotbar->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, hotbarHeight),
    });

    for (const HotbarItem &item : m_hotbarItems) {
        resolveHotbarItem(item);
    }

    m_contentDock->innerSpacing = config().getFloat(EDITOR_CONFIG_DOCK_INNER_SPACING);
    placeContentDock();

    for (auto &panel : m_panels) {
        panel->resolveLayout();
    }
}

void Workspace::setContentArea(Amethyst::vec2 position, Amethyst::vec2 size)
{
    m_contentAreaPosition = position;
    m_contentAreaSize = size;

    if (m_contentDock != nullptr) {
        placeContentDock();
    }
}

void Workspace::placeContentDock()
{
    const float hotbarHeight = config().getFloat(EDITOR_CONFIG_HOTBAR_HEIGHT);
    const float outerSpacing = config().getFloat(EDITOR_CONFIG_DOCK_OUTER_SPACING);

    m_contentDock->absolutePosition = {m_contentAreaPosition.x, m_contentAreaPosition.y + hotbarHeight + outerSpacing};
    m_contentDock->absoluteSize = {m_contentAreaSize.x, m_contentAreaSize.y - hotbarHeight - 2.0f * outerSpacing};
    m_contentDock->markDirty();
}

void Workspace::adoptPanel(std::unique_ptr<Panel> panel, Amethyst::TabBar *tabBar)
{
    RP_ASSERT(tabBar != nullptr, "a panel needs a bar to be docked into");

    panel->setContext(m_context);
    panel->assignId(m_nextPanelId++);
    if (panel->root() == nullptr) {
        panel->attach(tabBar);
    } else {
        panel->moveTo(tabBar);
    }
    m_panels.push_back(std::move(panel));

    tabBar->onSelectionChanged = [this](int32_t) {
        if (!m_teardown) {
            refreshShownPanels();
        }
    };
    refreshShownPanels();
}

void Workspace::refreshShownPanels()
{
    for (auto &panel : m_panels) {
        Amethyst::Frame *root = panel->root();
        if (root == nullptr || root->parent == nullptr) {
            continue;
        }

        auto *tabBar = root->parent->as<Amethyst::TabBar>();
        panel->setShown(m_active && tabBar != nullptr && tabBar->getSelectedContent() == root);
    }
}

void Workspace::setActive(bool active)
{
    if (m_active == active) {
        return;
    }

    m_active = active;
    refreshShownPanels();
}

void Workspace::onUpdate(float dt)
{
    if (!m_active) {
        return;
    }

    for (auto &panel : m_panels) {
        panel->onUpdate(dt);
    }
}

void Workspace::setMode(EditorMode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    onModeChanged.fire(mode);
}

void Workspace::resolveHotbarItem(const HotbarItem &item) const
{
    if (item.kind == HotbarItemKind::SEPARATOR) {
        const float width = config().getFloat(EDITOR_CONFIG_HOTBAR_SEPARATOR_WIDTH);
        item.island->setBaseProperties({.size = Amethyst::UDim2(0.0f, width, 1.0f, 0.0f)});
        return;
    }

    const float islandPadding = config().getFloat(EDITOR_CONFIG_HOTBAR_PADDING);
    item.island->setBaseProperties({
        .automaticSize = Amethyst::AutomaticSize::X,
        .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(islandPadding), .left = Amethyst::UDim::fromOffset(islandPadding)},
        .size = Amethyst::UDim2::fromScale(0.0f, 1.0f),
    });

    Amethyst::BasePropertiesArgs control = {
        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
        .position = Amethyst::UDim2::fromScale(0.0f, 0.5f),
        .size = Amethyst::UDim2::fromOffset(0.0f, HOTBAR_CONTROL_HEIGHT),
    };
    switch (item.kind) {
    case HotbarItemKind::FIXED:
        control.size = Amethyst::UDim2::fromOffset(item.width, item.height);
        break;
    case HotbarItemKind::LABELLED:
        control.automaticSize = Amethyst::AutomaticSize::X;
        control.padding =
            Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(HOTBAR_LABEL_MARGIN), .left = Amethyst::UDim::fromOffset(HOTBAR_LABEL_MARGIN)};
        break;
    case HotbarItemKind::ICON_LABELLED:
    case HotbarItemKind::SEPARATOR:
        break;
    }
    item.control->setBaseProperties(control);
}

Amethyst::Frame *Workspace::addHotbarIsland()
{
    Amethyst::Frame *island = nullptr;
    Amethyst::UIScope(*m_hotbar).frame(
        {
            .base = {.layoutOrder = static_cast<uint32_t>(m_hotbarItems.size())},
            .style = {.backgroundTransparency = 1.0f},
        },
        [&](Amethyst::FrameScope &f) { island = &f.component; });
    return island;
}

Amethyst::TextButton *Workspace::addHotbarButton(std::string_view label, std::function<void()> onClick)
{
    Amethyst::Frame *island = addHotbarIsland();

    Amethyst::TextButton *button = nullptr;
    Amethyst::UIScope(*island).textButton(
        {
            .classes = {std::string(HOTBAR_BUTTON_CLASS)},
            .text =
                {
                    .textXAlignment = Amethyst::TextXAlignment::CENTER,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = std::string(label),
        },
        [&](Amethyst::TextButtonScope &b) {
            button = &b.component;
            b.component.track(b.component.onMouseButton1ClickCb.connect([onClick = std::move(onClick)]() { onClick(); }));
        });

    const HotbarItem &item = m_hotbarItems.emplace_back(island, button, 0.0f, HotbarItemKind::LABELLED);
    resolveHotbarItem(item);

    return button;
}

IconLabelButton *Workspace::addHotbarButton(std::string_view label, std::string_view iconSvg, std::function<void()> onClick)
{
    return addHotbarButton(label, iconSvg, HOTBAR_BUTTON_CLASS, std::move(onClick));
}

IconLabelButton *Workspace::addHotbarButton(std::string_view label, std::string_view iconSvg, std::string_view styleClass,
                                            std::function<void()> onClick)
{
    Amethyst::Frame *island = addHotbarIsland();

    auto *button = island->add<IconLabelButton>(iconSvg, label, std::vector<std::string>{std::string(styleClass)});
    button->track(button->onMouseButton1ClickCb.connect([onClick = std::move(onClick)]() { onClick(); }));

    const HotbarItem &item = m_hotbarItems.emplace_back(island, button, 0.0f, HotbarItemKind::ICON_LABELLED);
    resolveHotbarItem(item);

    return button;
}

void Workspace::addHotbarSeparator()
{
    Amethyst::Frame *line = nullptr;
    Amethyst::UIScope(*m_hotbar).frame(
        {
            .classes = {std::string(HOTBAR_SEPARATOR_CLASS)},
            .base = {.layoutOrder = static_cast<uint32_t>(m_hotbarItems.size())},
        },
        [&](Amethyst::FrameScope &f) { line = &f.component; });

    const HotbarItem &item = m_hotbarItems.emplace_back(line, nullptr, 0.0f, HotbarItemKind::SEPARATOR);
    resolveHotbarItem(item);
}

Amethyst::Frame *Workspace::addHotbarItem(float width, const std::function<void(Amethyst::UIScope &)> &build)
{
    return addHotbarItem(Amethyst::vec2(width, HOTBAR_CONTROL_HEIGHT), build);
}

Amethyst::Frame *Workspace::addHotbarItem(Amethyst::vec2 size, const std::function<void(Amethyst::UIScope &)> &build)
{
    Amethyst::Frame *island = addHotbarIsland();

    Amethyst::Frame *slot = nullptr;
    Amethyst::UIScope(*island).frame({.classes = {std::string(HOTBAR_ITEM_CLASS)}}, [&](Amethyst::FrameScope &f) {
        slot = &f.component;
        build(f);
    });

    const HotbarItem &item = m_hotbarItems.emplace_back(island, slot, size.x, HotbarItemKind::FIXED, size.y);
    resolveHotbarItem(item);

    return slot;
}

void Workspace::saveDockLayout() const
{
    if (m_contentDock == nullptr) {
        return;
    }

    Amethyst::LayoutConfig::instance().set(m_contentDock->name, Amethyst::ConfigEntry(m_contentDock->saveConfig()));
}

void Workspace::restoreDockLayout()
{
    const Amethyst::ConfigEntry *entry = Amethyst::LayoutConfig::instance().get(m_contentDock->name);
    if (entry == nullptr || entry->type != Amethyst::ConfigType::DOCK_LAYOUT) {
        return;
    }

    m_contentDock->applyConfig(entry->dockLayout);
}
