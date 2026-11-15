#include "Panel.h"

#include "app/EditorConfig.h"
#include "ui/chrome/TabLabels.h"

#include <rapture/core/utils/rp_assert.h>

static constexpr std::string_view PANEL_CLASS = "panel";

Panel::Panel(std::string name, const WorkspaceContext &context)
    : m_name(std::move(name)), m_context(context), m_selection(context.selection)
{
}

Panel::~Panel()
{
    if (m_root == nullptr || m_root->parent == nullptr) {
        return;
    }

    if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
        tabBar->removeTab(m_root);
    }
}

const EditorConfig &Panel::config() const
{
    RP_ASSERT(m_context.editor != nullptr && m_context.editor->config != nullptr, "a panel was built without a config");
    return *m_context.editor->config;
}

void Panel::attach(Amethyst::TabBar *tabBar)
{
    RP_ASSERT(tabBar != nullptr, "a panel cannot be attached to no bar");

    tabBar->addTab(buildRoot(), iconTabLabel(m_name, m_icon, config()));
}

void Panel::attachTo(Amethyst::Instance &parent)
{
    std::unique_ptr<Amethyst::Frame> root = buildRoot();
    root->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
    parent.addChild(std::move(root));
}

std::unique_ptr<Amethyst::Frame> Panel::buildRoot()
{
    RP_ASSERT(m_root == nullptr, "a panel can only be attached once, use moveTo to redock it");

    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_root->name = m_name;
    m_root->addClass(PANEL_CLASS);
    m_root->setBaseProperties({.clipsDescendants = true});
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        onRootDestroyed();
    });

    Amethyst::UIScope scope(*m_root);
    build(scope);
    resolveLayout();

    return root;
}

void Panel::moveTo(Amethyst::TabBar *tabBar)
{
    if (m_root == nullptr || m_root->parent == nullptr || tabBar == nullptr) {
        return;
    }

    if (m_root->parent->as<Amethyst::TabBar>() == tabBar) {
        return;
    }

    tabBar->addTab(m_root->parent->removeChild(m_root), iconTabLabel(m_name, m_icon, config()));
}

void Panel::setContext(const WorkspaceContext &context)
{
    m_context = context;
    m_selection = context.selection;
}

void Panel::setShown(bool shown)
{
    if (m_shown == shown) {
        return;
    }

    m_shown = shown;
    if (m_shown) {
        onShown();
    } else {
        onHidden();
    }
}
