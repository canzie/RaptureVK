#include "ComponentEditors.h"

#include "components/Components.h"

#include <components/checkbox.h>
#include <components/common.h>
#include <components/table.h>

#include <functional>
#include <glm/glm.hpp>
#include <modules/color.h>
#include <string>
#include <string_view>
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
        .classes = {"property-field"},
        .base = {.position = Amethyst::UDim2(0.0f, LABEL_PAD, 0.0f, 0.0f), .size = Amethyst::UDim2(1.0f, -LABEL_PAD, 1.0f, 0.0f)},
        .text = {.textColor = Amethyst::Color4::fromHex(0xffffff9e, true),
                 .textXAlignment = Amethyst::TextXAlignment::LEFT,
                 .textYAlignment = Amethyst::TextYAlignment::CENTER},
        .label = std::string(label),
    });
}

static float s_fieldTable(Amethyst::CollapsibleHeaderScope &ch, const std::function<void(Amethyst::TableScope &)> &fn)
{
    float height = 0.0f;
    ch.table(
        {
            .style =
                {
                    .backgroundTransparency = 1.0f,
                    .borderMode = Amethyst::BorderMode::OUTLINE,
                    .borderPixelSize = 1.0f,
                    .borderColor = Amethyst::Color3::fromHex(0x181818),
                },
            .table =
                {
                    .rowHeight = ROW_HEIGHT,
                    .separatorMode = Amethyst::TableSeparatorMode::BOTH,
                    .separatorColor = Amethyst::Color4::fromHex(0x181818),
                    .showHeader = false,
                    .rowBackgroundColor = Amethyst::Color4(0.0f, 0.0f, 0.0f, 0.0f),
                    .rowAlternateColor = Amethyst::Color4(0.0f, 0.0f, 0.0f, 0.0f),
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
                      const std::function<void()> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([&values, speed, min, max, onChanged](Amethyst::UIScope &cell) {
            const float w = 1.0f / 3.0f;
            for (int axis = 0; axis < 3; ++axis) {
                cell.dragFloat(
                    {
                        .classes = {"generic-input-field"},
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
                        const std::function<void(float)> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([value, min, max, onChanged](Amethyst::UIScope &cell) {
            cell.sliderFloat(
                {
                    .classes = {"generic-input-field"},
                    .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -2.0f * CONTROL_HPAD, 1.0f, -2.0f * CONTROL_VPAD)},
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
            cell.checkbox({.classes = {"generic-input-field"},
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
                                std::vector<std::string>{"generic-input-field"});
                    out->onColorChanged = [onChanged](const Amethyst::Color4 &c) {
                        if (onChanged) {
                            onChanged(glm::vec3(c.r, c.g, c.b));
                        }
                    };
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
                    .classes = {"generic-input-field"},
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
                           const std::function<void(double)> &onChanged)
{
    t.row([&](Amethyst::TableRowScope &tr) {
        tr.cell([label](Amethyst::UIScope &cell) { s_labelCell(cell, label); });
        tr.cell([value, speed, min, max, onChanged](Amethyst::UIScope &cell) {
            cell.dragFloat(
                {
                    .classes = {"generic-input-field"},
                    .base = {.anchorPoint = glm::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, CONTROL_HPAD, 0.5f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -2.0f * CONTROL_HPAD, 1.0f, -2.0f * CONTROL_VPAD)},
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

static Amethyst::Dropdown *s_rowMobility(Amethyst::TableScope &t, Rapture::Mobility current,
                                         const std::function<void(Rapture::Mobility)> &onSelect)
{
    return s_rowDropdown(t, "Mobility", Rapture::mobilityToString(current), {"Static", "Dynamic"}, [onSelect](int index) {
        if (onSelect) {
            onSelect(static_cast<Rapture::Mobility>(index));
        }
    });
}

void TransformEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowVec3(t, "Translation", m_values[0], 0.1, -100000.0, 100000.0, [this]() { apply(0); });
        s_rowVec3(t, "Rotation", m_values[1], 0.5, -360.0, 360.0, [this]() { apply(1); });
        s_rowVec3(t, "Scale", m_values[2], 0.01, -1000.0, 1000.0, [this]() { apply(2); });
    });
}

void TransformEditor::apply(int row)
{
    if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::TransformComponent>()) {
        return;
    }
    auto &tc = m_entity.getComponent<Rapture::TransformComponent>();
    glm::vec3 v(static_cast<float>(m_values[row][0]), static_cast<float>(m_values[row][1]), static_cast<float>(m_values[row][2]));
    if (row == 0) {
        tc.transforms.setTranslation(v);
    } else if (row == 1) {
        tc.transforms.setRotation(v);
    } else {
        tc.transforms.setScale(v);
    }
    m_entity.markDirty();
}

void TransformEditor::sync(const Rapture::Entity &entity)
{
    m_entity = entity;
    if (!entity.hasComponent<Rapture::TransformComponent>()) {
        return;
    }
    const auto &tc = entity.getComponent<Rapture::TransformComponent>();
    glm::vec3 t = tc.translation();
    glm::vec3 r = tc.rotation();
    glm::vec3 s = tc.scale();
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

void LightEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    std::vector<std::string> typeOptions;
    for (int i = 0; i <= static_cast<int>(Rapture::LightType::SPOT); ++i) {
        typeOptions.push_back(Rapture::lightTypeToString(static_cast<Rapture::LightType>(i)));
    }

    m_bodyHeight = s_fieldTable(ch, [this, typeOptions](Amethyst::TableScope &t) {
        m_typeDropdown =
            s_rowDropdown(t, "Type", Rapture::lightTypeToString(Rapture::LightType::POINT), typeOptions, [this](int index) {
                if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::LightComponent>()) {
                    return;
                }
                auto type = static_cast<Rapture::LightType>(index);
                m_entity.getComponent<Rapture::LightComponent>().setType(type);
                if (m_typeDropdown != nullptr) {
                    m_typeDropdown->setText(Rapture::lightTypeToString(type));
                }
                m_entity.markDirty();
            });
        s_rowColor(t, "Color", m_colorField, m_color, [this](const glm::vec3 &c) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::LightComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::LightComponent>().setColor(c);
            m_entity.markDirty();
        });
        s_rowSlider(t, "Intensity", &m_intensity, 0.0f, 100.0f, [this](float v) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::LightComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::LightComponent>().setIntensity(v);
            m_entity.markDirty();
        });
        s_rowSlider(t, "Range", &m_range, 0.0f, 1000.0f, [this](float v) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::LightComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::LightComponent>().setRange(v);
            m_entity.markDirty();
        });
        s_rowCheckbox(t, "Casts Shadow", &m_castsShadow, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::LightComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::LightComponent>().setCastsShadow(b);
            m_entity.markDirty();
        });
    });
}

void LightEditor::sync(const Rapture::Entity &entity)
{
    bool entityChanged = !(entity == m_entity);
    m_entity = entity;
    if (!entity.hasComponent<Rapture::LightComponent>()) {
        return;
    }
    const auto &lc = entity.getComponent<Rapture::LightComponent>();
    m_color = lc.color;
    m_intensity = lc.intensity;
    m_range = lc.range;
    m_castsShadow = lc.castsShadow;
    if (m_colorField) {
        m_colorField->setColor3(Amethyst::Color3(lc.color.x, lc.color.y, lc.color.z));
    }

    if (entityChanged && m_typeDropdown != nullptr) {
        m_typeDropdown->setText(Rapture::lightTypeToString(lc.type));
    }
}

void MeshEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_mobilityDropdown = s_rowMobility(t, Rapture::MOBILITY_STATIC, [this](Rapture::Mobility m) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::MeshComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::MeshComponent>().mobility = m;
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
            m_entity.markDirty();
        });
        s_rowCheckbox(t, "Enabled", &m_isEnabled, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::MeshComponent>()) {
                return;
            }
            m_entity.getComponent<Rapture::MeshComponent>().isEnabled = b;
            m_entity.markDirty();
        });
    });
}

void MeshEditor::sync(const Rapture::Entity &entity)
{
    bool entityChanged = !(entity == m_entity);
    m_entity = entity;
    if (!entity.hasComponent<Rapture::MeshComponent>()) {
        return;
    }
    const auto &mc = entity.getComponent<Rapture::MeshComponent>();
    m_isEnabled = mc.isEnabled;

    if (entityChanged && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(mc.mobility));
    }
}

void CameraEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    m_bodyHeight = s_fieldTable(ch, [this](Amethyst::TableScope &t) {
        s_rowSlider(t, "FOV", &m_fov, 1.0f, 179.0f, [this](float v) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::CameraComponent>()) {
                return;
            }
            auto &cc = m_entity.getComponent<Rapture::CameraComponent>();
            cc.updateProjectionMatrix(v, cc.aspectRatio, cc.nearPlane, cc.farPlane);
            m_entity.markDirty();
        });
        s_rowDragFloat(t, "Near Plane", &m_nearPlane, 0.01, 0.001, 10000.0, [this](double v) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::CameraComponent>()) {
                return;
            }
            auto &cc = m_entity.getComponent<Rapture::CameraComponent>();
            cc.updateProjectionMatrix(cc.fov, cc.aspectRatio, static_cast<float>(v), cc.farPlane);
            m_entity.markDirty();
        });
        s_rowDragFloat(t, "Far Plane", &m_farPlane, 1.0, 0.001, 1000000.0, [this](double v) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::CameraComponent>()) {
                return;
            }
            auto &cc = m_entity.getComponent<Rapture::CameraComponent>();
            cc.updateProjectionMatrix(cc.fov, cc.aspectRatio, cc.nearPlane, static_cast<float>(v));
            m_entity.markDirty();
        });
        s_rowCheckbox(t, "Main Camera", &m_isMainCamera, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.hasComponent<Rapture::CameraComponent>()) {
                return;
            }
            if (b) {
                if (m_entity.getScene() != nullptr) {
                    m_entity.getScene()->setMainCamera(m_entity);
                }
            } else {
                m_entity.getComponent<Rapture::CameraComponent>().isMainCamera = false;
            }
            m_entity.markDirty();
        });
    });
}

void CameraEditor::sync(const Rapture::Entity &entity)
{
    m_entity = entity;
    if (!entity.hasComponent<Rapture::CameraComponent>()) {
        return;
    }
    const auto &cc = entity.getComponent<Rapture::CameraComponent>();
    m_fov = cc.fov;
    m_nearPlane = cc.nearPlane;
    m_farPlane = cc.farPlane;
    m_isMainCamera = cc.isMainCamera;
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
            m_entity.getComponent<Rapture::ShadowComponent>().mobility = m;
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
            m_entity.getComponent<Rapture::CascadedShadowComponent>().mobility = m;
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
            if (csc.cascadedShadowMap != nullptr) {
                csc.cascadedShadowMap->setLambda(v);
            }
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
    if (csc.cascadedShadowMap != nullptr) {
        m_lambda = csc.cascadedShadowMap->getLambda();
    }

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

void StubEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    ch.textLabel({
        .classes = {"property-field"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(4.0f, 4.0f),
                .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 28.0f),
            },
        .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
        .label = "Not yet implemented",
    });
}
