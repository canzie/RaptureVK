#ifndef RAPTURE__TAB_LAYOUTS_H
#define RAPTURE__TAB_LAYOUTS_H

#include <functional>
#include <string_view>

namespace Amethyst {
class Frame;
}

/**
 * @brief Builds a tab label that left-aligns text with a small left inset, optionally preceded by an icon.
 * @param label Text shown on the tab.
 * @param iconSvg SVG markup shown to the left of the label. Leave empty to omit it, in which case the label
 * starts where the icon would have, instead of the tab's default centered text.
 * @return Callback usable with TabBar::addTab(content, labelSetup), or wrapped into TabScope::label().
 */
std::function<void(Amethyst::Frame &)> iconTabLayout(std::string_view label, std::string_view iconSvg = {});

#endif // RAPTURE__TAB_LAYOUTS_H
