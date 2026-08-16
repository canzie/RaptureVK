#include "property_sections.h"

#include "layers/panels/components/header_layouts.h"

#include <components/checkbox.h>
#include <components/common.h>

static constexpr float ROW_HEIGHT = 32.0f;
static constexpr float PICKER_ROW_HEIGHT = 52.0f;
static constexpr float PICKER_PREVIEW_SIZE = 28.0f;
static constexpr float LABEL_FRAC = 0.4f;
static constexpr float LABEL_PAD = 12.0f;
static constexpr float CONTROL_VPAD = 4.0f;
static constexpr float CONTROL_HPAD = 10.0f;
static constexpr float CONTROL_GAP = 6.0f;

static constexpr float HEADER_HEIGHT = PropertySection::HEADER_HEIGHT;
static constexpr float SECTION_SPACING = PropertySection::SECTION_SPACING;
static constexpr float SECTION_TOP_PAD = 4.0f;

static void s_labelCell(Amethyst::UIScope &cell, std::string_view label)
{
    cell.textLabel({
        .classes = {"property-label"},
        .base = {.position = Amethyst::UDim2(0.0f, LABEL_PAD, 0.0f, 0.0f), .size = Amethyst::UDim2(1.0f, -LABEL_PAD, 1.0f, 0.0f)},
        .label = std::string(label),
    });
}

void PropertySection::fieldTable(Amethyst::CollapsibleHeaderScope &ch, const std::function<void(Amethyst::TableScope &)> &fn)
{
    ch.table(
        {
            .table =
                {
                    .rowHeight = ROW_HEIGHT,
                    .separatorMode = Amethyst::TableSeparatorMode::BOTH,
                    .separatorColor = Amethyst::Color4::fromHex(0x181818),
                    .showHeader = false,
                    .rowBackgroundColor = Amethyst::Color4(0.0f, 0.0f, 0.0f, 0.0f),
                    .rowAlternateColor = Amethyst::Color4(0.0f, 0.0f, 0.0f, 0.0f),
                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::NEVER,
                },
        },
        [&](Amethyst::TableScope &t) {
            t.column("", LABEL_FRAC, Amethyst::TableColumnSizing::FIXED);
            t.column("", 1.0f - LABEL_FRAC);
            fn(t);
            m_bodyHeight = t.component.contentHeight();
            t.component.setBaseProperties({.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, m_bodyHeight)});
        });
}

void PropertySection::rowVec3(Amethyst::TableScope &t, std::string_view label, double (&values)[3], double speed, double min,
                              double max, const std::function<void(void)> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([&values, speed, min, max, onChanged](Amethyst::UIScope &cell) {
            const float w = 1.0f / 3.0f;
            for (int axis = 0; axis < 3; ++axis) {
                // the outer edges take the control padding, the inner ones half the gutter each
                float leftInset = axis == 0 ? CONTROL_HPAD : CONTROL_GAP * 0.5f;
                float rightInset = axis == 2 ? CONTROL_HPAD : CONTROL_GAP * 0.5f;

                cell.dragFloat(
                    {
                        .classes = {"property-input-field"},
                        .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                                 .position = Amethyst::UDim2(axis * w, leftInset, 0.5f, 0.0f),
                                 .size = Amethyst::UDim2(w, -(leftInset + rightInset), 1.0f, -2.0f * CONTROL_VPAD)},
                        .speed = speed,
                        .min = min,
                        .max = max,
                        .value = &values[axis],
                    },
                    [onChanged](Amethyst::DragFloatScope &d) {
                        d.component.onValueChanged = [onChanged](double) {
                            if (onChanged) {
                                onChanged();
                            }
                        };
                    });
            }
        });
    });
}

void PropertySection::rowSlider(Amethyst::TableScope &t, std::string_view label, float *value, float min, float max,
                                const std::function<void(float)> &onChanged, std::string format)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([value, min, max, onChanged, format](Amethyst::UIScope &cell) {
            cell.sliderFloat(
                {
                    .classes = {"property-input-field"},
                    .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -2.0f * CONTROL_HPAD, 1.0f, -2.0f * CONTROL_VPAD)},
                    .format = format,
                    .min = min,
                    .max = max,
                    .value = value,
                },
                [onChanged](Amethyst::SliderFloatScope &s) {
                    s.component.onValueChanged = [onChanged](float v) {
                        if (onChanged) {
                            onChanged(v);
                        }
                    };
                });
        });
    });
}

Amethyst::TextLabel *PropertySection::rowText(Amethyst::TableScope &t, std::string_view label, std::string_view value)
{
    Amethyst::TextLabel *result = nullptr;

    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([value, &result](Amethyst::UIScope &cell) {
            cell.textLabel(
                {
                    .classes = {"property-label"},
                    .base = {.position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.0f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -CONTROL_HPAD, 1.0f, 0.0f)},
                    .label = std::string(value),
                },
                [&result](Amethyst::TextLabelScope &tl) { result = &tl.component; });
        });
    });

    return result;
}

void PropertySection::rowCheckbox(Amethyst::TableScope &t, std::string_view label, bool *value,
                                  const std::function<void(bool)> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([value, onChanged](Amethyst::UIScope &cell) {
            cell.checkbox({.classes = {"property-input-field"},
                           .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                                    .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                                    .size = Amethyst::UDim2::fromOffset(18.0f, 18.0f)},
                           .value = value},
                          [value, onChanged](Amethyst::CheckboxScope &c) {
                              c.component.onValueChanged = [onChanged](bool b) {
                                  if (onChanged) {
                                      onChanged(b);
                                  }
                              };
                          });
        });
    });
}

void PropertySection::rowColor(Amethyst::TableScope &t, std::string_view label, std::optional<ColorField> &out,
                               const glm::vec3 &initial, const std::function<void(const glm::vec3 &)> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([&](Amethyst::UIScope &cell) {
            cell.frame(
                {
                    .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -2.0f * CONTROL_HPAD, 1.0f, -2.0f * CONTROL_VPAD)},
                    .style = {.backgroundTransparency = 1.0f},
                },
                [&](Amethyst::FrameScope &wrap) {
                    out.emplace(wrap, Amethyst::Color3(initial.x, initial.y, initial.z),
                                std::vector<std::string>{"property-input-field"});
                    out->onColorChanged = [onChanged](const Amethyst::Color4 &c) {
                        if (onChanged) {
                            onChanged(glm::vec3(c.r, c.g, c.b));
                        }
                    };
                });
        });
    });
}

void PropertySection::rowAssetPicker(Amethyst::TableScope &t, std::string_view label, std::optional<AssetPicker> &out,
                                     AssetPickerConfig config, const std::function<void(Rapture::AssetHandle)> &onSelected)
{
    config.previewSize = PICKER_PREVIEW_SIZE;

    t.row(PICKER_ROW_HEIGHT, [&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([&](Amethyst::UIScope &cell) {
            cell.frame(
                {
                    .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -2.0f * CONTROL_HPAD, 1.0f, -2.0f * CONTROL_VPAD)},
                    .style = {.backgroundTransparency = 1.0f},
                },
                [&](Amethyst::FrameScope &wrap) {
                    out.emplace(wrap, std::move(config), std::vector<std::string>{"property-input-field"});
                    out->onAssetSelected = onSelected;
                });
        });
    });
}

Amethyst::Dropdown *PropertySection::rowDropdown(Amethyst::TableScope &t, std::string_view label, std::string_view current,
                                                 const std::vector<std::string> &options, const std::function<void(int)> &onSelect)
{
    Amethyst::Dropdown *dropdown = nullptr;
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([&](Amethyst::UIScope &cell) {
            cell.dropdown(
                {
                    .classes = {"property-input-field"},
                    .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -2.0f * CONTROL_HPAD, 1.0f, -2.0f * CONTROL_VPAD)},
                    .text = {.textXAlignment = Amethyst::TextXAlignment::LEFT, .textYAlignment = Amethyst::TextYAlignment::CENTER},
                    .label = std::string(current),
                },
                [&](Amethyst::DropdownScope &d) {
                    dropdown = &d.component;
                    for (size_t i = 0; i < options.size(); ++i) {
                        d.action(options[i], [onSelect, i]() {
                            if (onSelect) {
                                onSelect(static_cast<int>(i));
                            }
                        });
                    }
                });
        });
    });
    return dropdown;
}

void PropertySection::rowDragFloat(Amethyst::TableScope &t, std::string_view label, double *value, double speed, double min,
                                   double max, std::string format, const std::function<void(double)> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([value, speed, min, max, format, onChanged](Amethyst::UIScope &cell) {
            cell.dragFloat(
                {
                    .classes = {"property-input-field"},
                    .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -2.0f * CONTROL_HPAD, 1.0f, -2.0f * CONTROL_VPAD)},
                    .format = format,
                    .speed = speed,
                    .min = min,
                    .max = max,
                    .value = value,
                },
                [onChanged](Amethyst::DragFloatScope &d) {
                    d.component.onValueChanged = [onChanged](double v) {
                        if (onChanged) {
                            onChanged(v);
                        }
                    };
                });
        });
    });
}

PropertySectionList::PropertySectionList(Amethyst::Frame &parent, Amethyst::ScrollingFrameProperties props)
{
    Amethyst::UIScope(parent).scrollingFrame(std::move(props), [this](Amethyst::ScrollingFrameScope &sf) {
        m_view = &sf.component;
        m_viewDestroyConn = m_view->onDestroy.connect([this](Amethyst::Instance *) {
            m_view = nullptr;
            m_active.clear();
            m_sections.clear();
        });
    });
}

void PropertySectionList::refresh(const std::function<void()> &fn)
{
    m_active.clear();
    if (fn) {
        fn();
    }
    relayout();
}

bool PropertySectionList::consumeRefreshRequest()
{
    const bool requested = m_refreshRequested;
    m_refreshRequested = false;
    return requested;
}

void PropertySectionList::buildSection(PropertySection &section)
{
    if (m_view == nullptr) {
        return;
    }

    section.requestRefresh = [this]() { m_refreshRequested = true; };
    section.requestRelayout = [this]() { relayout(); };

    Amethyst::UIScope(*m_view).collapsibleHeader(
        {
            .classes = {"component-header"},
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, SECTION_TOP_PAD),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, HEADER_HEIGHT),
                },
            .style = {.backgroundTransparency = 1.0f},
            .header =
                {
                    .titleStyle = {.fontSize = 13.0f},
                    .headerHeight = HEADER_HEIGHT,
                },
        },
        [&](Amethyst::CollapsibleHeaderScope &ch) {
            section.header = &ch.component;
            ch.header(componentHeaderLayout(section.title(), section.icon()));
            section.buildBody(ch);
            section.header->setBaseProperties({.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, HEADER_HEIGHT + section.bodyHeight())});
        });

    section.header->onToggled = [this](bool) { relayout(); };
}

void PropertySectionList::destroySection(PropertySection &section)
{
    if (m_view != nullptr && section.header != nullptr) {
        m_view->removeChild(section.header);
    }
    std::erase(m_active, &section);
}

void PropertySectionList::relayout()
{
    if (m_view == nullptr) {
        return;
    }

    float y = SECTION_TOP_PAD;
    for (PropertySection *section : m_active) {
        bool expanded = static_cast<bool>(section->header->getCollapsibleHeaderProperties().expanded);
        float bodyHeight = expanded ? section->bodyHeight() : 0.0f;

        section->header->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(0.0f, y),
            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, HEADER_HEIGHT + bodyHeight),
        });

        y += HEADER_HEIGHT + bodyHeight + SECTION_SPACING;
    }

    m_view->setScrollingFrameProperties({.canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, y))});
}

void PropertySectionList::sync()
{
    for (PropertySection *section : m_active) {
        section->sync();
    }
}

void PropertySectionList::setVisible(bool visible)
{
    if (m_view != nullptr) {
        m_view->setBaseProperties({.visible = visible});
    }
}
