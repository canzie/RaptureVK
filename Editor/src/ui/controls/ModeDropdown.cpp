#include "mode_dropdown.h"

#include <components/dropdown.h>
#include <components/ui_scope.h>

#include <string>

static constexpr float DROPDOWN_WIDTH = 120.0f;
static constexpr float DROPDOWN_HEIGHT = 24.0f;

Amethyst::Dropdown *ModeDropdown_add(Amethyst::Instance &parent, std::span<const EditorMode> modes,
                                     std::function<void(EditorMode)> onSelected)
{
    if (modes.empty()) {
        return nullptr;
    }

    Amethyst::Dropdown *box = nullptr;

    Amethyst::UIScope(parent).dropdown(
        {
            .classes = {"property-input-field"},
            .base = {.size = Amethyst::UDim2::fromOffset(DROPDOWN_WIDTH, DROPDOWN_HEIGHT)},
            .label = std::string(EDITOR_MODE_LABELS[modes.front()]),
        },
        [&](Amethyst::DropdownScope &d) {
            box = &d.component;

            for (EditorMode mode : modes) {
                d.action(std::string(EDITOR_MODE_LABELS[mode]), [box, mode, onSelected]() {
                    box->setText(std::string(EDITOR_MODE_LABELS[mode]));
                    if (onSelected != nullptr) {
                        onSelected(mode);
                    }
                });
            }
        });

    return box;
}
