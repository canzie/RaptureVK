#include "Workspace.h"

#include "app/EditorConfig.h"
#include "ui/chrome/TabLabels.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <core/utils/rp_assert.h>
#include <parsers/config/layout_config.h>

static constexpr std::string_view WORKSPACE_CLASS = "workspace";
static constexpr std::string_view PANEL_TAB_CLASS = "panel-tab";
static constexpr std::string_view HOTBAR_CLASS = "workspace-hotbar";
static constexpr std::string_view HOTBAR_SEPARATOR_CLASS = "hotbar-separator";
static constexpr std::string_view HOTBAR_BUTTON_CLASS = "generic-text-button";

static constexpr float HOTBAR_LABEL_MARGIN = 12.0f;

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
    tabBar.addTab(std::move(content), iconTabLabel(tabLabel(), tabIcon(), config()));

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
    const float hotbarPadding = config().getFloat(EDITOR_CONFIG_HOTBAR_PADDING);

    m_hotbar->setBaseProperties({
        .padding = Amethyst::UDim4::fromOffset(hotbarPadding),
        .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, hotbarHeight),
    });

    m_hotbarLayout->innerPadding = Amethyst::UDim::fromOffset(hotbarPadding);

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
    float width = 0.0f;
    switch (item.kind) {
    case HotbarItemKind::FIXED:
        width = config().scale(item.width);
        break;
    case HotbarItemKind::LABELLED:
        width = labelWidth(*item.object->as<Amethyst::TextButton>()) + 2.0f * config().scale(HOTBAR_LABEL_MARGIN);
        break;
    case HotbarItemKind::SEPARATOR:
        width = config().getFloat(EDITOR_CONFIG_HOTBAR_SEPARATOR_WIDTH);
        break;
    }

    // the bar's own padding is what keeps an item off its edges
    item.object->setBaseProperties({.size = Amethyst::UDim2(0.0f, width, 1.0f, 0.0f)});
}

float Workspace::labelWidth(const Amethyst::TextButton &button) const
{
    RP_ASSERT(m_editor.amethyst != nullptr, "a workspace was built without the ui context");

    const uint32_t fontSize = static_cast<uint32_t>(button.getTextStyleProperties().fontSize);
    return m_editor.amethyst->getTextProcessor().measureTextAtlas(button.getText(), {}, fontSize).x;
}

Amethyst::TextButton *Workspace::addHotbarButton(std::string_view label, std::function<void()> onClick)
{
    Amethyst::TextButton *button = nullptr;
    Amethyst::UIScope(*m_hotbar).textButton(
        {
            .classes = {std::string(HOTBAR_BUTTON_CLASS)},
            .base = {.layoutOrder = static_cast<uint32_t>(m_hotbarItems.size())},
            .text =
                {
                    .textXAlignment = Amethyst::TextXAlignment::CENTER,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = std::string(label),
        },
        [&](Amethyst::TextButtonScope &b) {
            button = &b.component;
            b.component.onMouseButton1ClickCb = [onClick = std::move(onClick)]() {
                onClick();
                return Amethyst::EventResult::CONSUMED;
            };
        });

    const HotbarItem &item = m_hotbarItems.emplace_back(button, 0.0f, HotbarItemKind::LABELLED);
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

    const HotbarItem &item = m_hotbarItems.emplace_back(line, 0.0f, HotbarItemKind::SEPARATOR);
    resolveHotbarItem(item);
}

Amethyst::Frame *Workspace::addHotbarItem(float width, const std::function<void(Amethyst::UIScope &)> &build)
{
    Amethyst::Frame *slot = nullptr;
    Amethyst::UIScope(*m_hotbar).frame(
        {
            .base = {.layoutOrder = static_cast<uint32_t>(m_hotbarItems.size())},
            .style = {.backgroundTransparency = 1.0f},
        },
        [&](Amethyst::FrameScope &f) {
            slot = &f.component;
            build(f);
        });

    const HotbarItem &item = m_hotbarItems.emplace_back(slot, width, HotbarItemKind::FIXED);
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
