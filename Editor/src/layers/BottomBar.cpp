#include "layers/BottomBar.h"

#include "layers/EditorLayout.h"
#include "layers/panels/ContentBrowserPanel.h"

#include <components/popup.h>
#include <components/ui_scope.h>

static constexpr float BOTTOM_BAR_PADDING = 4.0f;
static constexpr float CONTENT_BROWSER_BUTTON_WIDTH = 130.0f;

BottomBar::BottomBar(Amethyst::Window *window) : m_window(window)
{
    m_root = window->add<Amethyst::Frame>();
    m_root->name = "Bottom Bar";
    m_root->setBaseProperties({
        .padding = {Amethyst::UDim::fromOffset(BOTTOM_BAR_PADDING), Amethyst::UDim::fromOffset(BOTTOM_BAR_PADDING),
                    Amethyst::UDim::fromOffset(BOTTOM_BAR_PADDING), Amethyst::UDim::fromOffset(BOTTOM_BAR_PADDING)},
        .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -EDITOR_BOTTOM_BAR_HEIGHT),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, EDITOR_BOTTOM_BAR_HEIGHT),
    });

    setupContentBrowserToggle();
}

BottomBar::~BottomBar() = default;

void BottomBar::setupContentBrowserToggle()
{
    Amethyst::UIScope(*m_root).textButton(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, CONTENT_BROWSER_BUTTON_WIDTH, 1.0f, 0.0f),
                },
            .text =
                {
                    .textXAlignment = Amethyst::TextXAlignment::CENTER,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = "Content Browser",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_contentBrowserBtn = &b.component;
            m_contentBrowserBtn->onMouseButton1ClickCb = [this]() {
                toggleContentBrowser();
                return Amethyst::EventResult::CONSUMED;
            };
        });

    Amethyst::UIScope(*m_root)
        .popup(
            {
                .base = {.size = Amethyst::UDim2::fromScale(0.7f, 0.45f)},
                .style = {.backgroundColor = Amethyst::Color3(0.13f), .borderPixelSize = 0.0f},
                .placement = Amethyst::PopupPlacement::ABOVE,
            },
            [this](Amethyst::PopupScope &p) {
                m_contentBrowserPopup = &p.component;
                m_contentBrowserPanel = std::make_unique<ContentBrowserPanel>(p);
            });
}

void BottomBar::toggleContentBrowser()
{
    if (m_contentBrowserPopup->isOpen()) {
        m_contentBrowserPopup->close();
    } else {
        m_contentBrowserPopup->open(m_contentBrowserBtn);
    }
}
