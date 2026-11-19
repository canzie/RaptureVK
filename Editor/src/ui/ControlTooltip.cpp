#include "ControlTooltip.h"

#include "context/EditorContext.h"

#include <components/extensions/ui_tooltip.h>
#include <components/text_label.h>
#include <components/tooltip.h>
#include <components/ui_scope.h>
#include <modules/text_processor.h>
#include <parsers/freetype/font_registry.h>

#include <rapture/core/utils/rp_assert.h>

#include <string>

static constexpr float TOOLTIP_HEIGHT = 26.0f;
static constexpr float TOOLTIP_PADDING = 10.0f;
static constexpr float TEXT_SHORTCUT_GAP = 16.0f;

static float s_measureLabelTextWidth(const Amethyst::TextProcessor &textProcessor, const Amethyst::TextLabel &label)
{
    const Amethyst::TextStyleProperties &style = label.getTextStyleProperties();
    const Amethyst::FontId font = Amethyst::FontRegistry::instance().findFont(style.fontFamily);
    return textProcessor.measureTextAtlas(label.getText(), font, static_cast<uint32_t>(style.fontSize)).x;
}

static Amethyst::TextLabel *s_addTooltipLabel(Amethyst::Tooltip &surface, std::string_view styleClass, std::string text)
{
    Amethyst::TextLabel *label = nullptr;
    Amethyst::UIScope(surface).textLabel(
        {
            .classes = {std::string(styleClass)},
            .base = {.interactable = false},
            .label = std::move(text),
        },
        [&label](Amethyst::TextLabelScope &tl) { label = &tl.component; });
    return label;
}

static void s_fillControlTooltip(Amethyst::Tooltip &surface, const ControlTooltip &tooltip, const Amethyst::TextProcessor &textProcessor,
                                 const ShortcutRegistry *shortcuts)
{
    surface.removeAllChildren();
    surface.setClasses({"tooltip"});

    const std::string_view text = tooltip.description.empty() ? tooltip.name : tooltip.description;
    Amethyst::TextLabel *textLabel = s_addTooltipLabel(surface, "tooltip-text", std::string(text));
    const float textWidth = s_measureLabelTextWidth(textProcessor, *textLabel);

    std::string shortcutText;
    if (tooltip.command.has_value() && shortcuts != nullptr) {
        shortcutText = Shortcut_toString(shortcuts->getShortcutForCommand(*tooltip.command));
    }

    float contentWidth = textWidth;
    textLabel->setBaseProperties({.size = Amethyst::UDim2(0.0f, textWidth, 1.0f, 0.0f)});

    if (!shortcutText.empty()) {
        Amethyst::TextLabel *shortcutLabel = s_addTooltipLabel(surface, "tooltip-shortcut", std::move(shortcutText));
        const float shortcutWidth = s_measureLabelTextWidth(textProcessor, *shortcutLabel);
        shortcutLabel->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(textWidth + TEXT_SHORTCUT_GAP, 0.0f),
            .size = Amethyst::UDim2(0.0f, shortcutWidth, 1.0f, 0.0f),
        });
        contentWidth += TEXT_SHORTCUT_GAP + shortcutWidth;
    }

    surface.setBaseProperties({.padding = Amethyst::UDim4{{0.0f, 0.0f}, {0.0f, TOOLTIP_PADDING}, {0.0f, 0.0f}, {0.0f, TOOLTIP_PADDING}}});
    const Amethyst::vec4 insets = surface.resolveContentInsets(surface.absoluteSize);
    surface.setBaseProperties({.size = Amethyst::UDim2::fromOffset(contentWidth + insets.y + insets.w, TOOLTIP_HEIGHT)});
}

void ControlTooltip_attach(Amethyst::UIObject &control, const ControlTooltip &tooltip, const EditorContext &editor)
{
    RP_ASSERT(editor.amethyst != nullptr, "a control tooltip needs the ui context to measure its text");

    Amethyst::UITooltip *extension = control.getExtension<Amethyst::UITooltip>();
    if (extension == nullptr) {
        extension = control.addExtension<Amethyst::UITooltip>();
    }

    const Amethyst::TextProcessor *textProcessor = &editor.amethyst->getTextProcessor();
    const ShortcutRegistry *shortcuts = editor.shortcuts;
    extension->build = [tooltip, textProcessor, shortcuts](Amethyst::Tooltip &surface) {
        s_fillControlTooltip(surface, tooltip, *textProcessor, shortcuts);
    };
}
