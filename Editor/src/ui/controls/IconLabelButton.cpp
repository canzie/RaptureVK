#include "IconLabelButton.h"

#include <components/image_label.h>
#include <components/text_label.h>
#include <components/ui_scope.h>

IconLabelButton::IconLabelButton(std::string_view iconSvg, std::string_view label, const std::vector<std::string> &classes)
{
    for (const std::string &styleClass : classes) {
        addClass(styleClass);
    }
    setBaseProperties({
        .automaticSize = Amethyst::AutomaticSize::X,
        .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(HORIZONTAL_PADDING), .left = Amethyst::UDim::fromOffset(HORIZONTAL_PADDING)},
    });

    Amethyst::UIScope scope(*this);
    scope.imageLabel(
        {
            .classes = classes,
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.5f, 0.0f),
                    .size = Amethyst::UDim2::fromOffset(ICON_SIZE, ICON_SIZE),
                },
            .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
            .svg = std::string(iconSvg),
        },
        [this](Amethyst::ImageLabelScope &icon) {
            m_icon = &icon.component;
            m_icon->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
        });

    scope.textLabel(
        {
            .classes = classes,
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::X,
                    .position = Amethyst::UDim2(0.0f, ICON_SIZE + ICON_LABEL_GAP, 0.0f, 0.0f),
                    .size = Amethyst::UDim2::fromScale(0.0f, 1.0f),
                },
            .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
            .text =
                {
                    .textXAlignment = Amethyst::TextXAlignment::LEFT,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = std::string(label),
        },
        [this](Amethyst::TextLabelScope &text) {
            m_label = &text.component;
            m_label->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
        });

    track(onHoverChanged.connect([this](bool hovered) {
        (void)hovered;
        propagateGuiStateToChildren();
    }));
}
