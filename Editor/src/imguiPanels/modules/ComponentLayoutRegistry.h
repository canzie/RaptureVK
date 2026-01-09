#ifndef RAPTURE__COMPONENT_LAYOUT_REGISTRY_H
#define RAPTURE__COMPONENT_LAYOUT_REGISTRY_H

#include "ComponentLayoutSystem.h"
#include "Components/Components.h"
#include "Components/FogComponent.h"
#include "Components/IndirectLightingComponent.h"
#include "Components/TerrainComponent.h"
#include "imguiPanels/modules/ScratchBuffer.h"
#include <glm/gtc/type_ptr.hpp>

namespace ComponentUI {

// Static options storage (these need to persist)
namespace LayoutOptions {
static FloatOptions intensityOpts = {0.01f, 0.0f, 100.0f, "%.2f"};
static FloatOptions rangeOpts = {0.1f, 0.0f, 1000.0f, "%.1f"};
static FloatOptions angleOpts = {0.1f, 0.0f, 89.0f, "%.1f"};
static FloatOptions fovOpts = {0.1f, 1.0f, 179.0f, "%.1f"};
static FloatOptions aspectRatioOpts = {0.01f, 0.1f, 10.0f, "%.2f"};
static FloatOptions nearPlaneOpts = {0.01f, 0.01f, 0.0f, "%.2f"};
static FloatOptions farPlaneOpts = {0.1f, 0.01f, 10000.0f, "%.1f"};
static FloatOptions fogDistanceOpts = {0.1f, 0.0f, 1000.0f, "%.2f"};
static FloatOptions fogDensityOpts = {0.001f, 0.0f, 1.0f, "%.3f"};
static FloatOptions giIntensityOpts = {0.01f, 0.0f, 10.0f, "%.2f"};
static FloatOptions skyIntensityOpts = {0.01f, 0.0f, 1.0f, "%.2f"};
static FloatOptions lambdaOpts = {0.01f, 0.0f, 1.0f, "%.3f"};

static const char *lightTypeNames[] = {"Point", "Directional", "Spot"};
static EnumOptions lightTypeEnum = {lightTypeNames, 3};

static const char *fogTypeNames[] = {"Linear", "Exponential", "ExponentialSquared"};
static EnumOptions fogTypeEnum = {fogTypeNames, 3};
} // namespace LayoutOptions

// ============ Transform Component ============
inline ComponentLayout<Rapture::TransformComponent> createTransformLayout()
{
    ComponentLayout<Rapture::TransformComponent> layout;
    layout.componentName = "Transform Component";

    layout.elements = {
        LayoutElement<Rapture::TransformComponent>::Separator(SeparatorDescriptor::Dummy(10.0f)),

        // TODO: TransformComponent needs to expose position/rotation/scale as direct members
        // or provide mutable references via getters. Currently getTranslation() returns by value.
        // Temporary: Keep using manual rendering for TransformComponent

        LayoutElement<Rapture::TransformComponent>::Separator(SeparatorDescriptor::Dummy(20.0f)),
    };

    return layout;
}

// ============ Light Component ============
inline ComponentLayout<Rapture::LightComponent> createLightLayout()
{
    ComponentLayout<Rapture::LightComponent> layout;
    layout.componentName = "Light Component";

    layout.elements = {
        LayoutElement<Rapture::LightComponent>::Field(
            {"Type", FieldType::ENUM, WidgetType::COMBO,
             [](Rapture::LightComponent &comp, ScratchBuffer &) -> void * { return reinterpret_cast<void *>(&comp.type); }, NONE,
             &LayoutOptions::lightTypeEnum}),

        LayoutElement<Rapture::LightComponent>::Field(
            {"Color", FieldType::COLOR3, WidgetType::COLOR_EDIT,
             [](Rapture::LightComponent &comp, ScratchBuffer &) -> void * { return &comp.color; }}),

        LayoutElement<Rapture::LightComponent>::Field(
            {"Intensity", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::LightComponent &comp, ScratchBuffer &) -> void * { return &comp.intensity; }, NONE,
             &LayoutOptions::intensityOpts}),

        LayoutElement<Rapture::LightComponent>::Field({"Range", FieldType::FLOAT, WidgetType::DRAG,
                                                       [](Rapture::LightComponent &comp, ScratchBuffer &) -> void * {
                                                           if (comp.type == Rapture::LightType::Point ||
                                                               comp.type == Rapture::LightType::Spot) {
                                                               return &comp.range;
                                                           }
                                                           return nullptr;
                                                       },
                                                       NONE, &LayoutOptions::rangeOpts}),

        LayoutElement<Rapture::LightComponent>::Field({"Inner Cone", FieldType::FLOAT, WidgetType::DRAG,
                                                       [](Rapture::LightComponent &comp, ScratchBuffer &scratch) -> void * {
                                                           if (comp.type == Rapture::LightType::Spot) {
                                                               float *degrees = static_cast<float *>(scratch.allocate(sizeof(float)));
                                                               *degrees = glm::degrees(comp.innerConeAngle);
                                                               return degrees;
                                                           }
                                                           return nullptr;
                                                       },
                                                       NONE, &LayoutOptions::angleOpts,
                                                       [](void *valuePtr, Rapture::LightComponent &comp) {
                                                           float degrees = *static_cast<float *>(valuePtr);
                                                           comp.innerConeAngle = glm::radians(degrees);
                                                       }}),

        LayoutElement<Rapture::LightComponent>::Field({"Outer Cone", FieldType::FLOAT, WidgetType::DRAG,
                                                       [](Rapture::LightComponent &comp, ScratchBuffer &scratch) -> void * {
                                                           if (comp.type == Rapture::LightType::Spot) {
                                                               float *degrees = static_cast<float *>(scratch.allocate(sizeof(float)));
                                                               *degrees = glm::degrees(comp.outerConeAngle);
                                                               return degrees;
                                                           }
                                                           return nullptr;
                                                       },
                                                       NONE, &LayoutOptions::angleOpts,
                                                       [](void *valuePtr, Rapture::LightComponent &comp) {
                                                           float degrees = *static_cast<float *>(valuePtr);
                                                           comp.outerConeAngle = glm::radians(degrees);
                                                       }}),

        LayoutElement<Rapture::LightComponent>::Field(
            {"Is Active", FieldType::BOOL, WidgetType::CHECKBOX,
             [](Rapture::LightComponent &comp, ScratchBuffer &) -> void * { return &comp.isActive; }}),

        LayoutElement<Rapture::LightComponent>::Field(
            {"Casts Shadow", FieldType::BOOL, WidgetType::CHECKBOX,
             [](Rapture::LightComponent &comp, ScratchBuffer &) -> void * { return &comp.castsShadow; }}),
    };

    return layout;
}

// ============ Camera Component ============
inline ComponentLayout<Rapture::CameraComponent> createCameraLayout()
{
    ComponentLayout<Rapture::CameraComponent> layout;
    layout.componentName = "Camera Component";

    layout.elements = {
        LayoutElement<Rapture::CameraComponent>::Field(
            {"FOV", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::CameraComponent &comp, ScratchBuffer &) -> void * { return &comp.fov; }, NONE, &LayoutOptions::fovOpts}),

        LayoutElement<Rapture::CameraComponent>::Field(
            {"Aspect Ratio", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::CameraComponent &comp, ScratchBuffer &) -> void * { return &comp.aspectRatio; }, NONE,
             &LayoutOptions::aspectRatioOpts}),

        LayoutElement<Rapture::CameraComponent>::Field(
            {"Near Plane", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::CameraComponent &comp, ScratchBuffer &) -> void * { return &comp.nearPlane; }, NONE,
             &LayoutOptions::nearPlaneOpts}),

        LayoutElement<Rapture::CameraComponent>::Field(
            {"Far Plane", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::CameraComponent &comp, ScratchBuffer &) -> void * { return &comp.farPlane; }, NONE,
             &LayoutOptions::farPlaneOpts}),
    };

    return layout;
}

// ============ Cascaded Shadow Component ============
inline ComponentLayout<Rapture::CascadedShadowComponent> createCascadedShadowLayout()
{
    ComponentLayout<Rapture::CascadedShadowComponent> layout;
    layout.componentName = "Cascaded Shadow Component";

    layout.elements = {
        LayoutElement<Rapture::CascadedShadowComponent>::Field(
            {"Lambda", FieldType::FLOAT, WidgetType::SLIDER,
             [](Rapture::CascadedShadowComponent &comp, ScratchBuffer &scratch) -> void * {
                 if (comp.cascadedShadowMap) {
                     float *lambda = static_cast<float *>(scratch.allocate(sizeof(float)));
                     *lambda = comp.cascadedShadowMap->getLambda();
                     return lambda;
                 }
                 return nullptr;
             },
             NONE, &LayoutOptions::lambdaOpts,
             [](void *valuePtr, Rapture::CascadedShadowComponent &comp) {
                 if (comp.cascadedShadowMap) {
                     float lambda = *static_cast<float *>(valuePtr);
                     comp.cascadedShadowMap->setLambda(lambda);
                 }
             }}),

        LayoutElement<Rapture::CascadedShadowComponent>::Separator(
            SeparatorDescriptor::Text("Cascade split distribution: 0.0 = linear, 1.0 = logarithmic")),

        LayoutElement<Rapture::CascadedShadowComponent>::Separator(SeparatorDescriptor::Line()),

        LayoutElement<Rapture::CascadedShadowComponent>::Field(
            {"Num Cascades", FieldType::INT, WidgetType::INPUT,
             [](Rapture::CascadedShadowComponent &comp, ScratchBuffer &scratch) -> void * {
                 if (comp.cascadedShadowMap) {
                     int *numCascades = static_cast<int *>(scratch.allocate(sizeof(int)));
                     *numCascades = comp.cascadedShadowMap->getNumCascades();
                     return numCascades;
                 }
                 return nullptr;
             },
             LOCKED}),

        LayoutElement<Rapture::CascadedShadowComponent>::Field(
            {"Texture Handle", FieldType::INT, WidgetType::INPUT,
             [](Rapture::CascadedShadowComponent &comp, ScratchBuffer &scratch) -> void * {
                 if (comp.cascadedShadowMap) {
                     int *handle = static_cast<int *>(scratch.allocate(sizeof(int)));
                     *handle = comp.cascadedShadowMap->getTextureHandle();
                     return handle;
                 }
                 return nullptr;
             },
             LOCKED}),
    };

    return layout;
}

// ============ Mesh Component ============
inline ComponentLayout<Rapture::MeshComponent> createMeshLayout()
{
    ComponentLayout<Rapture::MeshComponent> layout;
    layout.componentName = "Mesh Component";

    layout.elements = {
        // TODO: "Instanced" is derived from checking for InstanceComponent on entity.
        // Need access to entity context in accessor lambdas.
    };

    return layout;
}

// ============ Fog Component ============
inline ComponentLayout<Rapture::FogComponent> createFogLayout()
{
    ComponentLayout<Rapture::FogComponent> layout;
    layout.componentName = "Fog Component";

    layout.elements = {
        LayoutElement<Rapture::FogComponent>::Field(
            {"Enabled", FieldType::BOOL, WidgetType::CHECKBOX,
             [](Rapture::FogComponent &comp, ScratchBuffer &) -> void * { return &comp.enabled; }}),

        LayoutElement<Rapture::FogComponent>::Field(
            {"Fog Color", FieldType::COLOR3, WidgetType::COLOR_EDIT,
             [](Rapture::FogComponent &comp, ScratchBuffer &) -> void * { return &comp.color; }}),

        LayoutElement<Rapture::FogComponent>::Field(
            {"Start Distance", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::FogComponent &comp, ScratchBuffer &) -> void * { return &comp.start; }, NONE,
             &LayoutOptions::fogDistanceOpts}),

        LayoutElement<Rapture::FogComponent>::Field(
            {"End Distance", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::FogComponent &comp, ScratchBuffer &) -> void * { return &comp.end; }, NONE,
             &LayoutOptions::fogDistanceOpts}),

        LayoutElement<Rapture::FogComponent>::Field(
            {"Density", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::FogComponent &comp, ScratchBuffer &) -> void * { return &comp.density; }, NONE,
             &LayoutOptions::fogDensityOpts}),

        LayoutElement<Rapture::FogComponent>::Field(
            {"Fog Type", FieldType::ENUM, WidgetType::COMBO,
             [](Rapture::FogComponent &comp, ScratchBuffer &) -> void * { return reinterpret_cast<void *>(&comp.type); }, NONE,
             &LayoutOptions::fogTypeEnum}),
    };

    return layout;
}

// ============ Indirect Lighting Component ============
inline ComponentLayout<Rapture::IndirectLightingComponent> createIndirectLightingLayout()
{
    ComponentLayout<Rapture::IndirectLightingComponent> layout;
    layout.componentName = "Indirect Lighting Component";

    layout.elements = {
        LayoutElement<Rapture::IndirectLightingComponent>::Field(
            {"Enabled", FieldType::BOOL, WidgetType::CHECKBOX,
             [](Rapture::IndirectLightingComponent &comp, ScratchBuffer &) -> void * { return &comp.enabled; }}),

        LayoutElement<Rapture::IndirectLightingComponent>::Field(
            {"GI Intensity", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::IndirectLightingComponent &comp, ScratchBuffer &) -> void * { return &comp.giIntensity; }, NONE,
             &LayoutOptions::giIntensityOpts}),

        LayoutElement<Rapture::IndirectLightingComponent>::Separator(SeparatorDescriptor::Line()),
        LayoutElement<Rapture::IndirectLightingComponent>::Separator(SeparatorDescriptor::Text("Technique")),
    };

    return layout;
}

// ============ Skybox Component ============
inline ComponentLayout<Rapture::SkyboxComponent> createSkyboxLayout()
{
    ComponentLayout<Rapture::SkyboxComponent> layout;
    layout.componentName = "Skybox Component";

    layout.elements = {
        LayoutElement<Rapture::SkyboxComponent>::Field(
            {"Enabled", FieldType::BOOL, WidgetType::CHECKBOX,
             [](Rapture::SkyboxComponent &comp, ScratchBuffer &) -> void * { return &comp.isEnabled; }}),

        LayoutElement<Rapture::SkyboxComponent>::Field(
            {"Skybox Intensity", FieldType::FLOAT, WidgetType::DRAG,
             [](Rapture::SkyboxComponent &comp, ScratchBuffer &) -> void * { return &comp.skyIntensity; }, NONE,
             &LayoutOptions::skyIntensityOpts}),
    };

    return layout;
}

// ============ Registry ============
class ComponentLayoutRegistry {
  public:
    static ComponentLayoutRegistry &getInstance()
    {
        static ComponentLayoutRegistry instance;
        return instance;
    }

    const ComponentLayout<Rapture::TransformComponent> &getTransformLayout() const { return m_transformLayout; }
    const ComponentLayout<Rapture::LightComponent> &getLightLayout() const { return m_lightLayout; }
    const ComponentLayout<Rapture::CameraComponent> &getCameraLayout() const { return m_cameraLayout; }
    const ComponentLayout<Rapture::CascadedShadowComponent> &getCascadedShadowLayout() const { return m_cascadedShadowLayout; }
    const ComponentLayout<Rapture::MeshComponent> &getMeshLayout() const { return m_meshLayout; }
    const ComponentLayout<Rapture::FogComponent> &getFogLayout() const { return m_fogLayout; }
    const ComponentLayout<Rapture::IndirectLightingComponent> &getIndirectLightingLayout() const
    {
        return m_indirectLightingLayout;
    }
    const ComponentLayout<Rapture::SkyboxComponent> &getSkyboxLayout() const { return m_skyboxLayout; }

  private:
    ComponentLayoutRegistry()
    {
        m_transformLayout = createTransformLayout();
        m_lightLayout = createLightLayout();
        m_cameraLayout = createCameraLayout();
        m_cascadedShadowLayout = createCascadedShadowLayout();
        m_meshLayout = createMeshLayout();
        m_fogLayout = createFogLayout();
        m_indirectLightingLayout = createIndirectLightingLayout();
        m_skyboxLayout = createSkyboxLayout();
    }

    ComponentLayout<Rapture::TransformComponent> m_transformLayout;
    ComponentLayout<Rapture::LightComponent> m_lightLayout;
    ComponentLayout<Rapture::CameraComponent> m_cameraLayout;
    ComponentLayout<Rapture::CascadedShadowComponent> m_cascadedShadowLayout;
    ComponentLayout<Rapture::MeshComponent> m_meshLayout;
    ComponentLayout<Rapture::FogComponent> m_fogLayout;
    ComponentLayout<Rapture::IndirectLightingComponent> m_indirectLightingLayout;
    ComponentLayout<Rapture::SkyboxComponent> m_skyboxLayout;
};

} // namespace ComponentUI

#endif // RAPTURE__COMPONENT_LAYOUT_REGISTRY_H
