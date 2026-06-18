#ifndef RAPTURE__HEADER_LAYOUTS_H
#define RAPTURE__HEADER_LAYOUTS_H

#include <functional>
#include <string_view>

namespace Amethyst {
struct FrameScope;
}

/**
 * @brief Builds a collapsible-header content callback for a PropertiesPanel component section: an
 * optional icon, a left-aligned label, and a "..." menu button anchored to the right edge of the bar.
 * @param label Text shown next to the disclosure triangle.
 * @param iconSvg SVG markup shown between the triangle and the label. Leave empty to omit it, in which
 * case the label starts where the icon would have.
 * @param onMenuClicked Called when the "..." button is clicked. Defaults to logging the click.
 * @return Callback usable with CollapsibleHeaderScope::header(std::function<void(Amethyst::FrameScope &)>).
 */
std::function<void(Amethyst::FrameScope &)> componentHeaderLayout(std::string_view label, std::string_view iconSvg = {},
                                                                  std::function<void()> onMenuClicked = {});

#endif // RAPTURE__HEADER_LAYOUTS_H
