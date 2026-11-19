#include "ControlTooltip.h"

#include "context/EditorContext.h"

#include <components/extensions/ui_tooltip.h>
#include <components/text_label.h>
#include <components/tooltip.h>
#include <components/ui_scope.h>
#include <components/extensions/ui_list_layout.h>


#include <string>

static constexpr float TOOLTIP_HEIGHT = 26.0f;
static constexpr float TOOLTIP_PADDING = 10.0f;
static constexpr float TEXT_SHORTCUT_GAP = 16.0f;

static void s_addTooltipLabel(Amethyst::UIScope &row, std::string_view styleClass, std::string text, uint32_t layoutOrder)
{
    row.textLabel({
        .classes = {std::string(styleClass)},
        .base =
            {
                .automaticSize = Amethyst::AutomaticSize::X,
                .interactable = false,
                .layoutOrder = layoutOrder,
                .size = Amethyst::UDim2::fromScale(0.0f, 1.0f),
            },
        .label = std::move(text),
    });
}

static void s_fillControlTooltip(Amethyst::Tooltip &surface, const ControlTooltip &tooltip, const ShortcutRegistry *shortcuts)
{
    surface.removeAllChildren();
    surface.setClasses({"tooltip"});
    surface.setBaseProperties({
        .automaticSize = Amethyst::AutomaticSize::X,
        .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(TOOLTIP_PADDING), .left = Amethyst::UDim::fromOffset(TOOLTIP_PADDING)},
        .size = Amethyst::UDim2::fromOffset(0.0f, TOOLTIP_HEIGHT),
    });

    std::string shortcutText;
    if (tooltip.command.has_value() && shortcuts != nullptr) {
        shortcutText = Shortcut_toString(shortcuts->getShortcutForCommand(*tooltip.command));
    }

    // a row of its own, since the surface is shared with tooltips laid out differently
    Amethyst::UIScope(surface).frame(
        {
            .base = {.automaticSize = Amethyst::AutomaticSize::X, .interactable = false, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
        },
        [&tooltip, &shortcutText](Amethyst::FrameScope &row) {
            auto *layout = row.component.addExtension<Amethyst::UIListLayout>();
            layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
            layout->innerPadding = Amethyst::UDim::fromOffset(TEXT_SHORTCUT_GAP);

            const std::string_view text = tooltip.description.empty() ? tooltip.name : tooltip.description;
            s_addTooltipLabel(row, "tooltip-text", std::string(text), 0);
            if (!shortcutText.empty()) {
                s_addTooltipLabel(row, "tooltip-shortcut", std::move(shortcutText), 1);
            }
        });
}

void ControlTooltip_attach(Amethyst::UIObject &control, const ControlTooltip &tooltip, const EditorContext &editor)
{
    Amethyst::UITooltip *extension = control.getExtension<Amethyst::UITooltip>();
    if (extension == nullptr) {
        extension = control.addExtension<Amethyst::UITooltip>();
    }

    const ShortcutRegistry *shortcuts = editor.shortcuts;
    extension->build = [tooltip, shortcuts](Amethyst::Tooltip &surface) { s_fillControlTooltip(surface, tooltip, shortcuts); };
}
