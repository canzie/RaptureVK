#include "IconLabelButton.h"

#include <components/image_label.h>
#include <components/text_label.h>
#include <components/ui_scope.h>
#include <modules/text_processor.h>
#include <parsers/freetype/font_registry.h>

IconLabelButton::IconLabelButton(std::string_view iconSvg, std::string_view label, const std::vector<std::string> &classes)
{
    for (const std::string &styleClass : classes) {
        addClass(styleClass);
    }
    setBaseProperties({.padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(HORIZONTAL_PADDING),
                                                  .left = Amethyst::UDim::fromOffset(HORIZONTAL_PADDING)}});

    Amethyst::UIScope scope(*this);
    scope.imageLabel(
        {
            .classes = classes,
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                    .interactable = false,
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.5f, 0.0f),
                    .size = Amethyst::UDim2::fromOffset(ICON_SIZE, ICON_SIZE),
                },
            .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
            .svg = std::string(iconSvg),
        },
        [this](Amethyst::ImageLabelScope &icon) { m_icon = &icon.component; });

    scope.textLabel(
        {
            .classes = classes,
            .base =
                {
                    .interactable = false,
                    .position = Amethyst::UDim2(0.0f, ICON_SIZE + ICON_LABEL_GAP, 0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, -(ICON_SIZE + ICON_LABEL_GAP), 1.0f, 0.0f),
                },
            .style = {.backgroundTransparency = 1.0f, .borderPixelSize = 0.0f},
            .text =
                {
                    .textXAlignment = Amethyst::TextXAlignment::LEFT,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                    .textTruncate = Amethyst::TextTruncate::AT_END,
                },
            .label = std::string(label),
        },
        [this](Amethyst::TextLabelScope &text) { m_label = &text.component; });

    track(onHoverChanged.connect([this](bool hovered) {
        (void)hovered;
        propagateGuiStateToChildren();
    }));
}

float IconLabelButton::measureFittedWidth(const Amethyst::TextProcessor &textProcessor) const
{
    const Amethyst::TextStyleProperties &style = m_label->getTextStyleProperties();
    const Amethyst::FontId font = Amethyst::FontRegistry::instance().findFont(style.fontFamily);
    const float labelWidth = textProcessor.measureTextAtlas(m_label->getText(), font, static_cast<uint32_t>(style.fontSize)).x;
    const Amethyst::vec4 insets = resolveContentInsets(absoluteSize);
    return insets.y + insets.w + ICON_SIZE + ICON_LABEL_GAP + labelWidth;
}
