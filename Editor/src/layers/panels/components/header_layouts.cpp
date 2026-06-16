#include "layers/panels/components/header_layouts.h"

#include "Icons.h"
#include "logging/Log.h"

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

// CollapsibleHeader's default disclosure triangle uses indicatorPadding=6, indicatorSize=10, so its
// content starts at 6 + 10 + 6 = 22px; this mirrors that so icon/label line up with the triangle.
static constexpr float HEADER_CONTENT_OFFSET = 22.0f;
static constexpr float HEADER_ICON_SIZE = 16.0f;
static constexpr float HEADER_ICON_LABEL_GAP = 6.0f;
static constexpr float HEADER_MENU_BUTTON_SIZE = 20.0f;
static constexpr float HEADER_MENU_BUTTON_RIGHT_PAD = 6.0f;
static constexpr float HEADER_LABEL_RIGHT_RESERVED = HEADER_MENU_BUTTON_RIGHT_PAD + HEADER_MENU_BUTTON_SIZE + 4.0f;

std::function<void(Amethyst::FrameScope &)> componentHeaderLayout(std::string_view label, std::string_view iconSvg,
                                                                   std::function<void()> onMenuClicked)
{
    return [label = std::string(label), iconSvg = std::string(iconSvg), onMenuClicked](Amethyst::FrameScope &scope) {
        float textOffset = HEADER_CONTENT_OFFSET;

        if (!iconSvg.empty()) {
            scope.imageLabel({
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                        .position = Amethyst::UDim2(0.0f, HEADER_CONTENT_OFFSET, 0.5f, 0.0f),
                        .size = Amethyst::UDim2::fromOffset(HEADER_ICON_SIZE, HEADER_ICON_SIZE),
                        .zindexBehavior = Amethyst::ZIndexBehavior::GLOBAL,
                    },
                .style = {.backgroundTransparency = 1.0f},
                .image = {.imageColor = Amethyst::Color4(0.7f, 0.7f, 0.7f, 1.0f)},
                .svg = iconSvg,
            });
            textOffset = HEADER_CONTENT_OFFSET + HEADER_ICON_SIZE + HEADER_ICON_LABEL_GAP;
        }

        scope.textLabel({
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, textOffset, 0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, -(textOffset + HEADER_LABEL_RIGHT_RESERVED), 1.0f, 0.0f),
                    .zindexBehavior = Amethyst::ZIndexBehavior::GLOBAL,
                },
            .style = {.backgroundTransparency = 1.0f},
            .text =
                {
                    .fontSize = 13.0f,
                    .textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f),
                    .textXAlignment = Amethyst::TextXAlignment::LEFT,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = label,
        });

        scope.imageButton(
            {
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                        .position = Amethyst::UDim2(1.0f, -HEADER_MENU_BUTTON_RIGHT_PAD, 0.5f, 0.0f),
                        .size = Amethyst::UDim2::fromOffset(HEADER_MENU_BUTTON_SIZE, HEADER_MENU_BUTTON_SIZE),
                        .zindexBehavior = Amethyst::ZIndexBehavior::GLOBAL,
                    },
                .style = {.backgroundTransparency = 1.0f, .cornerRadius = 4.0f},
                .image = {.imageColor = Amethyst::Color4(0.6f, 0.6f, 0.6f, 1.0f)},
                .svg = Icons::SVG_MORE,
            },
            [label, onMenuClicked](Amethyst::ImageButtonScope &btn) {
                auto *b = &btn.component;
                b->onHoverChanged = [b](bool hovered) {
                    b->setImageStyleProperties({.imageColor = hovered ? Amethyst::Color4{0.85f, 0.85f, 0.85f, 1.0f}
                                                                       : Amethyst::Color4{0.6f, 0.6f, 0.6f, 1.0f}});
                    b->setBaseStyleProperties({.backgroundTransparency = hovered ? 0.85f : 1.0f});
                };
                b->onMouseButton1ClickCb = [label, onMenuClicked]() {
                    if (onMenuClicked) {
                        onMenuClicked();
                    } else {
                        Rapture::RP_INFO("{} menu clicked", label);
                    }
                    return Amethyst::EventResult::CONSUMED;
                };
            });
    };
}
