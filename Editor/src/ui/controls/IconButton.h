#ifndef RAPTURE__ICON_BUTTON_H
#define RAPTURE__ICON_BUTTON_H

#include <amethyst/Amethyst.h>
#include <components/text_button.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Amethyst {
class ImageLabel;
} // namespace Amethyst

/**
 * @brief A button showing an icon centred in it at a size of its own, styled as a whole by the
 * classes it is given: the background and border from the button, the icon's tint from the same
 * classes, the icon following the button's hover and active state.
 */
class IconButton : public Amethyst::TextButton {
  public:
    IconButton(std::string_view iconSvg, float iconSize, const std::vector<std::string> &classes);

    /**
     * @brief Replaces the icon shown
     * @param iconSvg The icon
     */
    void setIcon(std::string_view iconSvg);

    /**
     * @brief Turns one gui state flag on or off, for the button and its icon together
     * @param flag The flag, such as Amethyst::GUI_STATE_ACTIVE
     * @param on Whether the flag is set
     */
    void setGuiStateFlag(uint16_t flag, bool on);

  private:
    Amethyst::ImageLabel *m_icon = nullptr;
};

#endif // RAPTURE__ICON_BUTTON_H
