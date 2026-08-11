#include "ComponentEditors.h"

#include "components/Components.h"
#include "scenes/instances/Environment.h"
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

template <typename T>
static T *s_instanceAs(Rapture::Scene *scene, const Rapture::ecs::EntityAccessor &entity)
{
    if (!entity.isValid() || scene == nullptr) {
        return nullptr;
    }

    Rapture::Instance *instance = scene->instanceFor(entity.getEntity());
    return instance != nullptr ? instance->as<T>() : nullptr;
}

Amethyst::Dropdown *ComponentEditorBase::rowMobility(Amethyst::TableScope &t, Rapture::Mobility current,
                                                     const std::function<void(Rapture::Mobility)> &onSelect)
{
    return rowDropdown(t, "Mobility", Rapture::mobilityToString(current), {"Static", "Dynamic"}, [onSelect](int index) {
        if (onSelect) {
            onSelect(static_cast<Rapture::Mobility>(index));
        }
    });
}

void Node3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowVec3(t, "Translation", m_values[0], 0.1, -100000.0, 100000.0, [this]() { apply(0); });
        rowVec3(t, "Rotation", m_values[1], 0.5, -360.0, 360.0, [this]() { apply(1); });
        rowVec3(t, "Scale", m_values[2], 0.01, -1000.0, 1000.0, [this]() { apply(2); });
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

void Node3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::Node3D>(scene, entity);
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
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowColor(t, "Color", m_colorField, m_color, [this](const glm::vec3 &c) {
            if (m_node != nullptr) {
                m_node->setColor(c);
            }
        });
        rowSlider(t, "Intensity", &m_intensity, 0.0f, 100.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setIntensity(v);
            }
        });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_STATIC, [this](Rapture::Mobility m) {
            if (m_node == nullptr) {
                return;
            }
            m_node->setMobility(m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setActive(b);
            }
        });
        rowCheckbox(t, "Casts Shadow", &m_castsShadow, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setCastsShadow(b);
            }
        });
        rowCheckbox(t, "Use Temperature", &m_usesTemperature, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setUsesTemperature(b);
            }
        });
        rowSlider(
            t, "Temperature", &m_temperature, 1000.0f, 40000.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setTemperature(v);
                }
            },
            "%.1f K");
    });
}

void Light3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    Rapture::Light3D *previous = m_node;
    m_node = s_instanceAs<Rapture::Light3D>(scene, entity);
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
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowCheckbox(t, "Atmosphere Sun", &m_atmosphereSun, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setAtmosphereSun(b);
            }
        });
    });
}

void DirectionalLight3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::DirectionalLight3D>(scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_atmosphereSun = m_node->isAtmosphereSun();
}

void PointLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Range", &m_range, 0.0f, 1000.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setRange(v);
            }
        });
    });
}

void PointLight3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::PointLight3D>(scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_range = m_node->range();
}

void SpotLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Range", &m_range, 0.0f, 1000.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setRange(v);
            }
        });
        rowSlider(
            t, "Inner Cone", &m_innerConeAngle, 0.0f, 89.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setInnerConeAngle(glm::radians(v));
                }
            },
            "%.1f deg");
        rowSlider(
            t, "Outer Cone", &m_outerConeAngle, 0.0f, 90.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setOuterConeAngle(glm::radians(v));
                }
            },
            "%.1f deg");
    });
}

void SpotLight3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::SpotLight3D>(scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_range = m_node->range();
    m_innerConeAngle = glm::degrees(m_node->innerConeAngle());
    m_outerConeAngle = glm::degrees(m_node->outerConeAngle());
}

void Mesh3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowAssetPicker(t, "Material", m_materialPicker, {.types = {Rapture::ASSET_MATERIAL_INSTANCE}},
                         [this](Rapture::AssetHandle handle) {
                             if (m_node != nullptr) {
                                 m_node->setMaterial(handle);
                             }
                         });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_STATIC, [this](Rapture::Mobility m) {
            if (m_node == nullptr) {
                return;
            }
            m_node->setMobility(m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        rowCheckbox(t, "Visible", &m_isVisible, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setVisible(b);
            }
        });
        rowCheckbox(t, "Ray Traced", &m_isRayTraced, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setRayTraced(b);
            }
        });
    });
}

void Mesh3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    Rapture::Mesh3D *previous = m_node;
    m_node = s_instanceAs<Rapture::Mesh3D>(scene, entity);
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
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "FOV", &m_fieldOfView, 1.0f, 179.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setFieldOfView(v);
            }
        });
        rowDragFloat(t, "Near Plane", &m_nearPlane, 0.01, 0.001, 10000.0, {}, [this](double v) {
            if (m_node != nullptr) {
                m_node->setNearPlane(static_cast<float>(v));
            }
        });
        rowDragFloat(t, "Far Plane", &m_farPlane, 1.0, 0.001, 1000000.0, {}, [this](double v) {
            if (m_node != nullptr) {
                m_node->setFarPlane(static_cast<float>(v));
            }
        });
    });
}

void Camera3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::Camera3D>(scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_fieldOfView = m_node->fieldOfView();
    m_nearPlane = m_node->nearPlane();
    m_farPlane = m_node->farPlane();
}

void ShadowEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::ShadowComponent>()) {
                return;
            }
            m_entity.write<Rapture::ShadowComponent>()->isActive = b;
        });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_DYNAMIC, [this](Rapture::Mobility m) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::ShadowComponent>()) {
                return;
            }
            scene->getRenderData()->setShadowMobility(m_entity.getEntity(), m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
    });
}

void ShadowEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    bool entityChanged = !(entity == m_entity);
    m_entity = entity;
    if (!entity.has<Rapture::ShadowComponent>()) {
        return;
    }
    const auto &sc = entity.read<Rapture::ShadowComponent>();
    m_isActive = sc.isActive;

    if (entityChanged && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(sc.mobility));
    }
}

void CascadedShadowEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::CascadedShadowComponent>()) {
                return;
            }
            m_entity.write<Rapture::CascadedShadowComponent>()->isActive = b;
        });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_DYNAMIC, [this](Rapture::Mobility m) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::CascadedShadowComponent>()) {
                return;
            }
            scene->getRenderData()->setCascadedShadowMobility(m_entity.getEntity(), m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        rowSlider(t, "Lambda", &m_lambda, 0.0f, 1.0f, [this](float v) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::CascadedShadowComponent>()) {
                return;
            }
            auto csc = m_entity.write<Rapture::CascadedShadowComponent>();
            csc->lambda = std::clamp(v, 0.0f, 1.0f);
        });
    });
}

void CascadedShadowEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    bool entityChanged = !(entity == m_entity);
    m_entity = entity;
    if (!entity.has<Rapture::CascadedShadowComponent>()) {
        return;
    }
    const auto &csc = entity.read<Rapture::CascadedShadowComponent>();
    m_isActive = csc.isActive;
    m_lambda = csc.lambda;

    if (entityChanged && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(csc.mobility));
    }
}

void EnvironmentEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Sky Intensity", &m_skyIntensity, 0.0f, 10.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setSkyIntensity(v);
            }
        });
        rowCheckbox(t, "Sky Enabled", &m_skyboxEnabled, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setSkyboxEnabled(b);
            }
        });
        rowCheckbox(t, "Atmosphere Sky", &m_usesAtmosphereSkybox, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setUsesAtmosphereSkybox(b);
            }
        });
        rowSlider(t, "Time of Day", &m_atmosphere.timeOfDay, 0.0f, 24.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Latitude", &m_atmosphere.latitude, -90.0f, 90.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Longitude", &m_atmosphere.longitude, -180.0f, 180.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Mie", &m_atmosphere.mie, 0.0f, 60.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Mie G", &m_atmosphere.mieG, 0.0f, 0.999f, [this](float) { pushAtmosphere(); });
        rowDragFloat(t, "Sun Intensity", &m_sunIntensity, 0.1, 0.0, 1000.0, {}, [this](double v) {
            m_atmosphere.sunIntensity = static_cast<float>(v);
            pushAtmosphere();
        });
        rowDragFloat(t, "Camera Altitude", &m_cameraAltitude, 1.0, 0.0, 100000.0, {}, [this](double v) {
            m_atmosphere.cameraAltitude = static_cast<float>(v);
            pushAtmosphere();
        });
        rowVec3(t, "Wavelength (nm)", m_wavelengths, 1.0, 380.0, 740.0, [this]() {
            m_atmosphere.rayleigh = glm::vec3(Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[0])),
                                              Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[1])),
                                              Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[2])));
            pushAtmosphere();
        });
    });
}

void EnvironmentEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::Environment>(scene, entity);
    if (m_node == nullptr) {
        return;
    }

    m_skyIntensity = m_node->skyIntensity();
    m_skyboxEnabled = m_node->isSkyboxEnabled();
    m_usesAtmosphereSkybox = m_node->usesAtmosphereSkybox();

    m_atmosphere = m_node->atmosphere();
    m_sunIntensity = m_atmosphere.sunIntensity;
    m_cameraAltitude = m_atmosphere.cameraAltitude;
    m_wavelengths[0] = Rapture::Environment::wavelengthNm(m_atmosphere.rayleigh.x);
    m_wavelengths[1] = Rapture::Environment::wavelengthNm(m_atmosphere.rayleigh.y);
    m_wavelengths[2] = Rapture::Environment::wavelengthNm(m_atmosphere.rayleigh.z);
}

void EnvironmentEditor::pushAtmosphere()
{
    if (m_node == nullptr) {
        return;
    }
    m_node->atmosphere() = m_atmosphere;
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
