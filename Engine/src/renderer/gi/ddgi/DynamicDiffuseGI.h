#ifndef RAPTURE__DYNAMIC_DIFFUSE_GI_H
#define RAPTURE__DYNAMIC_DIFFUSE_GI_H

#include <array>
#include <atomic>
#include <memory>
#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/shaders/AShader.h>
#include <vector>

#include <glm/glm.hpp>

#include <rapture/gpu/buffers/Buffers.h>
#include <rapture/gpu/buffers/UniformBuffer.h>
#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <rapture/gpu/command_buffers/CommandPool.h>
#include <rapture/gpu/descriptors/DescriptorSet.h>
#include <rapture/gpu/shaders/Shader.h>
#include <rapture/gpu/textures/Texture.h>
#include <rapture/scene/Scene.h>

#include "core/utils/TextureFlattener.h"
#include <rapture/gpu/pipelines/ComputePipeline.h>
#include <rapture/gpu/vulkan_context/VulkanQueue.h>

#include "renderer/gi/ddgi/DDGICommon.h"
#include <rapture/gpu/vulkan_context/RenderContext.h>

namespace Rapture {

class MaterialInstance;

enum class DDGIDescriptorSetBindingLocation {
    RAY_DATA = 400,
    IRRADIANCE = 401,
    VISIBILITY = 402,
    PROBE_IRRADIANCE_ATLAS = 401,
    PROBE_DISTANCE_ATLAS = 402,
    PROBE_CLASSIFICATION = 403,
    PROBE_OFFSET = 404,
    PROBE_RELOCATION = 404
};

/**
 * @brief Bindless indices of the probe textures from one finished probe update
 */
struct DDGIProbeTextureIndices {
    uint32_t irradiance = UINT32_MAX;
    uint32_t visibility = UINT32_MAX;
    uint32_t offset = UINT32_MAX;
    uint32_t classification = UINT32_MAX;
};

class DynamicDiffuseGI {
  public:
    DynamicDiffuseGI(uint32_t framesInFlight);
    ~DynamicDiffuseGI();

    /**
     * @brief Records and submits one probe update, skipped while the previous update is still being recorded
     * @param scene The scene the probes trace against
     * @param frameIndex The frame in flight the update belongs to
     */
    void populateProbesCompute(Scene &scene, uint32_t frameIndex);

    Texture *getRadianceTextureFlattened()
    {
        return m_IrradianceTextureFlattened ? m_IrradianceTextureFlattened->getFlattenedTexture() : nullptr;
    }
    Texture *getVisibilityTextureFlattened()
    {
        return m_DistanceTextureFlattened ? m_DistanceTextureFlattened->getFlattenedTexture() : nullptr;
    }

    std::vector<glm::vec3> &getDebugProbePositions() { return m_DebugProbePositions; }

    uint32_t getRayDataTextureBindlessIndex() const { return m_RayDataTexture ? m_RayDataTexture->getBindlessIndex() : 0; }

    const ProbeVolume &getProbeVolume() const { return m_ProbeVolume; }

    void updateSkybox(Scene &scene);
    void updateProbeVolume(uint32_t frameIndex);

    /**
     * @brief The probe textures of the most recently finished probe update
     * @return Their bindless indices, all from the same update
     */
    DDGIProbeTextureIndices getPublishedProbeTextureIndices() const;

    const std::vector<std::shared_ptr<UniformBuffer>> &getProbeVolumeUniformBuffers() const { return m_ProbeInfoBuffers; }

  private:
    struct ProbeTextureSet {
        std::unique_ptr<Texture> irradiance;
        std::unique_ptr<Texture> visibility;
        std::unique_ptr<Texture> offset;
        std::unique_ptr<Texture> classification;

        DDGIProbeTextureIndices indices;

        std::unique_ptr<DescriptorSet> irradianceBlendingDescriptorSet;
        std::unique_ptr<DescriptorSet> distanceBlendingDescriptorSet;
        std::unique_ptr<DescriptorSet> relocationDescriptorSet;
        std::unique_ptr<DescriptorSet> classificationDescriptorSet;
    };

    static constexpr uint32_t PROBE_TEXTURE_SET_COUNT = 2;

  private:
    void recordAndSubmitProbeUpdate(Scene &scene, uint32_t frameIndex);

    void beginWritingProbeTextureSet(CommandBuffer *commandBuffer, ProbeTextureSet &writtenSet);
    void castRays(Scene &scene, CommandBuffer *commandBuffer, uint32_t frameIndex, const ProbeTextureSet &publishedSet);
    void relocateProbes(CommandBuffer *commandBuffer, uint32_t frameIndex, const ProbeTextureSet &publishedSet,
                        ProbeTextureSet &writtenSet);
    void classifyProbes(CommandBuffer *commandBuffer, uint32_t frameIndex, ProbeTextureSet &writtenSet);
    void blendTextures(CommandBuffer *commandBuffer, uint32_t frameIndex, const ProbeTextureSet &publishedSet,
                       ProbeTextureSet &writtenSet);

    void initTextures();
    void initProbeTextureSet(ProbeTextureSet &set, const TextureSpecification &irradianceSpec,
                             const TextureSpecification &distanceSpec, const TextureSpecification &classificationSpec,
                             const TextureSpecification &offsetSpec);
    void initProbeInfoBuffer();

    void createPipelines();

    void clearTextures();

  private:
    const RenderContext *m_rc = nullptr;
    Ref<AShader> m_DDGI_ProbeTraceShader;
    Ref<AShader> m_DDGI_ProbeIrradianceBlendingShader;
    Ref<AShader> m_DDGI_ProbeDistanceBlendingShader;
    Ref<AShader> m_DDGI_ProbeRelocationShader;
    Ref<AShader> m_DDGI_ProbeClassificationShader;

    std::shared_ptr<ComputePipeline> m_DDGI_ProbeTracePipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeIrradianceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeDistanceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeRelocationPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeClassificationPipeline;

    ProbeVolume m_ProbeVolume;

    std::vector<std::shared_ptr<UniformBuffer>> m_ProbeInfoBuffers;
    std::shared_ptr<DescriptorBindingUniformBuffer> m_probeInfoBinding;

    std::array<ProbeTextureSet, PROBE_TEXTURE_SET_COUNT> m_probeTextureSets;
    std::atomic<uint32_t> m_publishedSetIndex{0};
    std::atomic<bool> m_isProbeUpdateRecording{false};

    std::shared_ptr<Texture> m_RayDataTexture;

    std::unique_ptr<FlattenTexture> m_IrradianceTextureFlattened;
    std::unique_ptr<FlattenTexture> m_DistanceTextureFlattened;

    std::unique_ptr<FlattenTexture> m_RayDataTextureFlattened;
    std::unique_ptr<FlattenTexture> m_ProbeClassificationTextureFlattened;
    std::unique_ptr<FlattenTexture> m_ProbeOffsetTextureFlattened;

    std::vector<glm::vec3> m_DebugProbePositions;

    VmaAllocator m_allocator;
    std::shared_ptr<VulkanQueue> m_graphicsQueue;

    VkDevice m_device = VK_NULL_HANDLE;

    uint32_t m_framesInFlight;

    bool m_isVolumeDirty;

    uint32_t m_meshCount;
    uint32_t m_probesPerRow; // Number of probes along the X-axis of the atlas texture

    Texture *m_skyboxTexture;
    float m_skyIntensity = 1.0f;

    std::unique_ptr<Texture> m_defaultSkyboxTexture;

    std::shared_ptr<DescriptorSet> m_probeTraceDescriptorSet;
};

} // namespace Rapture

#endif // RAPTURE__DYNAMIC_DIFFUSE_GI_H
