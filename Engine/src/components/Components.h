#pragma once

/*
    Stores the state part of the ecs, mainly the data/instance of a system
*/

#include "ComponentsCommon.h"
#include "asset_manager/Asset.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "components/systems/BoundingBox.h"
#include "components/systems/Transforms.h"

#include "renderer/Frustum.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"

#include "acceleration_structures/BLAS.h"
#include "asset_manager/AssetManager.h"
#include "buffers/StorageBuffer.h"
#include "buffers/UniformBuffer.h"
#include "cameras/PerspectiveCamera.h"
#include "materials/MaterialInstance.h"
#include "meshes/Mesh.h"

#include "asset_manager/AssetManager.h"
#include "scenes/entities/Entity.h"
#include "scenes/entities/EntityCommon.h"
#include "textures/Texture.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Rapture {

struct TagComponent {
    std::string tag;
};

// need to store the data in the Transforms class because i want to support
// getting/setting each individual varaible while keeping the rest consistent
// e.g. chaning the transformmatrix will update the individual translation, rotation, scale and vice versa
struct TransformComponent {
    Transforms transforms;

    glm::vec3 translation() const { return transforms.getTranslation(); }
    glm::vec3 rotation() const { return transforms.getRotation(); }
    glm::vec3 scale() const { return transforms.getScale(); }
    glm::mat4 transformMatrix() const { return transforms.getTransform(); }

    generation_t getGeneration() const { return transforms.getGeneration(); }

  public:
    TransformComponent() = default;

    TransformComponent(glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale) : transforms(translation, rotation, scale) {}

    TransformComponent(glm::vec3 translation, glm::quat rotation, glm::vec3 scale) : transforms(translation, rotation, scale) {}

    TransformComponent(glm::mat4 transformMatrix) : transforms(transformMatrix) {}
};

// Pure camera component - only contains camera-specific data
struct CameraComponent {
    PerspectiveCamera camera;
    Frustum frustum;

    float fov;
    float aspectRatio;
    float nearPlane;
    float farPlane;

    // Optional: Camera could be marked as main camera for rendering
    bool isMainCamera = false;
    // slot into the SSBO where the camera metadata lives
    uint32_t renderDataSlot = UINT32_MAX;
    // whether the slot holds matrices from a previous update, so prevViewProj can carry them
    bool hasRenderData = false;

    CameraComponent(float fovy = 45.0f, float ar = 16.0f / 9.0f, float near_ = 0.1f, float far_ = 100.0f)
        : fov(fovy), aspectRatio(ar), nearPlane(near_), farPlane(far_)
    {
        camera = PerspectiveCamera(fovy, ar, near_, far_);
        frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
    }

    void updateProjectionMatrix(float fovy, float ar, float near_, float far_)
    {
        fov = fovy;
        aspectRatio = ar;
        nearPlane = near_;
        farPlane = far_;
        camera.updateProjectionMatrix(fovy, ar, near_, far_);
        frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
    }

    // Update view matrix from transform component
    void updateViewMatrix(const TransformComponent &transform)
    {
        glm::vec3 position = transform.translation();

        // Calculate forward direction from rotation
        glm::vec3 eulerAngles = transform.rotation();
        glm::vec3 front;
        front.x = cos(glm::radians(eulerAngles.y)) * cos(glm::radians(eulerAngles.x));
        front.y = sin(glm::radians(eulerAngles.x));
        front.z = sin(glm::radians(eulerAngles.y)) * cos(glm::radians(eulerAngles.x));
        front = glm::normalize(front);

        camera.updateViewMatrix(position, front);
        frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
    }

    // Update view matrix from transform component
    void updateViewMatrix(const TransformComponent &transform, const glm::vec3 &front)
    {
        glm::vec3 position = transform.translation();
        camera.updateViewMatrix(position, front);
        frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
    }
};

struct MaterialComponent {
    AssetPtr<MaterialInstance> material;

    MaterialComponent() = default;

    MaterialComponent(AssetRef ref) : material(std::move(ref))
    {
    }
};

struct MeshComponent {
    AssetPtr<Mesh> mesh;
    bool isLoading = true;
    Mobility mobility = MOBILITY_STATIC;
    bool isEnabled = true;
    // slot into the SSBO where the mesh metadata lives
    uint32_t renderDataSlot = UINT32_MAX;

    MeshComponent() = default;

    MeshComponent(AssetRef ref, Mobility mob = MOBILITY_STATIC) : mesh(std::move(ref)), mobility(mob)
    {
        isLoading = false;
    }
};

struct PrefabComponent {
    AssetPtr<Prefab> sourcePrefab;
    bool autoInherit = true;
    std::unordered_map<uint32_t, AssetHandle> materialOverrides; // prefab node index -> material

    PrefabComponent() = default;

    PrefabComponent(AssetRef ref) : sourcePrefab(std::move(ref))
    {
    }

  private:
    EventConnection m_structureChangedConnection;
};

struct AtmosphereComponent {
    float timeOfDay = 12.0f;
    float latitude = 0.0f;
    float longitude = 0.0f;

    glm::vec3 rayleigh = glm::vec3(5.8f, 13.5f, 33.1f);
    float mie = 21.0f;
    float sunIntensity = 20.0f;
    float mieG = 0.76f;
    float cameraAltitude = 1.0f;

    bool operator==(const AtmosphereComponent &other) const = default;
};

struct SkyboxComponent {
    AssetPtr<Texture> skyboxTexture;
    float skyIntensity = 1.0f;
    bool isEnabled = true;
    bool useAtmosphereSkybox = false;

    SkyboxComponent() = default;
    SkyboxComponent(AssetPtr<Texture> skyboxTexture, float skyIntensity = 1.0f)
        : skyboxTexture(std::move(skyboxTexture)), skyIntensity(skyIntensity) {}
    SkyboxComponent(std::filesystem::path skyboxTexturePath, float skyIntensity = 1.0f) : skyIntensity(skyIntensity)
    {
        auto asset = AssetManager::importAsset(skyboxTexturePath);
        if (asset) {
            skyboxTexture = AssetPtr<Texture>(std::move(asset));
        }
        if (!skyboxTexture) {
            RP_CORE_ERROR("Failed to load skybox texture: {}", skyboxTexturePath.string());
        }
    }
};

struct LightComponent {

    glm::vec3 color = glm::vec3(1.0f, 0.8f, 0.6f); // Light color (default: warm white?) #FFDDAA
    float intensity = 1.0f;                        // Light intensity multiplier
    bool useTemperature = false;
    float temperature = 6500.0f;

    bool isActive = true;
    Mobility mobility = MOBILITY_STATIC;
    bool castsShadow = false;

    // slot into the SSBO where the light metadata lives
    uint32_t renderDataSlot = UINT32_MAX;

    generation_t getGeneration() const { return m_generation; }

    void setColor(const glm::vec3 &c)
    {
        color = c;
        m_generation++;
    }

    static glm::vec3 kelvinToRgb(float kelvin)
    {
        float t = glm::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;

        float r;
        float g;
        float b;

        if (t <= 66.0f) {
            r = 1.0f;
        } else {
            r = glm::clamp(329.698727446f * std::pow(t - 60.0f, -0.1332047592f) / 255.0f, 0.0f, 1.0f);
        }

        if (t <= 66.0f) {
            g = glm::clamp((99.4708025861f * std::log(t) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
        } else {
            g = glm::clamp(288.1221695283f * std::pow(t - 60.0f, -0.0755148492f) / 255.0f, 0.0f, 1.0f);
        }

        if (t >= 66.0f) {
            b = 1.0f;
        } else if (t <= 19.0f) {
            b = 0.0f;
        } else {
            b = glm::clamp((138.5177312231f * std::log(t - 10.0f) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
        }

        return glm::vec3(r, g, b);
    }

    glm::vec3 getFinalColor() const
    {
        if (!useTemperature) {
            return color;
        }
        return color * kelvinToRgb(temperature);
    }

    void setIntensity(float i)
    {
        intensity = i;
        m_generation++;
    }
    void setActive(bool active)
    {
        isActive = active;
        m_generation++;
    }
    void setCastsShadow(bool casts)
    {
        castsShadow = casts;
        m_generation++;
    }

  protected:
    LightComponent() = default;
    LightComponent(const glm::vec3 &color, float intensity) : color(color), intensity(intensity) {}

  private:
    generation_t m_generation = 1;
};

struct DirectionalLightComponent : public LightComponent {
    bool atmosphereSunLight = false;

    DirectionalLightComponent() = default;
    DirectionalLightComponent(const glm::vec3 &color, float intensity) : LightComponent(color, intensity) {}
};

struct PointLightComponent : public LightComponent {
    float range = 10.0f;

    PointLightComponent() = default;
    PointLightComponent(const glm::vec3 &color, float intensity, float range) : LightComponent(color, intensity), range(range) {}
};

struct SpotLightComponent : public LightComponent {
    float range = 10.0f;
    float innerConeAngle = glm::radians(30.0f);
    float outerConeAngle = glm::radians(45.0f);

    SpotLightComponent() = default;
    SpotLightComponent(const glm::vec3 &color, float intensity, float range, float innerAngleDegrees, float outerAngleDegrees)
        : LightComponent(color, intensity), range(range), innerConeAngle(glm::radians(innerAngleDegrees)),
          outerConeAngle(glm::radians(outerAngleDegrees))
    {
    }
};

inline LightType Light_getLightType(Entity e)
{
    if (e.hasComponent<DirectionalLightComponent>()) {
        return LightType::DIRECTIONAL;
    }
    if (e.hasComponent<SpotLightComponent>()) {
        return LightType::SPOT;
    }
    return LightType::POINT;
}

inline LightComponent *Light_tryGetLight(Entity e)
{
    if (auto *d = e.tryGetComponent<DirectionalLightComponent>()) {
        return d;
    }
    if (auto *p = e.tryGetComponent<PointLightComponent>()) {
        return p;
    }
    if (auto *s = e.tryGetComponent<SpotLightComponent>()) {
        return s;
    }
    return nullptr;
}

struct BLASComponent {
    std::unique_ptr<BLAS> blas;

    BLASComponent(AssetPtr<Mesh> mesh)
    {
        blas = std::make_unique<BLAS>(std::move(mesh));
        blas->build();
    }
};

struct ShadowComponent {
    std::unique_ptr<ShadowMap> shadowMap;
    bool isActive = true;
    Mobility mobility = MOBILITY_DYNAMIC;
    uint32_t renderDataSlot = UINT32_MAX;

    ShadowComponent(float width, float height) { shadowMap = std::make_unique<ShadowMap>(width, height); }

    bool needsUpdate(const LightComponent &light, const TransformComponent &transform)
    {
        generation_t lGen = light.getGeneration();
        generation_t tGen = transform.getGeneration();
        if (lGen == m_lastLightGeneration && tGen == m_lastTransformGeneration) return false;
        m_lastLightGeneration = lGen;
        m_lastTransformGeneration = tGen;
        return true;
    }

  private:
    generation_t m_lastLightGeneration = 0;
    generation_t m_lastTransformGeneration = 0;
};

struct CascadedShadowComponent {
    std::unique_ptr<CascadedShadowMap> cascadedShadowMap;
    bool isActive = true;
    Mobility mobility = MOBILITY_DYNAMIC;
    uint32_t renderDataSlot = UINT32_MAX;

    CascadedShadowComponent(float width, float height, uint8_t numCascades, float lambda)
    {
        cascadedShadowMap = std::make_unique<CascadedShadowMap>(width, height, numCascades, lambda);
    }

    bool needsUpdate(const LightComponent &light, const TransformComponent &transform)
    {
        generation_t lGen = light.getGeneration();
        generation_t tGen = transform.getGeneration();
        if (lGen == m_lastLightGeneration && tGen == m_lastTransformGeneration) return false;
        m_lastLightGeneration = lGen;
        m_lastTransformGeneration = tGen;
        return true;
    }

  private:
    generation_t m_lastLightGeneration = 0;
    generation_t m_lastTransformGeneration = 0;
};

struct BoundingBoxComponent {
    BoundingBox localBoundingBox;
    BoundingBox worldBoundingBox;

    BoundingBoxComponent() = default;
    BoundingBoxComponent(glm::vec3 min, glm::vec3 max)
    {
        localBoundingBox = BoundingBox(min, max);
        worldBoundingBox = localBoundingBox;
    }

    void updateWorldBoundingBox(const TransformComponent &transform)
    {
        generation_t gen = transform.getGeneration();
        if (gen == m_lastTransformGeneration) return;
        m_lastTransformGeneration = gen;
        worldBoundingBox = localBoundingBox.transform(transform.transformMatrix());
    }

  private:
    generation_t m_lastTransformGeneration = 0;
};

} // namespace Rapture
