#ifndef RAPTURE__CONTROL_TOOLTIP_H
#define RAPTURE__CONTROL_TOOLTIP_H

#include "ShortcutRegistry.h"

#include <optional>
#include <string_view>

namespace Amethyst {
class UIObject;
} // namespace Amethyst

struct EditorContext;

/**
 * @brief What a control says about itself when the cursor rests on it
 */
struct ControlTooltip {
    std::string_view name;
    std::string_view description;
    /// The command whose current keys are shown beside the text
    std::optional<EditorCommand> command = std::nullopt;
};

/**
 * @brief Shows a tooltip when the cursor rests on a control, replacing one it already shows
 * @param control The control the tooltip describes
 * @param tooltip The text it shows, its description or else its name, and the command whose keys it shows
 * @param editor The editor whose shortcuts the tooltip shows
 */
void ControlTooltip_attach(Amethyst::UIObject &control, const ControlTooltip &tooltip, const EditorContext &editor);

#endif // RAPTURE__CONTROL_TOOLTIP_H
