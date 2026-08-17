#ifndef RAPTURE__GBUFFERPASS_H
#define RAPTURE__GBUFFERPASS_H

#include "gpu/pipelines/GraphicsPipeline.h"
#include "gpu/shaders/Shader.h"
#include "renderer/MDIBatch.h"
#include "renderer/SceneGeometryDraw.h"
#include "renderer/passes/RenderPass.h"

#include "assets/asset_manager/AssetManager.h"
#include "core/events/GameEvents.h"
#include "gpu/buffers/UniformBuffer.h"
#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/command_buffers/CommandPool.h"
#include "gpu/descriptors/DescriptorBinding.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "gpu/descriptors/DescriptorSet.h"
#include "gpu/textures/Texture.h"
#include "gpu/vulkan_context/VulkanContext.h"
#include "renderer/generators/terrain/TerrainGenerator.h"
#include "scene/Scene.h"
#include "scene/cameras/CameraCommon.h"
#include "scene/components/Components.h"

#include <cstdint>
#include <memory>

namespace Rapture {

enum class GBufferFlags : uint32_t {
    // Vertex attribute flags (bits 0-4)
    HAS_NORMALS = 1u,
    HAS_TANGENTS = 2u,
    HAS_BITANGENTS = 4u,
    HAS_TEXCOORDS = 8u,

    // Material texture flags (bits 5-13)
    HAS_ALBEDO_MAP = 32u,
    HAS_NORMAL_MAP = 64u,
    HAS_METALLIC_ROUGHNESS_MAP = 128u,
    HAS_AO_MAP = 256u,
    HAS_METALLIC_MAP = 512u,
    HAS_ROUGHNESS_MAP = 1024u,
    HAS_EMISSIVE_MAP = 2048u,
    HAS_SPECULAR_MAP = 4096u,
    HAS_HEIGHT_MAP = 8192u
};

class GBufferPass : public RenderPass {
  public:
    GBufferPass(float width, float height, uint32_t framesInFlight);
    ~GBufferPass();

    static FramebufferSpecification getFramebufferSpecification();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    Texture *getNormalTexture(uint32_t frameInFlight) const { return m_normalTextures[frameInFlight].get(); }
    Texture *getAlbedoTexture(uint32_t frameInFlight) const { return m_albedoSpecTextures[frameInFlight].get(); }
    Texture *getMaterialTexture(uint32_t frameInFlight) const { return m_materialTextures[frameInFlight].get(); }
    Texture *getShadingModelTexture(uint32_t frameInFlight) const { return m_shadingModelTextures[frameInFlight].get(); }

    // Getters for bindless texture indices for current frame
    uint32_t getNormalTextureIndex() const { return m_normalTextureIndices[m_currentFrame]; }
    uint32_t getAlbedoTextureIndex() const { return m_albedoTextureIndices[m_currentFrame]; }
    uint32_t getMaterialTextureIndex() const { return m_materialTextureIndices[m_currentFrame]; }
    uint32_t getShadingModelTextureIndex() const { return m_shadingModelTextureIndices[m_currentFrame]; }

    // Getters for all bindless texture indices
    const std::vector<uint32_t> &getNormalTextureIndices() const { return m_normalTextureIndices; }
    const std::vector<uint32_t> &getAlbedoTextureIndices() const { return m_albedoTextureIndices; }
    const std::vector<uint32_t> &getMaterialTextureIndices() const { return m_materialTextureIndices; }

  private:
    void createTextures();

    /**
     * @brief Builds one of the two entity pipelines
     * @param skinned Whether the pipeline deforms its vertices by a skeleton pose
     */
    void createPipeline(bool skinned);

    void createTerrainPipeline();
    void bindGBufferTexturesToBindlessSet();
    void setupCommandResources();

    // Record terrain rendering only
    void recordTerrainCommands(CommandBuffer *secondaryCb, Scene &activeScene, ecs::EntityAccessor camera,
                               TerrainGenerator &terrain, uint32_t currentFrame);

    // Record entity rendering only
    void recordEntityCommands(CommandBuffer *secondaryCb, const RenderPassContext &context);

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    const RenderContext *m_rc = nullptr;
    Shader *m_shader = nullptr;
    float m_width;
    float m_height;
    uint32_t m_framesInFlight;
    uint32_t m_currentFrame;

    VmaAllocator m_vmaAllocator;
    VkDevice m_device;

    // Multiple textures for each frame in flight
    std::vector<std::unique_ptr<Texture>> m_normalTextures;
    std::vector<std::unique_ptr<Texture>> m_albedoSpecTextures;
    std::vector<std::unique_ptr<Texture>> m_materialTextures;
    std::vector<std::unique_ptr<Texture>> m_shadingModelTextures;

    // Bindless texture indices for each frame in flight
    std::vector<uint32_t> m_normalTextureIndices;
    std::vector<uint32_t> m_albedoTextureIndices;
    std::vector<uint32_t> m_materialTextureIndices;
    std::vector<uint32_t> m_shadingModelTextureIndices;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    Shader *m_skinnedShader = nullptr;
    std::unique_ptr<GraphicsPipeline> m_skinnedPipeline;

    // Terrain rendering
    Shader *m_terrainShader = nullptr;

    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<GraphicsPipeline> m_terrainPipeline;

    CommandPoolHash m_commandPoolHash = 0;
};

} // namespace Rapture
#endif // RAPTURE__GBUFFERPASS_H
