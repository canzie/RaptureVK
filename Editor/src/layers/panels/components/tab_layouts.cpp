#include "layers/panels/components/tab_layouts.h"

#include <components/ui_scope.h>

static constexpr float TAB_LABEL_PADDING = 8.0f;
static constexpr float TAB_ICON_SIZE = 16.0f;
static constexpr float TAB_ICON_LABEL_GAP = 6.0f;

static void s_idk(std::string label, std::string iconSvg, Amethyst::FrameScope &scope, std::string_view className)
{
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
        .classes = {std::string(className)},
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

std::function<void(Amethyst::Frame &)> iconTabLayout(std::string_view label, std::string_view iconSvg)
{
    return [label = std::string(label), iconSvg = std::string(iconSvg)](Amethyst::Frame &frame) {
        auto scope = Amethyst::FrameScope(frame);
        s_idk(label, iconSvg, scope, "panel-tab");
    };
}

std::function<void(Amethyst::FrameScope &)> iconTabLayoutScope(std::string_view label, std::string_view iconSvg)
{
    return
        [label = std::string(label), iconSvg = std::string(iconSvg)](Amethyst::FrameScope &scope) {
            s_idk(label, iconSvg, scope, "workspace-tab");
        };
}
