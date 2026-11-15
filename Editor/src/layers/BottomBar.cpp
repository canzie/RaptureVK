#include "layers/BottomBar.h"

#include "app/EditorConfig.h"
#include "panels/ContentBrowserPanel.h"

#include <components/common.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <modules/color.h>

static constexpr float BOTTOM_BAR_PADDING = 4.0f;
static constexpr float CONTENT_BROWSER_BUTTON_WIDTH = 150.0f;
static constexpr float SEARCHBAR_CONTAINER_WIDTH = 400.0f;

static bool s_workspaceContainsContentBrowserPanel(Workspace *workspace)
{
    if (workspace == nullptr) {
        return false;
    }

    for (const auto &panel : workspace->getPanels()) {
        if (dynamic_cast<ContentBrowserPanel *>(panel.get()) != nullptr) {
            return true;
        }
    }
    return false;
}

BottomBar::BottomBar(Amethyst::Window *window, const EditorContext &context) : m_editor(context), m_window(window)
{
    const float height = m_editor.config->getFloat(EDITOR_CONFIG_BOTTOM_BAR_HEIGHT);

    m_root = window->add<Amethyst::Frame>();
    m_root->name = "Bottom Bar";
    m_root->addClass("bottom-bar");
    m_root->setBaseProperties({
        .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -height),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, height),
    });

    auto rootScope = Amethyst::UIScope(*m_root);
    rootScope.textButton(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, CONTENT_BROWSER_BUTTON_WIDTH, 1.0f, 0.0f),
                },
            .label = "Content Browser",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_contentBrowserBtn = &b.component;
            m_contentBrowserBtn->addClass("bottom-bar-button");
            m_contentBrowserBtn->onMouseButton1ClickCb = [this]() {
                toggleContentBrowser();
                return Amethyst::EventResult::CONSUMED;
            };
        });

    rootScope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH + 4.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, SEARCHBAR_CONTAINER_WIDTH, 1.0f, 0.0f),
                },
        },
        [this](Amethyst::FrameScope &cont) {
            cont.component.addClass("bottom-bar");
            cont.frame(
                {
                    .base =
                        {
                            .anchorPoint = {0.5f, 0.5f},
                            .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
                            .size = Amethyst::UDim2::fromScale(0.95f, 0.8f),
                        },

                },
                [this](Amethyst::FrameScope &sb) {
                    sb.textInput({.base = {.size = Amethyst::UDim2::fromScale(1.0f)}, .placeholder = "COMMAND"},
                                 [this](Amethyst::TextInputScope &tis) { tis.component.addClass("searchbar"); });
                });
        });
    rootScope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH, 0.0f),
                    .size = Amethyst::UDim2(0.0f, 2.0f, 1.0f, 0.0f),
                },
            .style = {.backgroundColor = Amethyst::Color3::fromHex(0x181818)},
        },
        [](Amethyst::FrameScope &fs) { (void)fs; });

    rootScope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH + SEARCHBAR_CONTAINER_WIDTH + 2.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, 2.0f, 1.0f, 0.0f),
                },
            .style = {.backgroundColor = Amethyst::Color3::fromHex(0x181818)},
        },
        [](Amethyst::FrameScope &fs) { (void)fs; });

    setupContentBrowserToggle();
}

BottomBar::~BottomBar() = default;

void BottomBar::setupContentBrowserToggle()
{
    Amethyst::UIScope(*m_root).popup(
        {
            .base = {.size = Amethyst::UDim2::fromScale(0.7f, 0.45f)},
            .style = {.backgroundColor = Amethyst::Color3(0.13f), .borderPixelSize = 0.0f},
            .placement = Amethyst::PopupPlacement::ABOVE,
        },
        [this](Amethyst::PopupScope &p) { m_contentBrowserPopup = &p.component; });

    WorkspaceContext context = m_currWorkspace != nullptr ? m_currWorkspace->getContext() : WorkspaceContext{.editor = &m_editor};
    m_contentBrowserPanel = std::make_unique<ContentBrowserPanel>(context, [this]() { dockContentBrowser(); });
    m_contentBrowserPanel->attachTo(*m_contentBrowserPopup);
}

void BottomBar::dockContentBrowser()
{
    if (m_currWorkspace == nullptr) {
        return;
    }

    m_currWorkspace->addPanel(std::move(m_contentBrowserPanel), Amethyst::DockZone::BOTTOM);
    m_contentBrowserPanel = nullptr;
    m_contentBrowserPopup = nullptr;
}

void BottomBar::setCurrentWorkspace(Workspace *workspace)
{
    m_currWorkspace = workspace;
    if (m_contentBrowserPanel != nullptr && workspace != nullptr) {
        m_contentBrowserPanel->setContext(workspace->getContext());
    }
}

void BottomBar::toggleContentBrowser()
{
    if (m_contentBrowserPopup == nullptr || m_contentBrowserPanel == nullptr) {
        if (s_workspaceContainsContentBrowserPanel(m_currWorkspace)) {
            return;
        }
        setupContentBrowserToggle();
    }

    if (m_contentBrowserPopup->isOpen()) {
        m_contentBrowserPopup->close();
    } else {
        m_contentBrowserPopup->open(m_contentBrowserBtn);
    }
}
