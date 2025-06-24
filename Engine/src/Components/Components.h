#pragma once


/*
    Stores the state part of the ecs, mainly the data/instance of a system
*/


#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Components/Systems/Transforms.h"
#include "Components/Systems/CameraController.h"
#include "Components/Systems/EntityNode.h"
#include "Components/Systems/BoundingBox.h"
#include "Components/Systems/ObjectDataBuffer.h"

#include "Renderer/Frustum/Frustum.h"
#include "Renderer/Shadows/ShadowMapping/ShadowMapping.h"
#include "Renderer/Shadows/CascadedShadowMapping/CascadedShadowMapping.h"

#include "Cameras/PerspectiveCamera/PerspectiveCamera.h"
#include "AccelerationStructures/BLAS.h"
#include "Meshes/Mesh.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Materials/MaterialInstance.h"
#include "AssetManager/AssetManager.h"

#include "Physics/Colliders/ColliderPrimitives.h"

#include <string>
#include <memory>


    // Maximum number of lights supported
    static constexpr uint32_t MAX_LIGHTS = 16;

namespace Rapture {


    struct TagComponent {
        std::string tag;
    };

    // dummy component to identify the root entity
    struct RootComponent {
        bool isRoot = true;
    };


    // need to store the data in the Transforms class because i want to support
    // getting/setting each individual varaible while keeping the rest consistent
    // e.g. chaning the transformmatrix will update the individual translation, rotation, scale and vice versa
	struct TransformComponent
	{
        Transforms transforms;
        
        glm::vec3 translation() const { return transforms.getTranslation(); }
        glm::vec3 rotation() const { return transforms.getRotation(); }
        glm::vec3 scale() const { return transforms.getScale(); }
        glm::mat4 transformMatrix() const { return transforms.getTransform(); }

        private:
            mutable std::size_t m_lastHash = 0;
            mutable uint32_t m_lastFrame = 10; // random number larger than framesinglight
            mutable bool changedThisFrame = false;

        public:
        TransformComponent()=default;

        TransformComponent(glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale) {
            transforms = Transforms(translation, rotation, scale);
        }

        // Add constructor for quaternion rotation
        TransformComponent(glm::vec3 translation, glm::quat rotation, glm::vec3 scale) {
            transforms = Transforms(translation, rotation, scale);
        }

        TransformComponent(glm::mat4 transformMatrix) {
            transforms.setTransform(transformMatrix);
        }

        std::size_t calculateCurrentHash() const
        {
            const glm::mat4& matrix = transforms.getTransform();
            std::size_t hash = 0;
            
            // Hash the 16 float values in the transform matrix
            const float* values = glm::value_ptr(matrix);
            for (int i = 0; i < 16; ++i) {
                // Simple hash combination using bit operations
                // Multiply by prime number and XOR with current hash
                hash ^= std::hash<float>{}(values[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            
            return hash;
        }

        bool hasChanged(uint32_t currentFrame) const
        {
            if (m_lastFrame != currentFrame) {
                m_lastFrame = currentFrame;
                changedThisFrame = false;

                size_t currentHash = calculateCurrentHash();
                if (m_lastHash != currentHash) {
                    m_lastHash = currentHash;
                    changedThisFrame = true;
                    return true;
                }
            }
            return changedThisFrame;
        }
	};

    // Pure camera component - only contains camera-specific data
    struct CameraComponent
    {
        PerspectiveCamera camera;
        Frustum frustum;
        
        float fov;
        float aspectRatio;
        float nearPlane;
        float farPlane;
        
        // Optional: Camera could be marked as main camera for rendering
        bool isMainCamera = false;
        
        std::shared_ptr<CameraDataBuffer> cameraDataBuffer;

        CameraComponent(float fovy = 45.0f, float ar = 16.0f/9.0f, float near_ = 0.1f, float far_ = 100.0f)
            : fov(fovy), aspectRatio(ar), nearPlane(near_), farPlane(far_)
        {
            camera = PerspectiveCamera(fovy, ar, near_, far_);
            frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
            cameraDataBuffer = std::make_shared<CameraDataBuffer>();
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
        void updateViewMatrix(const TransformComponent& transform)
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
        void updateViewMatrix(const TransformComponent& transform, const glm::vec3& front)
        {
            glm::vec3 position = transform.translation();
            camera.updateViewMatrix(position, front);
            frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
        }
    };

    // Controller component - now only holds data, logic is in CameraController class
    struct CameraControllerComponent
    {
        // Use the controller class for logic
        CameraController controller;
        
        CameraControllerComponent()
            : controller() {}

        // Simple wrapper that delegates to the controller update
        void update(float deltaTime, TransformComponent& transform, CameraComponent& camera)
        {
            controller.update(deltaTime, transform, camera);

        }
    };

    struct MaterialComponent {
        std::shared_ptr<MaterialInstance> material;

        MaterialComponent(std::shared_ptr<BaseMaterial> baseMaterial, std::string name = "") {
            material = std::make_shared<MaterialInstance>(baseMaterial, name);
        };

        MaterialComponent(std::shared_ptr<MaterialInstance> material) {
            this->material = material;
        };
    };

    struct MeshComponent {
        std::shared_ptr<Mesh> mesh;
        bool isLoading = true;

        std::shared_ptr<MeshDataBuffer> meshDataBuffer;


        MeshComponent() {
            mesh = std::make_shared<Mesh>();
            meshDataBuffer = std::make_shared<MeshDataBuffer>();
        };

        MeshComponent(std::shared_ptr<Mesh> mesh) {
            this->mesh = mesh;
            isLoading = false;
            meshDataBuffer = std::make_shared<MeshDataBuffer>();
        };
    };

    struct InstanceComponent {
        std::vector<MaterialComponent> materials;
        std::vector<TransformComponent> transforms;
        std::vector<uint32_t> instanceIDs;
        uint32_t instanceIDCount = 0; // need this seperate variable to avoid issues when instances are added/removed

        InstanceComponent(std::vector<MaterialComponent> materials, std::vector<TransformComponent> transforms) : materials(materials), transforms(transforms) {
            for (uint32_t i = 0; i < materials.size(); i++) {
                instanceIDs.push_back(instanceIDCount++);
            }
        }

        InstanceComponent(MaterialComponent material, TransformComponent transform) {
            materials.push_back(material);
            transforms.push_back(transform);
            instanceIDs.push_back(instanceIDCount++);
        }

        void addInstance(MaterialComponent material, TransformComponent transform) {
            materials.push_back(material);
            transforms.push_back(transform);
            instanceIDs.push_back(instanceIDCount++);
        }


    };

    struct EntityNodeComponent
    {
        std::shared_ptr<EntityNode> entity_node;

        EntityNodeComponent() = default;

        EntityNodeComponent(std::shared_ptr<Entity> entity)
        {
            entity_node = std::make_shared<EntityNode>(entity);
        }

        EntityNodeComponent(std::shared_ptr<Entity> entity, std::shared_ptr<EntityNode> parent)
        {
            entity_node = std::make_shared<EntityNode>(entity, parent);
        }
        
        ~EntityNodeComponent()
        {
        }
        
        
    };

struct SkyboxComponent {
    std::shared_ptr<Texture> skyboxTexture;
    AssetHandle skyboxTextureHandle;

    SkyboxComponent() = default;
    SkyboxComponent(std::shared_ptr<Texture> skyboxTexture) : skyboxTexture(skyboxTexture) {}
    SkyboxComponent(std::filesystem::path skyboxTexturePath) {
        auto [skyboxTexture, handle] = AssetManager::importAsset<Texture>(skyboxTexturePath);
        if (!skyboxTexture) {
            RP_CORE_ERROR("SkyboxComponent - Failed to load skybox texture: {}", skyboxTexturePath.string());
            this->skyboxTextureHandle = AssetHandle();
        } else {
            this->skyboxTexture = skyboxTexture;
            this->skyboxTextureHandle = handle;
        }
    }
};


// Light types for the LightComponent
enum class LightType
{
    Point = 0,
    Directional = 1,
    Spot = 2
};

struct LightComponent
{

    LightType type = LightType::Point;
    glm::vec3 color = glm::vec3(1.0f, 0.8f, 0.6f);    // Light color (default: warm white?) #FFDDAA
    float intensity = 1.0f;               // Light intensity multiplier
    
    // For point and spot lights
    float range = 10.0f;                  // Attenuation range
    
    // For spot lights only
    float innerConeAngle = glm::radians(30.0f); // Inner cone angle in radians
    float outerConeAngle = glm::radians(45.0f); // Outer cone angle in radians
    
    // Flag indicating if the light is active
    bool isActive = true;
    bool castsShadow = false;

    std::shared_ptr<LightDataBuffer> lightDataBuffer;
    
    private:
        mutable uint32_t m_lastHash = 0;
        mutable uint32_t m_lastFrame = 10; // random number larger than framesinglight
        mutable bool changedThisFrame = false;

    public:
    // Constructors
    LightComponent() = default;
    
    // Constructor for point light
    LightComponent(const glm::vec3& color, float intensity, float range)
        : type(LightType::Point), color(color), intensity(intensity), range(range) {}
    
    // Constructor for directional light
    LightComponent(const glm::vec3& color, float intensity)
        : type(LightType::Directional), color(color), intensity(intensity) {}
    
    // Constructor for spot light
    LightComponent(const glm::vec3& color, float intensity, float range, 
                    float innerAngleDegrees, float outerAngleDegrees)
        : type(LightType::Spot), color(color), intensity(intensity), range(range),
            innerConeAngle(glm::radians(innerAngleDegrees)), 
            outerConeAngle(glm::radians(outerAngleDegrees)) {}



    std::uint32_t calculateCurrentHash() const {
        std::uint32_t hash = 0;
        
        // Common properties for all light types
        hash ^= std::hash<LightType>{}(type) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<bool>{}(isActive) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<bool>{}(castsShadow) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(intensity) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        
        // Hash color components
        hash ^= std::hash<float>{}(color.r) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(color.g) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(color.b) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        
        // Properties specific to light types
        switch (type) {
            case LightType::Point:
                // Point lights use range
                hash ^= std::hash<float>{}(range) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                break;
                
            case LightType::Directional:
                // Directional lights don't need additional properties
                break;
                
            case LightType::Spot:
                // Spot lights use range and cone angles
                hash ^= std::hash<float>{}(range) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= std::hash<float>{}(innerConeAngle) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= std::hash<float>{}(outerConeAngle) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                break;
        }
        
        return hash;
    }

    bool hasChanged(uint32_t currentFrame) const {
        if (m_lastFrame != currentFrame) {
            m_lastFrame = currentFrame;
            changedThisFrame = false;
            uint32_t currentHash = calculateCurrentHash();
            if (m_lastHash != currentHash) {
                m_lastHash = currentHash;
                changedThisFrame = true;
                return true;
            }
        }
        return changedThisFrame;
    }

};

struct BLASComponent {
    std::shared_ptr<BLAS> blas;

    BLASComponent(std::shared_ptr<Mesh> mesh) {
        try {
            blas = std::make_shared<BLAS>(mesh);
            blas->build();
        } catch (const std::runtime_error& e) {
            RP_CORE_ERROR("BLASComponent: Failed to create BLAS: {}", e.what());
        }
    }
};

struct ShadowComponent {
    std::unique_ptr<ShadowMap> shadowMap;
    std::shared_ptr<ShadowDataBuffer> shadowDataBuffer;
    bool isActive = true;
    
    ShadowComponent(float width, float height) {
        shadowMap = std::make_unique<ShadowMap>(width, height);
        shadowDataBuffer = std::make_shared<ShadowDataBuffer>();
    }
};

struct CascadedShadowComponent {
    std::unique_ptr<CascadedShadowMap> cascadedShadowMap;
    std::shared_ptr<ShadowDataBuffer> shadowDataBuffer;
    bool isActive = true;
    
    CascadedShadowComponent(float width, float height, uint8_t numCascades, float lambda) {
        cascadedShadowMap = std::make_unique<CascadedShadowMap>(width, height, numCascades, lambda);
        shadowDataBuffer = std::make_shared<ShadowDataBuffer>();
    }
};

struct BoundingBoxComponent {
    // start off as invalid
    BoundingBox localBoundingBox;
    BoundingBox worldBoundingBox;


    BoundingBoxComponent() = default;
    BoundingBoxComponent(glm::vec3 min, glm::vec3 max) {
        localBoundingBox = BoundingBox(min, max);
        worldBoundingBox = localBoundingBox;
    }

    void updateWorldBoundingBox(const glm::mat4& transform) {
            worldBoundingBox = localBoundingBox.transform(transform);
    }

};

struct RigidBodyComponent {
    std::unique_ptr<Entropy::ColliderBase> collider;

    RigidBodyComponent(std::unique_ptr<Entropy::ColliderBase> collider) 
        : collider(std::move(collider)) {}
};

// Light data structure for shader
struct LightData {
    alignas(16) glm::vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    alignas(16) glm::vec4 direction;     // w = range
    alignas(16) glm::vec4 color;         // w = intensity
    alignas(16) glm::vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};

// Light uniform buffer object (binding 1)
struct LightUniformBufferObject {
    alignas(4) uint32_t numLights = 0;
    LightData lights[MAX_LIGHTS];
};

}


