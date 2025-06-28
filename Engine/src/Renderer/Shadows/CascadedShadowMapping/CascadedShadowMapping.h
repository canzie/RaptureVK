#pragma once

#include "Pipelines/GraphicsPipeline.h"
#include "AssetManager/AssetManager.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"
#include "Utils/TextureFlattener.h"

#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"

#include "Renderer/Frustum/Frustum.h"

#include "Components/Systems/ObjectDataBuffers/ShadowDataBuffer.h"
#include "Renderer/MDIBatch.h"

#include "Scenes/Scene.h"

#include <array>
#include <memory>
#include <glm/glm.hpp>

namespace Rapture {

// Forward declarations
struct LightComponent;
struct TransformComponent;
struct CameraComponent;

    // Projection type enum (to avoid dependency on Frustum.h)
    enum class ProjectionType
    {
        Perspective,
        Orthographic
    };

    struct CascadeData {
        float nearPlane;
        float farPlane;
        glm::mat4 lightViewProj;
    };

class CascadedShadowMap {
public:
    CascadedShadowMap(float width, float height, uint32_t numCascades, float lambda);

    ~CascadedShadowMap();

    // Returns the calculated split depths for each cascade using a hybrid approach
    std::vector<float> calculateCascadeSplits(float nearPlane, float farPlane, float lambda = 0.5f);
    
    // Calculates the light space matrices for each cascade, and the split depths
    std::vector<CascadeData> calculateCascades(
        const glm::vec3& lightDir, 
        const glm::mat4& viewMatrix, 
        const glm::mat4& projMatrix,
        float nearPlane,
        float farPlane,
        ProjectionType cameraProjectionType = ProjectionType::Perspective);


    uint8_t getNumCascades() const { return m_NumCascades; }
    
    void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, 
                            std::shared_ptr<Scene> activeScene, 
                            uint32_t currentFrame);
    
    std::vector<CascadeData> updateViewMatrix(const LightComponent& lightComp, const TransformComponent& transformComp, const CameraComponent& cameraComp);

    std::shared_ptr<Texture> getShadowTexture() const { return m_shadowTextureArray; }
    std::shared_ptr<Texture> getFlattenedShadowTexture() const { return m_flattenedShadowTexture ? m_flattenedShadowTexture->getFlattenedTexture() : nullptr; }

    uint32_t getTextureHandle() { return m_shadowTextureArray->getBindlessIndex(); }
    std::shared_ptr<ShadowDataBuffer> getShadowDataBuffer() { return m_shadowDataBuffer; }

    std::vector<glm::mat4> getLightViewProjections() const { return m_lightViewProjections; }

    float getLambda() const { return m_lambda; }
    void setLambda(float lambda) { m_lambda = std::clamp(lambda, 0.0f, 1.0f); }

    std::vector<float> getCascadeSplits() const { return m_cascadeSplits; }

private:
    void createPipeline();
    void createShadowTexture();
    void createUniformBuffers();

    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);
    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);
    void transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer);

    // Extracts view frustum corners for a specific cascade depth slice
    // All parameters relate to the camera, not the light
    std::array<glm::vec3, 8> extractFrustumCorners(
        const glm::mat4& cameraProjectionMatrix, // The camera's projection matrix
        const glm::mat4& cameraViewMatrix,       // The camera's view matrix
        float cascadeNearPlane,                  // Near plane for this specific cascade
        float cascadeFarPlane,                   // Far plane for this specific cascade
        ProjectionType cameraProjectionType);    // Type of projection used by the camera


private:
    float m_width;
    float m_height;
    float m_lambda;
    uint8_t m_NumCascades;
    
    bool m_firstFrame = true;

    uint32_t m_currentFrame = 0;
    uint32_t m_framesInFlight = 3; // Default, will be updated

    std::vector<glm::mat4> m_lightViewProjections;
    std::vector<float> m_cascadeSplits;

    // ping pong textures
    std::shared_ptr<Texture> m_shadowTextureArray;

    uint32_t m_writeIndex = 0;
    uint32_t m_readIndex = 1;

    std::shared_ptr<FlattenTexture> m_flattenedShadowTexture;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    std::shared_ptr<ShadowDataBuffer> m_shadowDataBuffer;
    std::shared_ptr<UniformBuffer> m_cascadeMatricesBuffer;
    uint32_t m_cascadeMatricesIndex;

    // Rendering attachments info
    VkRenderingAttachmentInfo m_depthAttachmentInfo{};
    
    std::weak_ptr<Shader> m_shader;
    AssetHandle m_handle;

    VmaAllocator m_allocator;
    
    // MDI batching system - one per frame in flight
    std::vector<std::unique_ptr<MDIBatchMap>> m_mdiBatchMaps;

    
    };

}