#ifndef RAPTURE__TAB_LABELS_H
#define RAPTURE__TAB_LABELS_H

#include <functional>
#include <string_view>

#include <modules/color.h>

namespace Amethyst {
class Frame;
struct FrameScope;
} // namespace Amethyst

class EditorConfig;

/**
 * @brief Builds a tab's label: a left aligned name, preceded by an icon where one is given.
 * @param label Text shown on the tab
 * @param iconSvg SVG markup shown left of the text, empty to omit it, in which case the text starts
 * where the icon would have rather than centred
 * @param config Where the label takes its sizes from
 * @return Callback for TabBar::addTab
 */
std::function<void(Amethyst::Frame &)> iconTabLabel(std::string_view label, std::string_view iconSvg, const EditorConfig &config);

/**
 * @brief The same label, its icon tinted with a given color
 * @param label Text shown on the tab
 * @param iconSvg SVG markup shown left of the text, empty to omit it
 * @param iconColor Color the icon is tinted with
 * @param config Where the label takes its sizes from
 * @return Callback for TabBar::addTab
 */
std::function<void(Amethyst::Frame &)> iconTabLabel(std::string_view label, std::string_view iconSvg, Amethyst::Color4 iconColor,
                                                    const EditorConfig &config);

/**
 * @brief The same label, for a tab declared while its bar is being built
 * @param label Text shown on the tab
 * @param iconSvg SVG markup shown left of the text, empty to omit it
 * @param config Where the label takes its sizes from
 * @return Callback for TabScope::label
 */
std::function<void(Amethyst::FrameScope &)> iconTabLabelScope(std::string_view label, std::string_view iconSvg,
                                                              const EditorConfig &config);

#endif // RAPTURE__TAB_LABELS_H
