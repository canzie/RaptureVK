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
#include "components/systems/CameraController.h"
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
#include "physics/EntropyComponents.h"
#include "physics/colliders/ColliderPrimitives.h"

#include "asset_manager/AssetManager.h"
#include "scenes/entities/EntityCommon.h"
#include "textures/Texture.h"

#include <memory>
#include <string>

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

// Controller component - now only holds data, logic is in CameraController class
struct CameraControllerComponent {
    // Use the controller class for logic
    CameraController controller;

    CameraControllerComponent() : controller() {}

    // Simple wrapper that delegates to the controller update
    void update(float deltaTime, TransformComponent &transform, CameraComponent &camera)
    {
        controller.update(deltaTime, transform, camera);
    }
};

struct MaterialComponent {
    MaterialInstance *material = nullptr;

    MaterialComponent() = default;

    MaterialComponent(AssetRef ref)
    {
        asset = ref;
        material = asset ? asset.get()->getUnderlyingAsset<MaterialInstance>() : nullptr;
    }

  private:
    AssetRef asset;
};

struct MeshComponent {
    Mesh *mesh = nullptr;
    bool isLoading = true;
    Mobility mobility = MOBILITY_STATIC;
    bool isEnabled = true;
    // slot into the SSBO where the mesh metadata lives
    uint32_t renderDataSlot = UINT32_MAX;

    MeshComponent() = default;

    MeshComponent(AssetRef ref)
    {
        asset = ref;
        mesh = asset ? asset.get()->getUnderlyingAsset<Mesh>() : nullptr;
        isLoading = false;
    }

  private:
    AssetRef asset;
};

struct InstanceComponent {
    std::vector<MaterialComponent> materials;
    std::vector<TransformComponent> transforms;
    std::vector<uint32_t> instanceIDs;
    uint32_t instanceIDCount = 0; // need this seperate variable to avoid issues when instances are added/removed

    InstanceComponent(std::vector<MaterialComponent> materials, std::vector<TransformComponent> transforms)
        : materials(materials), transforms(transforms)
    {
        for (uint32_t i = 0; i < materials.size(); i++) {
            instanceIDs.push_back(instanceIDCount++);
        }
    }

    InstanceComponent(MaterialComponent material, TransformComponent transform)
    {
        materials.push_back(material);
        transforms.push_back(transform);
        instanceIDs.push_back(instanceIDCount++);
    }

    void addInstance(MaterialComponent material, TransformComponent transform)
    {
        materials.push_back(material);
        transforms.push_back(transform);
        instanceIDs.push_back(instanceIDCount++);
    }
};

// enables efficient instancing of 1000s of instances
// a more limited version of instancing as materials and other data is static
// if you want more complex objects, boohoo
struct InstanceShapeComponent {
    // ssbo containing instance data and other instancing details like wiremode, ...
    std::shared_ptr<StorageBuffer> instanceSSBO;
    glm::vec4 color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    bool useWireMode = true;
    uint32_t instanceCount = 0;

    InstanceShapeComponent(std::vector<InstanceData> instanceData, VmaAllocator allocator)
    {

        if (!instanceData.empty()) {
            instanceSSBO =
                std::make_shared<StorageBuffer>(sizeof(InstanceData) * instanceData.size(), BufferUsage::DYNAMIC, allocator);
            instanceSSBO->addDataGPU(instanceData.data(), instanceData.size() * sizeof(InstanceData), 0);
            instanceCount = static_cast<uint32_t>(instanceData.size());
        }
    }
};

struct SkyboxComponent {
    Texture *skyboxTexture;
    float skyIntensity = 1.0f;
    bool isEnabled = true;

    SkyboxComponent() = default;
    SkyboxComponent(std::filesystem::path skyboxTexturePath, float skyIntensity = 1.0f) : skyIntensity(skyIntensity)
    {
        asset = AssetManager::importAsset(skyboxTexturePath);
        skyboxTexture = asset ? asset.get()->getUnderlyingAsset<Texture>() : nullptr;
        if (!skyboxTexture) {
            RP_CORE_ERROR("Failed to load skybox texture: {}", skyboxTexturePath.string());
        }
    }

  private:
    AssetRef asset;
};

struct LightComponent {

    LightType type = LightType::Point;
    glm::vec3 color = glm::vec3(1.0f, 0.8f, 0.6f); // Light color (default: warm white?) #FFDDAA
    float intensity = 1.0f;                        // Light intensity multiplier

    // For point and spot lights
    float range = 10.0f; // Attenuation range

    // For spot lights only
    float innerConeAngle = glm::radians(30.0f); // Inner cone angle in radians
    float outerConeAngle = glm::radians(45.0f); // Outer cone angle in radians

    bool isActive = true;
    Mobility mobility = MOBILITY_STATIC;
    bool castsShadow = false;
    // slot into the SSBO where the light metadata lives
    uint32_t renderDataSlot = UINT32_MAX;

    generation_t getGeneration() const { return m_generation; }

    LightComponent() = default;

    LightComponent(const glm::vec3 &color, float intensity, float range)
        : type(LightType::Point), color(color), intensity(intensity), range(range)
    {
    }

    LightComponent(const glm::vec3 &color, float intensity) : type(LightType::Directional), color(color), intensity(intensity) {}

    LightComponent(const glm::vec3 &color, float intensity, float range, float innerAngleDegrees, float outerAngleDegrees)
        : type(LightType::Spot), color(color), intensity(intensity), range(range), innerConeAngle(glm::radians(innerAngleDegrees)),
          outerConeAngle(glm::radians(outerAngleDegrees))
    {
    }

    void setColor(const glm::vec3 &c)
    {
        color = c;
        m_generation++;
    }
    void setIntensity(float i)
    {
        intensity = i;
        m_generation++;
    }
    void setRange(float r)
    {
        range = r;
        m_generation++;
    }
    void setType(LightType t)
    {
        type = t;
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
    void setInnerConeAngle(float angle)
    {
        innerConeAngle = angle;
        m_generation++;
    }
    void setOuterConeAngle(float angle)
    {
        outerConeAngle = angle;
        m_generation++;
    }

  private:
    generation_t m_generation = 1;
};

struct BLASComponent {
    std::unique_ptr<BLAS> blas;

    BLASComponent(Mesh *mesh)
    {
        blas = std::make_unique<BLAS>(mesh);
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
