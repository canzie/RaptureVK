#ifndef RAPTURE__COLOR_FIELD_H
#define RAPTURE__COLOR_FIELD_H

#include <amethyst/Amethyst.h>
#include <components/popup.h>
#include <components/ui_scope.h>

#include <functional>
#include <string>
#include <vector>

/**
 * @brief Editor color field: a swatch next to a hex input that opens a picker popup on click.
 * Construct from a Color3 for an RGB field (6-digit hex), or a Color4 for RGBA (8-digit hex).
 * The given theme classes are applied to every component it builds.
 */
class ColorField {
  public:
    ColorField(Amethyst::UIScope &parent, const Amethyst::Color3 &initial, std::vector<std::string> classes = {});
    ColorField(Amethyst::UIScope &parent, const Amethyst::Color4 &initial, std::vector<std::string> classes = {});

    Amethyst::Color3 getColor3() const { return Amethyst::Color3(m_color); }
    const Amethyst::Color4 &getColor4() const { return m_color; }
    void setColor3(const Amethyst::Color3 &color);
    void setColor4(const Amethyst::Color4 &color);

    Amethyst::Frame *getRoot() const { return m_root; }

    std::function<void(const Amethyst::Color4 &)> onColorChanged;

  private:
    void buildField(Amethyst::UIScope &parent);

    std::vector<std::string> m_classes;
    Amethyst::Color4 m_color;
    Amethyst::Color3 m_rgb;

    Amethyst::Frame *m_root = nullptr;
    Amethyst::ImageButton *m_swatch = nullptr;
    Amethyst::TextInput *m_hexInput = nullptr;
    Amethyst::Popup *m_popup = nullptr;
    Amethyst::Color3Picker *m_picker3 = nullptr;
    Amethyst::Color4Picker *m_picker4 = nullptr;
};

#endif // RAPTURE__COLOR_FIELD_H
