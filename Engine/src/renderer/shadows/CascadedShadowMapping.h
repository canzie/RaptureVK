#ifndef RAPTURE__CASCADEDSHADOWMAPPING_H
#define RAPTURE__CASCADEDSHADOWMAPPING_H

#include "assets/asset_manager/AssetManager.h"
#include "core/utils/TextureFlattener.h"
#include "gpu/pipelines/GraphicsPipeline.h"
#include "gpu/shaders/Shader.h"
#include "gpu/textures/Texture.h"

#include "gpu/buffers/UniformBuffer.h"
#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/descriptors/DescriptorBinding.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "gpu/descriptors/DescriptorSet.h"

#include "renderer/Frustum.h"
#include "renderer/generators/terrain/TerrainCuller.h"
#include "renderer/generators/terrain/TerrainGenerator.h"

#include "renderer/MDIBatch.h"

#include "scene/Scene.h"

#include "gpu/vulkan_context/RenderContext.h"

#include <array>
#include <glm/glm.hpp>
#include <memory>

namespace Rapture {

// Forward declarations
struct LightComponent;
struct TransformComponent;
struct CameraComponent;

// Projection type enum (to avoid dependency on Frustum.h)
enum class ProjectionType {
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

    CommandBuffer *recordSecondary(Scene &activeScene, uint32_t currentFrame, TerrainGenerator *terrain);

    void beginDynamicRendering(CommandBuffer *commandBuffer);
    void endDynamicRendering(CommandBuffer *commandBuffer);

    // Returns the calculated split depths for each cascade using a hybrid approach
    std::vector<float> calculateCascadeSplits(float nearPlane, float farPlane, float lambda = 0.5f);

    // Calculates the light space matrices for each cascade, and the split depths
    std::vector<CascadeData> calculateCascades(const glm::vec3 &lightDir, const glm::mat4 &viewMatrix, const glm::mat4 &projMatrix,
                                               float nearPlane, float farPlane,
                                               ProjectionType cameraProjectionType = ProjectionType::Perspective);

    uint8_t getNumCascades() const { return m_NumCascades; }

    std::vector<CascadeData> updateViewMatrix(const LightComponent &lightComp, const TransformComponent &transformComp,
                                              const CameraComponent &cameraComp);

    std::shared_ptr<Texture> getShadowTexture() const { return m_shadowTextureArray; }
    Texture *getFlattenedShadowTexture() const
    {
        return m_flattenedShadowTexture ? m_flattenedShadowTexture->getFlattenedTexture() : nullptr;
    }

    /**
     * @brief Toggle the per-frame flatten of the cascade array into a debug texture
     * @param enabled Whether to flatten the cascade array each frame
     */
    void enableDebugTexture(bool enabled);

    uint32_t getTextureHandle() { return m_shadowTextureArray->getBindlessIndex(); }

    std::vector<glm::mat4> getLightViewProjections() const { return m_lightViewProjections; }

    float getLambda() const { return m_lambda; }
    void setLambda(float lambda) { m_lambda = std::clamp(lambda, 0.0f, 1.0f); }

    float getShadowDistance() const { return m_shadowDistance; }
    void setShadowDistance(float distance) { m_shadowDistance = std::max(distance, 1.0f); }

    std::vector<float> getCascadeSplits() const { return m_cascadeSplits; }

  private:
    void createPipeline();
    void createTerrainPipeline();
    void createShadowTexture();
    void createUniformBuffers();
    void setupCommandResources();

    void recordTerrainCommands(CommandBuffer *commandBuffer, TerrainGenerator *terrain, const glm::vec3 &cameraPos);

    void setupDynamicRenderingMemoryBarriers(CommandBuffer *commandBuffer);
    void transitionToShaderReadableLayout(CommandBuffer *commandBuffer);

    // Extracts view frustum corners for a specific cascade depth slice
    // All parameters relate to the camera, not the light
    std::array<glm::vec3, 8> extractFrustumCorners(const glm::mat4 &cameraProjectionMatrix, // The camera's projection matrix
                                                   const glm::mat4 &cameraViewMatrix,       // The camera's view matrix
                                                   float cascadeNearPlane,                  // Near plane for this specific cascade
                                                   float cascadeFarPlane,                   // Far plane for this specific cascade
                                                   ProjectionType cameraProjectionType);    // Type of projection used by the camera

  private:
    const RenderContext *m_rc = nullptr;
    float m_width;
    float m_height;
    float m_lambda;
    float m_shadowDistance = 80.0f;
    uint8_t m_NumCascades;

    bool m_firstFrame = true;
    bool m_debugFlattenEnabled = false;

    uint32_t m_currentFrame = 0;
    uint32_t m_framesInFlight = 1;

    std::vector<glm::mat4> m_lightViewProjections;
    std::vector<float> m_cascadeSplits;

    std::shared_ptr<Texture> m_shadowTextureArray;

    std::unique_ptr<FlattenTexture> m_flattenedShadowTexture;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    std::vector<std::shared_ptr<UniformBuffer>> m_cascadeMatricesBuffers;
    std::vector<uint32_t> m_cascadeMatricesIndices;

    // Rendering attachments info
    VkRenderingAttachmentInfo m_depthAttachmentInfo{};

    Shader *m_shader = nullptr;

    // Terrain shadow rendering
    std::shared_ptr<GraphicsPipeline> m_terrainPipeline;
    Shader *m_terrainShader = nullptr;

    std::vector<AssetRef> m_shaderAssets;
    Frustum m_shadowFrustum;
    std::vector<TerrainCullBuffers> m_terrainShadowBuffers;

    VmaAllocator m_allocator;

    // MDI batching system - one per frame in flight
    std::vector<std::unique_ptr<MDIBatchMap>> m_mdiBatchMaps;

    CommandPoolHash m_commandPoolHash = 0;
};

} // namespace Rapture

#endif // RAPTURE__CASCADEDSHADOWMAPPING_H
