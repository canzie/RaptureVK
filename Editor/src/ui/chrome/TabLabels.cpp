#include "TabLabels.h"

#include "app/EditorConfig.h"

#include <components/ui_scope.h>

static const Amethyst::Color4 COL_TAB_ICON{0.7f, 0.7f, 0.7f, 1.0f};

/**
 * @brief The sizes a tab label is laid out by, taken once so the build callback holds no config
 */
struct TabLabelSizes {
    float padding;
    float iconSize;
    float iconGap;
};

static TabLabelSizes s_takeTabLabelSizes(const EditorConfig &config)
{
    return {
        .padding = config.getFloat(EDITOR_CONFIG_TAB_LABEL_PADDING),
        .iconSize = config.getFloat(EDITOR_CONFIG_TAB_ICON_SIZE),
        .iconGap = config.getFloat(EDITOR_CONFIG_TAB_ICON_GAP),
    };
}

static void s_buildTabLabel(const std::string &label, const std::string &iconSvg, const TabLabelSizes &sizes,
                            Amethyst::FrameScope &scope)
{
    float textOffset = sizes.padding;

    if (!iconSvg.empty()) {
        scope.imageLabel({
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                    .position = Amethyst::UDim2(0.0f, sizes.padding, 0.5f, 0.0f),
                    .size = Amethyst::UDim2::fromOffset(sizes.iconSize, sizes.iconSize),
                },
            .style = {.backgroundTransparency = 1.0f},
            .image = {.imageColor = COL_TAB_ICON},
            .svg = iconSvg,
        });
        textOffset = sizes.padding + sizes.iconSize + sizes.iconGap;
    }

    scope.textLabel({
        .base =
            {
                .position = Amethyst::UDim2(0.0f, textOffset, 0.0f, 0.0f),
                .size = Amethyst::UDim2(1.0f, -(textOffset + sizes.padding), 1.0f, 0.0f),
            },
        .style = {.backgroundTransparency = 1.0f},
        .text =
            {
                .textXAlignment = Amethyst::TextXAlignment::LEFT,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
            },
        .label = label,
    });
}

std::function<void(Amethyst::Frame &)> iconTabLabel(std::string_view label, std::string_view iconSvg, const EditorConfig &config)
{
    return
        [label = std::string(label), iconSvg = std::string(iconSvg), sizes = s_takeTabLabelSizes(config)](Amethyst::Frame &frame) {
            Amethyst::FrameScope scope(frame);
            s_buildTabLabel(label, iconSvg, sizes, scope);
        };
}

std::function<void(Amethyst::FrameScope &)> iconTabLabelScope(std::string_view label, std::string_view iconSvg,
                                                              const EditorConfig &config)
{
    return [label = std::string(label), iconSvg = std::string(iconSvg),
            sizes = s_takeTabLabelSizes(config)](Amethyst::FrameScope &scope) { s_buildTabLabel(label, iconSvg, sizes, scope); };
}
