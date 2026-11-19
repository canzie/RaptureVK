#include "IconButton.h"

#include <components/image_label.h>
#include <components/ui_scope.h>

IconButton::IconButton(std::string_view iconSvg, float iconSize, const std::vector<std::string> &classes)
{
    for (const std::string &styleClass : classes) {
        addClass(styleClass);
    }

    Amethyst::UIScope(*this).imageLabel(
        {
            .classes = classes,
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
                    .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
                    .size = Amethyst::UDim2::fromOffset(iconSize, iconSize),
                },
            .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
            .svg = std::string(iconSvg),
        },
        [this](Amethyst::ImageLabelScope &icon) {
            m_icon = &icon.component;
            m_icon->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
        });

    track(onHoverChanged.connect([this](bool hovered) {
        (void)hovered;
        propagateGuiStateToChildren();
    }));
}

void IconButton::setIcon(std::string_view iconSvg)
{
    m_icon->setSvg(std::string(iconSvg));
}

void IconButton::setGuiStateFlag(uint16_t flag, bool on)
{
    const uint16_t current = getGuiState();
    setGuiState(static_cast<uint16_t>(on ? (current | flag) : (current & ~flag)));
    propagateGuiStateToChildren();
}
