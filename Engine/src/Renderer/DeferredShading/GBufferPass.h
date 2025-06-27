#pragma once

#include "Shaders/Shader.h"
#include "Pipelines/GraphicsPipeline.h"
#include "Renderer/MDIBatch.h"

#include "AssetManager/AssetManager.h"
#include "Scenes/Scene.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Cameras/CameraCommon.h"
#include "Textures/Texture.h"
#include "WindowContext/VulkanContext/VulkanContext.h"
#include "Components/Components.h"
#include "Events/GameEvents.h"

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

class GBufferPass {
public:
    GBufferPass(float width, float height, uint32_t framesInFlight);
    ~GBufferPass();

    static FramebufferSpecification getFramebufferSpecification();

    // NOTE: assumes that the command buffer is already started, and will be ended by the caller
    void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene, uint32_t currentFrame);

    // Getters for current frame's GBuffer textures
    std::shared_ptr<Texture> getPositionTexture() const { return m_positionDepthTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getNormalTexture() const { return m_normalTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getAlbedoTexture() const { return m_albedoSpecTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getMaterialTexture() const { return m_materialTextures[m_currentFrame]; }
    std::shared_ptr<Texture> getDepthTexture() const { return m_depthStencilTextures[m_currentFrame]; }

    std::vector<std::shared_ptr<Texture>> getPositionDepthTextures() const { return m_positionDepthTextures; }
    std::vector<std::shared_ptr<Texture>> getNormalTextures() const { return m_normalTextures; }
    std::vector<std::shared_ptr<Texture>> getAlbedoSpecTextures() const { return m_albedoSpecTextures; }
    std::vector<std::shared_ptr<Texture>> getMaterialTextures() const { return m_materialTextures; }
    std::vector<std::shared_ptr<Texture>> getDepthTextures() const { return m_depthStencilTextures; }

    // Getters for bindless texture indices for current frame
    uint32_t getPositionTextureIndex() const { return m_positionTextureIndices[m_currentFrame]; }
    uint32_t getNormalTextureIndex() const { return m_normalTextureIndices[m_currentFrame]; }
    uint32_t getAlbedoTextureIndex() const { return m_albedoTextureIndices[m_currentFrame]; }
    uint32_t getMaterialTextureIndex() const { return m_materialTextureIndices[m_currentFrame]; }
    uint32_t getDepthTextureIndex() const { return m_depthTextureIndices[m_currentFrame]; }

    // Getters for all bindless texture indices
    const std::vector<uint32_t>& getPositionTextureIndices() const { return m_positionTextureIndices; }
    const std::vector<uint32_t>& getNormalTextureIndices() const { return m_normalTextureIndices; }
    const std::vector<uint32_t>& getAlbedoTextureIndices() const { return m_albedoTextureIndices; }
    const std::vector<uint32_t>& getMaterialTextureIndices() const { return m_materialTextureIndices; }
    const std::vector<uint32_t>& getDepthTextureIndices() const { return m_depthTextureIndices; }



private:
    void createTextures();
    void createPipeline();
    void bindGBufferTexturesToBindlessSet();

    void beginDynamicRendering(std::shared_ptr<CommandBuffer> commandBuffer);

    void setupDynamicRenderingMemoryBarriers(std::shared_ptr<CommandBuffer> commandBuffer);

    void transitionToShaderReadableLayout(std::shared_ptr<CommandBuffer> commandBuffer);


private:
    std::weak_ptr<Shader> m_shader; 
    AssetHandle m_handle;
    float m_width;
    float m_height;
    uint32_t m_framesInFlight;
    uint32_t m_currentFrame;

    VmaAllocator m_vmaAllocator;
    VkDevice m_device;

    // Multiple textures for each frame in flight
    std::vector<std::shared_ptr<Texture>> m_positionDepthTextures;
    std::vector<std::shared_ptr<Texture>> m_normalTextures;
    std::vector<std::shared_ptr<Texture>> m_albedoSpecTextures;
    std::vector<std::shared_ptr<Texture>> m_materialTextures;
    std::vector<std::shared_ptr<Texture>> m_depthStencilTextures;

    // Bindless texture indices for each frame in flight
    std::vector<uint32_t> m_positionTextureIndices;
    std::vector<uint32_t> m_normalTextureIndices;
    std::vector<uint32_t> m_albedoTextureIndices;
    std::vector<uint32_t> m_materialTextureIndices;
    std::vector<uint32_t> m_depthTextureIndices;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    // MDI batching system
    std::unique_ptr<MDIBatchMap> m_mdiBatchMap;
    std::unique_ptr<MDIBatchMap> m_selectedEntityBatchMap; // Separate batches for selected entities

    VkRenderingAttachmentInfo m_colorAttachmentInfo[4];
    VkRenderingAttachmentInfo m_depthAttachmentInfo;

    std::shared_ptr<Entity> m_selectedEntity;
    size_t m_entitySelectedListenerId;
};

}