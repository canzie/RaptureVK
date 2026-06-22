#include "layers/panels/components/color_field.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

static constexpr float SWATCH_SIZE = 18.0f;
static constexpr float SWATCH_GAP = 6.0f;
static constexpr float POPUP_WIDTH = 240.0f;
static constexpr float POPUP_HEIGHT = 260.0f;
static constexpr float POPUP_PADDING = 8.0f;

static std::string s_toHexString(const Amethyst::Color4 &c, bool alpha)
{
    auto ch = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char buf[10];
    if (alpha) {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", ch(c.r), ch(c.g), ch(c.b), ch(c.a));
    } else {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", ch(c.r), ch(c.g), ch(c.b));
    }
    return buf;
}

static std::optional<Amethyst::Color4> s_parseHex(std::string_view text)
{
    size_t b = 0, e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b])) != 0) {
        b++;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1])) != 0) {
        e--;
    }
    std::string_view s = text.substr(b, e - b);
    if (!s.empty() && s.front() == '#') {
        s.remove_prefix(1);
    }
    if (s.size() != 6 && s.size() != 8) {
        return std::nullopt;
    }
    uint32_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return std::nullopt;
        }
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    return Amethyst::Color4::fromHex(v, s.size() == 8);
}

static Amethyst::Popup *s_createPopup(Amethyst::Frame &root, const std::vector<std::string> &classes)
{
    Amethyst::Popup *popup = nullptr;
    Amethyst::UIScope(root).popup(
        {
            .classes = classes,
            .base = {.padding = {Amethyst::UDim::fromOffset(POPUP_PADDING), Amethyst::UDim::fromOffset(POPUP_PADDING),
                                 Amethyst::UDim::fromOffset(POPUP_PADDING), Amethyst::UDim::fromOffset(POPUP_PADDING)},
                     .size = Amethyst::UDim2::fromOffset(POPUP_WIDTH, POPUP_HEIGHT)},
            .placement = Amethyst::PopupPlacement::BELOW,
        },
        [&popup](Amethyst::PopupScope &p) { popup = &p.component; });
    return popup;
}

ColorField::ColorField(Amethyst::UIScope &parent, const Amethyst::Color3 &initial, std::vector<std::string> classes)
    : m_classes(std::move(classes)), m_color(initial, 1.0f), m_rgb(initial)
{
    buildField(parent);

    m_swatch->onMouseButton1ClickCb = [this]() {
        if (m_popup == nullptr) {
            m_popup = s_createPopup(*m_root, m_classes);
            Amethyst::UIScope(*m_popup).color3Picker(
                {
                    .classes = m_classes,
                    .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                    .value = &m_rgb,
                },
                [this](Amethyst::Color3PickerScope &s) {
                    m_picker3 = &s.component;
                    m_picker3->onValueChanged = [this](const Amethyst::Color3 &c) {
                        m_color = Amethyst::Color4(c, 1.0f);
                        m_rgb = c;
                        m_swatch->setBaseStyleProperties({.backgroundColor = Amethyst::Color3(m_color)});
                        m_hexInput->setText(s_toHexString(m_color, false));
                        if (onColorChanged) {
                            onColorChanged(m_color);
                        }
                    };
                });
        }
        if (m_popup->isOpen()) {
            m_popup->close();
        } else {
            Amethyst::vec2 anchor = m_swatch->absolutePosition;
            m_popup->openAt(Amethyst::vec2(anchor.x - POPUP_WIDTH, anchor.y - POPUP_HEIGHT));
        }
        return Amethyst::EventResult::CONSUMED;
    };

    setColor4(m_color);
}

ColorField::ColorField(Amethyst::UIScope &parent, const Amethyst::Color4 &initial, std::vector<std::string> classes)
    : m_classes(std::move(classes)), m_color(initial), m_rgb(initial)
{
    buildField(parent);

    m_swatch->onMouseButton1ClickCb = [this]() {
        if (m_popup == nullptr) {
            m_popup = s_createPopup(*m_root, m_classes);
            Amethyst::UIScope(*m_popup).color4Picker(
                {
                    .classes = m_classes,
                    .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                    .value = &m_color,
                },
                [this](Amethyst::Color4PickerScope &s) {
                    m_picker4 = &s.component;
                    m_picker4->onValueChanged = [this](const Amethyst::Color4 &c) {
                        m_color = c;
                        m_rgb = Amethyst::Color3(c);
                        m_swatch->setBaseStyleProperties({.backgroundColor = Amethyst::Color3(m_color)});
                        m_hexInput->setText(s_toHexString(m_color, true));
                        if (onColorChanged) {
                            onColorChanged(m_color);
                        }
                    };
                });
        }
        if (m_popup->isOpen()) {
            m_popup->close();
        } else {
            Amethyst::vec2 anchor = m_swatch->absolutePosition;
            m_popup->openAt(Amethyst::vec2(anchor.x - POPUP_WIDTH, anchor.y - POPUP_HEIGHT));
        }
        return Amethyst::EventResult::CONSUMED;
    };

    setColor4(m_color);
}

void ColorField::setColor3(const Amethyst::Color3 &color)
{
    setColor4(Amethyst::Color4(color, 1.0f));
}

void ColorField::setColor4(const Amethyst::Color4 &color)
{
    m_color = color;
    m_rgb = Amethyst::Color3(color);
    if (m_swatch != nullptr) {
        m_swatch->setBaseStyleProperties({.backgroundColor = Amethyst::Color3(m_color)});
    }
    if (m_hexInput != nullptr) {
        m_hexInput->setText(s_toHexString(m_color, m_picker4 != nullptr));
    }
    if (m_picker3 != nullptr) {
        m_picker3->syncFromValue();
    }
    if (m_picker4 != nullptr) {
        m_picker4->syncFromValue();
    }
}

void ColorField::buildField(Amethyst::UIScope &parent)
{
    parent.frame(
        {
            .classes = m_classes,
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        },
        [this](Amethyst::FrameScope &f) {
            m_root = &f.component;

            f.imageButton(
                {
                    .classes = m_classes,
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .position = Amethyst::UDim2(0.0f, 0.0f, 0.5f, 0.0f),
                            .size = Amethyst::UDim2::fromOffset(SWATCH_SIZE, SWATCH_SIZE),
                        },
                },
                [this](Amethyst::ImageButtonScope &b) { m_swatch = &b.component; });

            f.textInput(
                {
                    .classes = m_classes,
                    .base =
                        {
                            .position = Amethyst::UDim2(0.0f, SWATCH_SIZE + SWATCH_GAP, 0.0f, 0.0f),
                            .size = Amethyst::UDim2(1.0f, -(SWATCH_SIZE + SWATCH_GAP), 1.0f, 0.0f),
                        },
                    .placeholder = "#RRGGBB",
                },
                [this](Amethyst::TextInputScope &ti) {
                    m_hexInput = &ti.component;
                    auto commit = [this]() {
                        if (auto parsed = s_parseHex(m_hexInput->getText())) {
                            Amethyst::Color4 c = *parsed;
                            if (m_picker4 == nullptr) {
                                c.a = 1.0f;
                            }
                            setColor4(c);
                            if (onColorChanged) {
                                onColorChanged(m_color);
                            }
                        } else {
                            m_hexInput->setText(s_toHexString(m_color, m_picker4 != nullptr));
                        }
                    };
                    m_hexInput->onEnterPressed = commit;
                    m_hexInput->onFocusLost = commit;
                });
        });
}
