#ifndef RAPTURE__MODE_DROPDOWN_H
#define RAPTURE__MODE_DROPDOWN_H

#include "layers/workspaces/Workspace.h"

#include <amethyst/Amethyst.h>

#include <functional>
#include <span>

namespace Amethyst {
class Dropdown;
} // namespace Amethyst

/**
 * @brief Builds the control a mode is chosen through
 * @param parent What the control is built under
 * @param modes The modes to offer, in the order they are listed
 * @param onSelected Called with the mode chosen
 * @return The control, showing the mode it was last used to choose
 */
Amethyst::Dropdown *ModeDropdown_add(Amethyst::Instance &parent, std::span<const EditorMode> modes,
                                     std::function<void(EditorMode)> onSelected);

#endif // RAPTURE__MODE_DROPDOWN_H
