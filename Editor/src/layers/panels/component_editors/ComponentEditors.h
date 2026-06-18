#ifndef RAPTURE__COMPONENT_EDITORS_H
#define RAPTURE__COMPONENT_EDITORS_H

#include "ComponentEditorBase.h"
#include "Icons.h"

class TransformEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Transform"; }
    const char *icon() const override { return Icons::SVG_TRANSFORM; }
    float bodyHeight() const override;
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
    float bodyHeight() const override;
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::Entity &entity) override;

  private:
    float m_intensity = 1.0f;
    float m_range = 10.0f;
    bool m_castsShadow = false;

    Amethyst::Dropdown *m_typeDropdown = nullptr;
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

struct MeshEditor : StubEditor {
    MeshEditor() : StubEditor("Mesh", Icons::SVG_MESH) {}
};
struct MaterialEditor : StubEditor {
    MaterialEditor() : StubEditor("Material", Icons::SVG_MATERIAL) {}
};
struct CameraEditor : StubEditor {
    CameraEditor() : StubEditor("Camera", Icons::SVG_CAMERA) {}
};
struct ShadowEditor : StubEditor {
    ShadowEditor() : StubEditor("Shadow", "") {}
};
struct CascadedShadowEditor : StubEditor {
    CascadedShadowEditor() : StubEditor("Cascaded Shadow", "") {}
};
struct SkyboxEditor : StubEditor {
    SkyboxEditor() : StubEditor("Skybox", "") {}
};
struct TerrainEditor : StubEditor {
    TerrainEditor() : StubEditor("Terrain", "") {}
};

#endif // RAPTURE__COMPONENT_EDITORS_H
