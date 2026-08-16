#ifndef RAPTURE__COMPONENT_EDITORS_H
#define RAPTURE__COMPONENT_EDITORS_H

#include "ComponentEditorBase.h"
#include "Icons.h"
#include "scene/components/Components.h"
#include "layers/panels/components/asset_picker.h"
#include "layers/panels/components/color_field.h"
#include "layers/panels/components/segmented_control.h"
#include "scene/instances/Camera3D.h"
#include "scene/instances/CharacterBody3D.h"
#include "scene/instances/DirectionalLight3D.h"
#include "scene/instances/Environment.h"
#include "scene/instances/Mesh3D.h"
#include "scene/instances/PointLight3D.h"
#include "scene/instances/RigidBody3D.h"
#include "scene/instances/SpotLight3D.h"
#include "scene/instances/SpringArm3D.h"
#include "scene/instances/controllers/CameraController.h"
#include "scene/instances/controllers/PlayerController.h"

#include <glm/glm.hpp>
#include <optional>

class Node3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Transform"; }
    const char *icon() const override { return Icons::SVG_TRANSFORM; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

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
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

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
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    bool m_atmosphereSun = false;
    Rapture::DirectionalLight3D *m_node = nullptr;
};

class PointLight3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Point Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    float m_range = 10.0f;
    Rapture::PointLight3D *m_node = nullptr;
};

class SpotLight3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Spot Light"; }
    const char *icon() const override { return Icons::SVG_LIGHT; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

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
    void sync(const Rapture::ecs::EntityAccessor &entity) override { (void)entity; }

  private:
    const char *m_title;
    const char *m_icon;
};

class Mesh3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Mesh"; }
    const char *icon() const override { return Icons::SVG_MESH; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    bool m_isVisible = true;
    bool m_isRayTraced = false;
    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
    std::optional<AssetPicker> m_meshPicker;
    std::optional<AssetPicker> m_materialPicker;
    Rapture::Mesh3D *m_node = nullptr;
};

/**
 * @brief The section that picks which body an object simulates with, holding that body's own section.
 */
class PhysicsEditor : public ComponentEditorBase {
  public:
    enum PhysicsBodyKind {
        PHYSICS_BODY_NONE,
        PHYSICS_BODY_RIGID,
        PHYSICS_BODY_CHARACTER,
        PHYSICS_BODY_COUNT
    };

    const char *title() const override { return "Physics"; }
    const char *icon() const override { return ""; }
    float bodyHeight() const override;
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;
    void setSubject(Rapture::Scene *scene, const Rapture::ecs::EntityAccessor &entity) override;

  private:
    /**
     * @brief Replaces the nested section with the one editing the given kind of body
     * @param kind The kind of body the owner now has
     */
    void buildPhysicsSubEditor(PhysicsBodyKind kind);

    SegmentedControl *m_physicsSubSelector = nullptr;
    std::unique_ptr<ComponentEditorBase> m_physicsSubEditor;
    Amethyst::CollapsibleHeader *m_physicsSubHeader = nullptr;
    PhysicsBodyKind m_builtKind = PHYSICS_BODY_NONE;
    Rapture::SceneObject *m_owner = nullptr;
};

class RigidBody3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Rigid Body"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    /**
     * @brief The body of the selected object, whether it stands on its own or a mesh holds it
     * @param entity The selected entity
     * @return The body, or nullptr if the selection has none
     */
    Rapture::RigidBody3D *resolveBody(const Rapture::ecs::EntityAccessor &entity) const;

    float m_friction = 0.2f;
    float m_restitution = 0.0f;
    Amethyst::Dropdown *m_motionTypeDropdown = nullptr;
    Amethyst::Dropdown *m_shapeDropdown = nullptr;
    Rapture::RigidBody3D *m_rigidBody = nullptr;
};

class CharacterBody3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Character Body"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    /**
     * @brief The body of the selected object, whether it stands on its own or a mesh holds it
     * @param entity The selected entity
     * @return The body, or nullptr if the selection has none
     */
    Rapture::CharacterBody3D *resolveBody(const Rapture::ecs::EntityAccessor &entity) const;

    double m_shapeOffset[3] = {};
    float m_mass = 70.0f;
    float m_maxSlopeAngle = 50.0f;
    float m_stepUp = 0.4f;
    float m_stepDown = 0.5f;
    float m_jumpSpeed = 4.0f;
    Amethyst::Dropdown *m_shapeDropdown = nullptr;
    Rapture::CharacterBody3D *m_characterBody = nullptr;
};

class SpringArm3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Spring Arm"; }
    const char *icon() const override { return Icons::SVG_LINK; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    double m_length = 4.0;
    Rapture::SpringArm3D *m_node = nullptr;
};

class Camera3DEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Camera"; }
    const char *icon() const override { return Icons::SVG_CAMERA; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

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
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    bool m_isActive = true;
    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
};

class CascadedShadowEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Cascaded Shadow"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    bool m_isActive = true;
    float m_lambda = 0.5f;
    Amethyst::Dropdown *m_mobilityDropdown = nullptr;
};

class EnvironmentEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Environment"; }
    const char *icon() const override { return ""; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    void pushAtmosphere();

    float m_skyIntensity = 1.0f;
    bool m_skyboxEnabled = true;
    bool m_usesAtmosphereSkybox = false;

    Rapture::AtmosphereSettings m_atmosphere;
    double m_wavelengths[3] = {680.0, 550.0, 440.0};
    double m_sunIntensity = 20.0;
    double m_cameraAltitude = 1.0;

    Rapture::Environment *m_node = nullptr;
};

class CameraControllerEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Camera Controller"; }
    const char *icon() const override { return Icons::SVG_CAMERA; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    float m_mouseSensitivity = 0.1f;
    float m_movementSpeed = 5.0f;
    float m_orbitSensitivity = 0.3f;
    float m_panSpeed = 0.0015f;
    float m_zoomSpeed = 0.15f;
    float m_maxPitch = 89.0f;

    Rapture::CameraController *m_controller = nullptr;
};

class PlayerControllerEditor : public ComponentEditorBase {
  public:
    const char *title() const override { return "Player Controller"; }
    const char *icon() const override { return Icons::SVG_CONTROLLER; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(const Rapture::ecs::EntityAccessor &entity) override;

  private:
    float m_movementSpeed = 5.0f;
    float m_mouseSensitivity = 0.1f;
    float m_maxPitch = 89.0f;

    Rapture::PlayerController *m_controller = nullptr;
};

#endif // RAPTURE__COMPONENT_EDITORS_H
