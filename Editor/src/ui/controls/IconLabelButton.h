#ifndef RAPTURE__ICON_LABEL_BUTTON_H
#define RAPTURE__ICON_LABEL_BUTTON_H

#include <amethyst/Amethyst.h>
#include <components/text_button.h>

#include <string>
#include <string_view>
#include <vector>

namespace Amethyst {
class ImageLabel;
class TextLabel;
class TextProcessor;
} // namespace Amethyst

/**
 * @brief A button showing an icon followed by a label, styled as a whole by the classes it is given:
 * the background and border from the button, the label's text and the icon's tint from the same
 * classes, all following the button's hover and active state.
 */
class IconLabelButton : public Amethyst::TextButton {
  public:
    static constexpr float ICON_SIZE = 15.0f;
    static constexpr float ICON_LABEL_GAP = 7.0f;
    static constexpr float HORIZONTAL_PADDING = 10.0f;

    IconLabelButton(std::string_view iconSvg, std::string_view label, const std::vector<std::string> &classes);

    Amethyst::TextLabel &getLabel() { return *m_label; }
    Amethyst::ImageLabel &getIcon() { return *m_icon; }

    /**
     * @brief How wide the button is when it fits its icon and label
     * @param textProcessor What measures the label
     * @return The width
     */
    float measureFittedWidth(const Amethyst::TextProcessor &textProcessor) const;

  private:
    Amethyst::ImageLabel *m_icon = nullptr;
    Amethyst::TextLabel *m_label = nullptr;
};

#endif // RAPTURE__ICON_LABEL_BUTTON_H
