#include "layers/BottomBar.h"

#include "layers/EditorLayout.h"
#include "layers/panels/ContentBrowserPanel.h"

#include <scenes/World.h>

#include <components/common.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <modules/color.h>

static constexpr float BOTTOM_BAR_PADDING = 4.0f;
static constexpr float CONTENT_BROWSER_BUTTON_WIDTH = 150.0f;
static constexpr float SEARCHBAR_CONTAINER_WIDTH = 400.0f;

static Rapture::Scene *s_workspaceScene(Workspace *workspace)
{
    return workspace != nullptr ? workspace->getContext().scene : nullptr;
}

static bool s_workspaceContainsContentBrowserPanel(Workspace *workspace)
{
    if (workspace == nullptr) return false;

    for (const auto &panel : workspace->getPanels()) {
        if (dynamic_cast<ContentBrowserPanel *>(panel.get()) != nullptr) {
            return true;
        }
    }
    return false;
}

BottomBar::BottomBar(Amethyst::Window *window, const PanelServices &services) : m_services(services), m_window(window)
{
    m_root = window->add<Amethyst::Frame>();
    m_root->name = "Bottom Bar";
    m_root->addClass("bottom-bar");
    m_root->setBaseProperties({
        .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -EDITOR_BOTTOM_BAR_HEIGHT),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_BOTTOM_BAR_HEIGHT),
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
        [this](Amethyst::FrameScope &fs) {});

    rootScope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH + SEARCHBAR_CONTAINER_WIDTH + 2.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, 2.0f, 1.0f, 0.0f),
                },
            .style = {.backgroundColor = Amethyst::Color3::fromHex(0x181818)},
        },
        [this](Amethyst::FrameScope &fs) {});

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
        [this](Amethyst::PopupScope &p) {
            m_contentBrowserPopup = &p.component;
            m_contentBrowserPanel = std::make_unique<ContentBrowserPanel>(p, m_services);
            m_contentBrowserPanel->setScene(s_workspaceScene(m_currWorkspace));
            m_contentBrowserPanel->onDockInLayout.detachedOnce([this]() {
                if (m_currWorkspace != nullptr) {
                    m_currWorkspace->addPanel(std::move(m_contentBrowserPanel), Amethyst::DockZone::BOTTOM);
                    m_contentBrowserPanel = nullptr;
                    m_contentBrowserPopup = nullptr;
                }
            });
        });
}

void BottomBar::setCurrentWorkspace(Workspace *workspace)
{
    m_currWorkspace = workspace;
    if (m_contentBrowserPanel != nullptr) {
        m_contentBrowserPanel->setScene(s_workspaceScene(workspace));
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
