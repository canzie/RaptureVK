#ifndef RAPTURE__COMPONENT_EDITORS_H
#define RAPTURE__COMPONENT_EDITORS_H

#include "ComponentEditorBase.h"
#include "Icons.h"
#include "components/Components.h"
#include "layers/panels/components/color_field.h"

#include <glm/glm.hpp>
#include <optional>

class TransformEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Transform"; }
    const char *icon() const override { return Icons::SVG_TRANSFORM; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    void apply(int row);

    double m_values[3][3] = {};
    Rapture::Entity m_entity;
};

class DirectionalLightEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Directional Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    glm::vec3 m_color = glm::vec3(1.0f);
    float m_intensity = 1.0f;
    bool m_castsShadow = false;
    bool m_useTemperature = false;
    bool m_atmosphereSunLight = false;
    float m_temperature = 6500.0f;

    std::optional<ColorField> m_colorField;
    Rapture::Entity m_entity;
};

class PointLightEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Point Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    glm::vec3 m_color = glm::vec3(1.0f);
    float m_intensity = 1.0f;
    float m_range = 10.0f;
    bool m_castsShadow = false;
    bool m_useTemperature = false;
    float m_temperature = 6500.0f;

    std::optional<ColorField> m_colorField;
    Rapture::Entity m_entity;
};

class SpotLightEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Spot Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    glm::vec3 m_color = glm::vec3(1.0f);
    float m_intensity = 1.0f;
    float m_range = 10.0f;
    float m_innerConeAngle = 30.0f;
    float m_outerConeAngle = 45.0f;
    bool m_castsShadow = false;
    bool m_useTemperature = false;
    float m_temperature = 6500.0f;

    std::optional<ColorField> m_colorField;
    Rapture::Entity m_entity;
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

class MeshEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Mesh"; }
    const char *icon() const override { return Icons::SVG_MESH; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    bool m_isEnabled = true;
    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
    Rapture::Entity m_entity;
};

struct MaterialEditor : StubEditor {
    MaterialEditor() : StubEditor("Material", Icons::SVG_MATERIAL) {}
};

class CameraEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Camera"; }
    const char *icon() const override { return Icons::SVG_CAMERA; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    float m_fov = 45.0f;
    double m_nearPlane = 0.1;
    double m_farPlane = 100.0;
    bool m_isMainCamera = false;
    Rapture::Entity m_entity;
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
