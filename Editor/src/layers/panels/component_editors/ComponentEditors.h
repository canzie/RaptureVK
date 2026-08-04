#ifndef RAPTURE__COMPONENT_EDITORS_H
#define RAPTURE__COMPONENT_EDITORS_H

#include "ComponentEditorBase.h"
#include "Icons.h"
#include "components/Components.h"
#include "layers/panels/components/asset_picker.h"
#include "layers/panels/components/color_field.h"
#include "scenes/instances/Camera3D.h"
#include "scenes/instances/DirectionalLight3D.h"
#include "scenes/instances/Mesh3D.h"
#include "scenes/instances/PointLight3D.h"
#include "scenes/instances/SpotLight3D.h"

#include <glm/glm.hpp>
#include <optional>

class Node3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Transform"; }
    const char *icon() const override { return Icons::SVG_TRANSFORM; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    void apply(int row);

    double m_values[3][3] = {};
    Rapture::Node3D *m_node = nullptr;
};

class Light3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    glm::vec3 m_color = glm::vec3(1.0f);
    float m_intensity = 1.0f;
    bool m_castsShadow = false;
    bool m_usesTemperature = false;
    float m_temperature = 6500.0f;
    bool m_isActive = true;

    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
    std::optional<ColorField> m_colorField;
    Rapture::Light3D *m_node = nullptr;
};

class DirectionalLight3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Directional Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    bool m_atmosphereSun = false;
    Rapture::DirectionalLight3D *m_node = nullptr;
};

class PointLight3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Point Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    float m_range = 10.0f;
    Rapture::PointLight3D *m_node = nullptr;
};

class SpotLight3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Spot Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    float m_range = 10.0f;
    float m_innerConeAngle = 30.0f;
    float m_outerConeAngle = 45.0f;
    Rapture::SpotLight3D *m_node = nullptr;
};

/**
 * @brief Placeholder section for components that don't have a full editor yet.
 */
class StubEditor : public ComponentEditorBase {
  public:
    StubEditor(const char *title, const char *icon) : m_title(title), m_icon(icon) {}
    const char *title() const override { return m_title; }
    const char *icon() const override { return m_icon; }
    float bodyHeight() const override { return 36.0f; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override { (void)entity; }

  private:
    const char *m_title;
    const char *m_icon;
};

class Mesh3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Mesh"; }
    const char *icon() const override { return Icons::SVG_MESH; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    bool m_isVisible = true;
    bool m_isRayTraced = false;
    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
    std::optional<AssetPicker> m_materialPicker;
    Rapture::Mesh3D *m_node = nullptr;
};

class Camera3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Camera"; }
    const char *icon() const override { return Icons::SVG_CAMERA; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    float m_fieldOfView = 45.0f;
    double m_nearPlane = 0.1;
    double m_farPlane = 100.0;
    Rapture::Camera3D *m_node = nullptr;
};

class ShadowEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Shadow"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    bool m_isActive = true;
    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
    Rapture::Entity m_entity;
};

class CascadedShadowEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Cascaded Shadow"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    bool m_isActive = true;
    float m_lambda = 0.5f;
    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
    Rapture::Entity m_entity;
};

class SkyboxEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Skybox"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    float m_intensity = 1.0f;
    bool m_isEnabled = true;
    Rapture::Entity m_entity;
};

class AtmosphereEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Atmosphere"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    void pushToComponent();

    Rapture::AtmosphereComponent m_component;
    double m_wavelengths[3] = {680.0, 550.0, 440.0};
    double m_sunIntensity = 20.0;
    double m_cameraAltitude = 1.0;
    Rapture::Entity m_entity;
};

#endif // RAPTURE__COMPONENT_EDITORS_H
