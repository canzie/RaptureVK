#include "ComponentEditors.h"

#include "components/Components.h"
#include "components/systems/Environment.h"
#include "logging/Log.h"
#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"

#include <algorithm>

#include <components/checkbox.h>
#include <components/common.h>
#include <components/table.h>

#include <functional>
#include <glm/glm.hpp>
#include <modules/color.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static constexpr float ROW_HEIGHT = 32.0f;
static constexpr float LABEL_FRAC = 0.4f;
static constexpr float LABEL_PAD = 12.0f;
static constexpr float CONTROL_VPAD = 4.0f;
static constexpr float CONTROL_HPAD = 8.0f;

static float s_tableHeight(uint32_t rows)
{
    return static_cast<float>(rows) * ROW_HEIGHT;
}

static void s_labelCell(Amethyst::UIScope &cell, std::string_view label)
{
    cell.textLabel({
        .classes = {"property-label"},
        .base = {.position = Amethyst::UDim2(0.0f, LABEL_PAD, 0.0f, 0.0f), .size = Amethyst::UDim2(1.0f, -LABEL_PAD, 1.0f, 0.0f)},
        .label = std::string(label),
    });
}

static float s_fieldTable(Amethyst::CollapsibleHeaderScope &ch, const std::function<void(Amethyst::TableScope &)> &fn)
{
    float height = 0.0f;
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
            height = s_tableHeight(t.component.rowCount());
            t.component.setBaseProperties({.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, height)});
        });
    return height;
}

static void s_rowVec3(Amethyst::TableScope &t, std::string_view label, double (&values)[3], double speed, double min, double max,
                      const std::function<void(void)> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([&values, speed, min, max, onChanged](Amethyst::UIScope &cell) {
            const float w = 1.0f / 3.0f;
            for (int axis = 0; axis < 3; ++axis) {
                cell.dragFloat(
                    {
                        .classes = {"property-input-field"},
                        .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                                 .position = Amethyst::UDim2(axis * w, axis == 0 ? CONTROL_HPAD : 2.0f, 0.5f, 0.0f),
                                 .size = Amethyst::UDim2(w, axis == 2 ? -2.0f - CONTROL_HPAD : -2.0f, 1.0f, -2.0f * CONTROL_VPAD)},
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

static void s_rowSlider(Amethyst::TableScope &t, std::string_view label, float *value, float min, float max,
                        const std::function<void(float)> &onChanged, std::string format = {})
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

static void s_rowCheckbox(Amethyst::TableScope &t, std::string_view label, bool *value, const std::function<void(bool)> &onChanged)
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

static void s_rowColor(Amethyst::TableScope &t, std::string_view label, std::optional<ColorField> &out, const glm::vec3 &initial,
                       const std::function<void(const glm::vec3 &)> &onChanged)
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

static void s_rowAssetPicker(Amethyst::TableScope &t, std::string_view label, std::optional<AssetPicker> &out,
                             AssetPickerConfig config, const std::function<void(Rapture::AssetHandle)> &onSelected)
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
                    out.emplace(wrap, std::move(config), std::vector<std::string>{"property-input-field"});
                    out->onAssetSelected = onSelected;
                });
        });
    });
}

static Amethyst::Dropdown *s_rowDropdown(Amethyst::TableScope &t, std::string_view label, std::string_view current,
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

static void s_rowDragFloat(Amethyst::TableScope &t, std::string_view label, double *value, double speed, double min, double max,
                           std::string format, const std::function<void(double)> &onChanged)
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

template <typename T>
static T *s_instanceAs(const Rapture::Entity &entity)
{
    if (!entity.isValid() || entity.getScene() == nullptr) {
        return nullptr;
    }

    Rapture::Instance *instance = entity.getScene()->instanceFor(entity);
    return instance != nullptr ? instance->as<T>() : nullptr;
}

static Amethyst::Dropdown *s_rowMobility(Amethyst::TableScope &t, Rapture::Mobility current,
                                         const std::function<void(Rapture::Mobility)> &onSelect)
{
    return s_rowDropdown(t, "Mobility", Rapture::mobilityToString(current), {"Static", "Dynamic"}, [onSelect](int index) {
        if (onSelect) {
            onSelect(static_cast<Rapture::Mobility>(index));
        }
    });
}

void Node3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowVec3(t, "Translation", m_values[0], 0.1, -100000.0, 100000.0, [this]() { apply(0); });
        s_rowVec3(t, "Rotation", m_values[1], 0.5, -360.0, 360.0, [this]() { apply(1); });
        s_rowVec3(t, "Scale", m_values[2], 0.01, -1000.0, 1000.0, [this]() { apply(2); });
    });
}

void Node3DEditor::apply(int row)
{
    if (m_node == nullptr) {
        return;
    }
    glm::vec3 v(static_cast<float>(m_values[row][0]), static_cast<float>(m_values[row][1]), static_cast<float>(m_values[row][2]));
    if (row == 0) {
        m_node->setPosition(v);
    } else if (row == 1) {
        m_node->setRotation(v);
    } else {
        m_node->setScale(v);
    }
}

void Node3DEditor::sync(const Rapture::Entity &entity)
{
    m_node = s_instanceAs<Rapture::Node3D>(entity);
    if (m_node == nullptr) {
        return;
    }
    glm::vec3 t = m_node->position();
    glm::vec3 r = m_node->rotation();
    glm::vec3 s = m_node->scale();
    m_values[0][0] = t.x;
    m_values[0][1] = t.y;
    m_values[0][2] = t.z;
    m_values[1][0] = r.x;
    m_values[1][1] = r.y;
    m_values[1][2] = r.z;
    m_values[2][0] = s.x;
    m_values[2][1] = s.y;
    m_values[2][2] = s.z;
}

void Light3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowColor(t, "Color", m_colorField, m_color, [this](const glm::vec3 &c) {
            if (m_node != nullptr) {
                m_node->setColor(c);
            }
        });
        s_rowSlider(t, "Intensity", &m_intensity, 0.0f, 100.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setIntensity(v);
            }
        });
        m_mobilityDropdown = s_rowMobility(t, Rapture::MOBILITY_STATIC, [this](Rapture::Mobility m) {
            if (m_node == nullptr) {
                return;
            }
            m_node->setMobility(m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        s_rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setActive(b);
            }
        });
        s_rowCheckbox(t, "Casts Shadow", &m_castsShadow, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setCastsShadow(b);
            }
        });
        s_rowCheckbox(t, "Use Temperature", &m_usesTemperature, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setUsesTemperature(b);
            }
        });
        s_rowSlider(
            t, "Temperature", &m_temperature, 1000.0f, 40000.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setTemperature(v);
                }
            },
            "%.1f K");
    });
}

void Light3DEditor::sync(const Rapture::Entity &entity)
{
    Rapture::Light3D *previous = m_node;
    m_node = s_instanceAs<Rapture::Light3D>(entity);
    if (m_node == nullptr) {
        return;
    }

    m_color = m_node->color();
    m_intensity = m_node->intensity();
    m_castsShadow = m_node->castsShadow();
    m_usesTemperature = m_node->usesTemperature();
    m_temperature = m_node->temperature();
    m_isActive = m_node->isActive();

    if (m_colorField) {
        m_colorField->setColor3(Amethyst::Color3(m_color.x, m_color.y, m_color.z));
    }

    if (previous != m_node && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(m_node->mobility()));
    }
}

void DirectionalLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowCheckbox(t, "Atmosphere Sun", &m_atmosphereSun, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setAtmosphereSun(b);
            }
        });
    });
}

void DirectionalLight3DEditor::sync(const Rapture::Entity &entity)
{
    m_node = s_instanceAs<Rapture::DirectionalLight3D>(entity);
    if (m_node == nullptr) {
        return;
    }
    m_atmosphereSun = m_node->isAtmosphereSun();
}

void PointLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowSlider(t, "Range", &m_range, 0.0f, 1000.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setRange(v);
            }
        });
    });
}

void PointLight3DEditor::sync(const Rapture::Entity &entity)
{
    m_node = s_instanceAs<Rapture::PointLight3D>(entity);
    if (m_node == nullptr) {
        return;
    }
    m_range = m_node->range();
}

void SpotLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowSlider(t, "Range", &m_range, 0.0f, 1000.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setRange(v);
            }
        });
        s_rowSlider(
            t, "Inner Cone", &m_innerConeAngle, 0.0f, 89.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setInnerConeAngle(glm::radians(v));
                }
            },
            "%.1f deg");
        s_rowSlider(
            t, "Outer Cone", &m_outerConeAngle, 0.0f, 90.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setOuterConeAngle(glm::radians(v));
                }
            },
            "%.1f deg");
    });
}

void SpotLight3DEditor::sync(const Rapture::Entity &entity)
{
    m_node = s_instanceAs<Rapture::SpotLight3D>(entity);
    if (m_node == nullptr) {
        return;
    }
    m_range = m_node->range();
    m_innerConeAngle = glm::degrees(m_node->innerConeAngle());
    m_outerConeAngle = glm::degrees(m_node->outerConeAngle());
}

void Mesh3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowAssetPicker(t, "Material", m_materialPicker, {.types = {Rapture::AssetType::MATERIAL_INSTANCE}},
                         [this](Rapture::AssetHandle handle) {
                             if (m_node != nullptr) {
                                 m_node->setMaterial(handle);
                             }
                         });
        m_mobilityDropdown = s_rowMobility(t, Rapture::MOBILITY_STATIC, [this](Rapture::Mobility m) {
            if (m_node == nullptr) {
                return;
            }
            m_node->setMobility(m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        s_rowCheckbox(t, "Visible", &m_isVisible, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setVisible(b);
            }
        });
        s_rowCheckbox(t, "Ray Traced", &m_isRayTraced, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setRayTraced(b);
            }
        });
    });
}

void Mesh3DEditor::sync(const Rapture::Entity &entity)
{
    Rapture::Mesh3D *previous = m_node;
    m_node = s_instanceAs<Rapture::Mesh3D>(entity);
    if (m_node == nullptr) {
        return;
    }

    m_isVisible = m_node->isVisible();
    m_isRayTraced = m_node->isRayTraced();

    if (previous != m_node) {
        if (m_mobilityDropdown != nullptr) {
            m_mobilityDropdown->setText(Rapture::mobilityToString(m_node->mobility()));
        }
        if (m_materialPicker.has_value()) {
            m_materialPicker->setAsset(m_node->material());
        }
    }
}

void Camera3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowSlider(t, "FOV", &m_fieldOfView, 1.0f, 179.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setFieldOfView(v);
            }
        });
        s_rowDragFloat(t, "Near Plane", &m_nearPlane, 0.01, 0.001, 10000.0, {}, [this](double v) {
            if (m_node != nullptr) {
                m_node->setNearPlane(static_cast<float>(v));
            }
        });
        s_rowDragFloat(t, "Far Plane", &m_farPlane, 1.0, 0.001, 1000000.0, {}, [this](double v) {
            if (m_node != nullptr) {
                m_node->setFarPlane(static_cast<float>(v));
            }
        });
    });
}

void Camera3DEditor::sync(const Rapture::Entity &entity)
{
    m_node = s_instanceAs<Rapture::Camera3D>(entity);
    if (m_node == nullptr) {
        return;
    }
    m_fieldOfView = m_node->fieldOfView();
    m_nearPlane = m_node->nearPlane();
    m_farPlane = m_node->farPlane();
}

void ShadowEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::ShadowComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::ShadowComponent>().isActive = b;
            m_entity.markDirty();
        });
        m_mobilityDropdown = s_rowMobility(t, Rapture::MOBILITY_DYNAMIC, [this](Rapture::Mobility m) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::ShadowComponent>()) {
                return;
            }
            m_entity.getScene()->getRenderData()->setShadowMobility(m_entity.getID(), m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
            m_entity.markDirty();
        });
    });
}

void ShadowEditor::sync(const Rapture::Entity &entity)
{
    bool entityChanged = !(entity == m_entity);
    m_entity = entity;
    if (!entity.hasComponent<Rapture::ShadowComponent>()) {
        return;
    }
    const auto &sc = entity.getComponent<Rapture::ShadowComponent>();
    m_isActive = sc.isActive;

    if (entityChanged && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(sc.mobility));
    }
}

void CascadedShadowEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::CascadedShadowComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::CascadedShadowComponent>().isActive = b;
            m_entity.markDirty();
        });
        m_mobilityDropdown = s_rowMobility(t, Rapture::MOBILITY_DYNAMIC, [this](Rapture::Mobility m) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::CascadedShadowComponent>()) {
                return;
            }
            m_entity.getScene()->getRenderData()->setCascadedShadowMobility(m_entity.getID(), m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
            m_entity.markDirty();
        });
        s_rowSlider(t, "Lambda", &m_lambda, 0.0f, 1.0f, [this](float v) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::CascadedShadowComponent>()) {
                return;
            }
            auto &csc = m_entity.getComponent<Rapture::CascadedShadowComponent>();
            csc.lambda = std::clamp(v, 0.0f, 1.0f);
            m_entity.markDirty();
        });
    });
}

void CascadedShadowEditor::sync(const Rapture::Entity &entity)
{
    bool entityChanged = !(entity == m_entity);
    m_entity = entity;
    if (!entity.hasComponent<Rapture::CascadedShadowComponent>()) {
        return;
    }
    const auto &csc = entity.getComponent<Rapture::CascadedShadowComponent>();
    m_isActive = csc.isActive;
    m_lambda = csc.lambda;

    if (entityChanged && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(csc.mobility));
    }
}

void SkyboxEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowSlider(t, "Intensity", &m_intensity, 0.0f, 10.0f, [this](float v) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::SkyboxComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::SkyboxComponent>().skyIntensity = v;
            m_entity.markDirty();
        });
        s_rowCheckbox(t, "Enabled", &m_isEnabled, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::SkyboxComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::SkyboxComponent>().isEnabled = b;
            m_entity.markDirty();
        });
    });
}

void SkyboxEditor::sync(const Rapture::Entity &entity)
{
    m_entity = entity;
    if (!entity.hasComponent<Rapture::SkyboxComponent>()) {
        return;
    }
    const auto &sc = entity.getComponent<Rapture::SkyboxComponent>();
    m_intensity = sc.skyIntensity;
    m_isEnabled = sc.isEnabled;
}

void AtmosphereEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowSlider(t, "Time of Day", &m_component.timeOfDay, 0.0f, 24.0f, [this](float) { pushToComponent(); });
        s_rowSlider(t, "Latitude", &m_component.latitude, -90.0f, 90.0f, [this](float) { pushToComponent(); });
        s_rowSlider(t, "Longitude", &m_component.longitude, -180.0f, 180.0f, [this](float) { pushToComponent(); });
        s_rowSlider(t, "Mie", &m_component.mie, 0.0f, 60.0f, [this](float) { pushToComponent(); });
        s_rowSlider(t, "Mie G", &m_component.mieG, 0.0f, 0.999f, [this](float) { pushToComponent(); });
        s_rowDragFloat(t, "Sun Intensity", &m_sunIntensity, 0.1, 0.0, 1000.0, {}, [this](double v) {
            m_component.sunIntensity = static_cast<float>(v);
            pushToComponent();
        });
        s_rowDragFloat(t, "Camera Altitude", &m_cameraAltitude, 1.0, 0.0, 100000.0, {}, [this](double v) {
            m_component.cameraAltitude = static_cast<float>(v);
            pushToComponent();
        });
        s_rowVec3(t, "Wavelength (nm)", m_wavelengths, 1.0, 380.0, 740.0, [this]() {
            m_component.rayleigh = glm::vec3(Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[0])),
                                             Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[1])),
                                             Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[2])));
            pushToComponent();
        });
    });
}

void AtmosphereEditor::sync(const Rapture::Entity &entity)
{
    m_entity = entity;
    const auto *atmo = entity.tryGetComponent<Rapture::AtmosphereComponent>();
    if (atmo == nullptr) {
        return;
    }
    m_component = *atmo;
    m_sunIntensity = m_component.sunIntensity;
    m_cameraAltitude = m_component.cameraAltitude;
    m_wavelengths[0] = Rapture::Environment::wavelengthNm(m_component.rayleigh.x);
    m_wavelengths[1] = Rapture::Environment::wavelengthNm(m_component.rayleigh.y);
    m_wavelengths[2] = Rapture::Environment::wavelengthNm(m_component.rayleigh.z);
}

void AtmosphereEditor::pushToComponent()
{
    auto *atmo = m_entity.tryGetComponent<Rapture::AtmosphereComponent>();
    if (atmo == nullptr) {
        return;
    }
    *atmo = m_component;
    m_entity.markDirty();
}

void StubEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    ch.textLabel({
        .classes = {"property-label"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(4.0f, 4.0f),
                .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 28.0f),
            },
        .label = "Not yet implemented",
    });
}
