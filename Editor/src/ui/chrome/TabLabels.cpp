#include "TabLabels.h"

#include "app/EditorConfig.h"

#include <components/ui_scope.h>

#include <optional>
#include <utility>

static constexpr float TAB_CLOSE_BUTTON_CLEARANCE = 26.0f;

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

static void s_buildTabLabel(const std::string &label, const std::string &iconSvg, std::optional<Amethyst::Color4> iconColor,
                            const TabLabelSizes &sizes, Amethyst::FrameScope &scope)
{
    float textOffset = sizes.padding;

    if (!iconSvg.empty()) {
        Amethyst::ImageLabelProperties icon = {
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                    .position = Amethyst::UDim2(0.0f, sizes.padding, 0.5f, 0.0f),
                    .size = Amethyst::UDim2::fromOffset(sizes.iconSize, sizes.iconSize),
                },
            .style = {.backgroundTransparency = 1.0f},
            .svg = iconSvg,
        };
        if (iconColor.has_value()) {
            icon.classes = {"tab-icon-tinted"};
            icon.image.imageColor = *iconColor;
        }
        scope.imageLabel(std::move(icon));
        textOffset = sizes.padding + sizes.iconSize + sizes.iconGap;
    }

    scope.textLabel({
        .base =
            {
                .position = Amethyst::UDim2(0.0f, textOffset, 0.0f, 0.0f),
                .size = Amethyst::UDim2(1.0f, -(textOffset + TAB_CLOSE_BUTTON_CLEARANCE), 1.0f, 0.0f),
            },
        .style = {.backgroundTransparency = 1.0f},
        .text =
            {
                .textXAlignment = Amethyst::TextXAlignment::LEFT,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
                .textTruncate = Amethyst::TextTruncate::ELLIPSIS,
            },
        .label = label,
    });
}

std::function<void(Amethyst::Frame &)> iconTabLabel(std::string_view label, std::string_view iconSvg, const EditorConfig &config)
{
    return [label = std::string(label), iconSvg = std::string(iconSvg), sizes = s_takeTabLabelSizes(config)](Amethyst::Frame &frame) {
        Amethyst::FrameScope scope(frame);
        s_buildTabLabel(label, iconSvg, std::nullopt, sizes, scope);
    };
}

std::function<void(Amethyst::Frame &)> iconTabLabel(std::string_view label, std::string_view iconSvg, Amethyst::Color4 iconColor,
                                                    const EditorConfig &config)
{
    return [label = std::string(label), iconSvg = std::string(iconSvg), iconColor,
            sizes = s_takeTabLabelSizes(config)](Amethyst::Frame &frame) {
        Amethyst::FrameScope scope(frame);
        s_buildTabLabel(label, iconSvg, iconColor, sizes, scope);
    };
}

std::function<void(Amethyst::FrameScope &)> iconTabLabelScope(std::string_view label, std::string_view iconSvg,
                                                              const EditorConfig &config)
{
    return [label = std::string(label), iconSvg = std::string(iconSvg),
            sizes = s_takeTabLabelSizes(config)](Amethyst::FrameScope &scope) {
        s_buildTabLabel(label, iconSvg, std::nullopt, sizes, scope);
    };
}
