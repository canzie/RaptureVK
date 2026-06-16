#include "layers/panels/components/tab_layouts.h"

#include <components/ui_scope.h>

static constexpr float TAB_LABEL_PADDING = 8.0f;
static constexpr float TAB_ICON_SIZE = 16.0f;
static constexpr float TAB_ICON_LABEL_GAP = 6.0f;

std::function<void(Amethyst::Frame &)> iconTabLayout(std::string_view label, std::string_view iconSvg)
{
    return [label = std::string(label), iconSvg = std::string(iconSvg)](Amethyst::Frame &frame) {
        Amethyst::FrameScope scope(frame);

        float textOffset = TAB_LABEL_PADDING;

        if (!iconSvg.empty()) {
            scope.imageLabel({
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                        .position = Amethyst::UDim2(0.0f, TAB_LABEL_PADDING, 0.5f, 0.0f),
                        .size = Amethyst::UDim2::fromOffset(TAB_ICON_SIZE, TAB_ICON_SIZE),
                    },
                .style = {.backgroundTransparency = 1.0f},
                .image = {.imageColor = Amethyst::Color4(0.7f, 0.7f, 0.7f, 1.0f)},
                .svg = iconSvg,
            });
            textOffset = TAB_LABEL_PADDING + TAB_ICON_SIZE + TAB_ICON_LABEL_GAP;
        }

        scope.textLabel({
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, textOffset, 0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, -(textOffset + TAB_LABEL_PADDING), 1.0f, 0.0f),
                },
            .style = {.backgroundTransparency = 1.0f},
            .text =
                {
                    .textXAlignment = Amethyst::TextXAlignment::LEFT,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = label,
        });
    };
}
