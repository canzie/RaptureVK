#ifndef RAPTURE__COMPONENT_EDITORS_H
#define RAPTURE__COMPONENT_EDITORS_H

#include "ComponentEditorBase.h"
#include "Icons.h"
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

class LightEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    glm::vec3 m_color = glm::vec3(1.0f);
    float m_intensity = 1.0f;
    float m_range = 10.0f;
    bool m_castsShadow = false;
    bool m_useTemperature = false;
    double m_temperature = 6500.0;

    Amethyst::Dropdown *m_typeDropdown = nullptr;
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

#endif // RAPTURE__COMPONENT_EDITORS_H
