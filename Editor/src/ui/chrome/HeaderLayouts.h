#ifndef RAPTURE__HEADER_LAYOUTS_H
#define RAPTURE__HEADER_LAYOUTS_H

#include <functional>
#include <string_view>

namespace Amethyst {
struct FrameScope;
class UIObject;
} // namespace Amethyst

/**
 * @brief Builds a collapsible-header content callback for a property section: an optional icon, a left-aligned label,
 * and a "..." menu button anchored to the right edge of the bar when a menu is given.
 * @param label Text shown next to the disclosure triangle.
 * @param iconSvg SVG markup shown between the triangle and the label, empty to omit it.
 * @param onMenuClicked Called with the "..." button when it is clicked, empty to omit the button.
 * @return Callback usable with CollapsibleHeaderScope::header(std::function<void(Amethyst::FrameScope &)>).
 */
std::function<void(Amethyst::FrameScope &)> componentHeaderLayout(std::string_view label, std::string_view iconSvg = {},
                                                                  std::function<void(Amethyst::UIObject &)> onMenuClicked = {});

#endif // RAPTURE__HEADER_LAYOUTS_H
