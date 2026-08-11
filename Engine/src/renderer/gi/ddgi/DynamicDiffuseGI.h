#pragma once

#include "asset_manager/AssetHandle.h"
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "buffers/Buffers.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/command_buffers/CommandPool.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "buffers/UniformBuffer.h"
#include "scenes/Scene.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"

#include "pipelines/ComputePipeline.h"
#include "utils/TextureFlattener.h"
#include "window_context/vulkan_context/VulkanQueue.h"

#include "DDGICommon.h"
#include "window_context/vulkan_context/RenderContext.h"

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

class DynamicDiffuseGI {
  public:
    DynamicDiffuseGI(uint32_t framesInFlight);
    ~DynamicDiffuseGI();

    void populateProbesCompute(Scene &scene, uint32_t frameIndex);

    std::shared_ptr<Texture> getRadianceTexture() { return m_RadianceTexture; }
    std::shared_ptr<Texture> getVisibilityTexture() { return m_VisibilityTexture; }

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

    // Get bindless indices for probe textures
    uint32_t getProbeIrradianceBindlessIndex() const { return m_probeIrradianceBindlessIndex; }
    uint32_t getProbeVisibilityBindlessIndex() const { return m_probeVisibilityBindlessIndex; }
    uint32_t getProbeOffsetBindlessIndex() const { return m_probeOffsetBindlessIndex; }
    uint32_t getProbeClassificationBindlessIndex() const { return m_probeClassificationBindlessIndex; }

    const std::vector<std::shared_ptr<UniformBuffer>> &getProbeVolumeUniformBuffers() const { return m_ProbeInfoBuffers; }

  private:
    void castRays(Scene &scene, CommandBuffer *commandBuffer, uint32_t frameIndex);
    void blendTextures(CommandBuffer *commandBuffer, uint32_t frameIndex);
    void classifyProbes(CommandBuffer *commandBuffer, uint32_t frameIndex);
    void relocateProbes(CommandBuffer *commandBuffer, uint32_t frameIndex);

    void initTextures();
    void initProbeInfoBuffer();

    void createPipelines();
    void setupProbeTextures();

    void clearTextures();

  private:
    const RenderContext *m_rc = nullptr;
    Shader *m_DDGI_ProbeTraceShader = nullptr;
    Shader *m_DDGI_ProbeIrradianceBlendingShader = nullptr;
    Shader *m_DDGI_ProbeDistanceBlendingShader = nullptr;
    Shader *m_DDGI_ProbeRelocationShader = nullptr;
    Shader *m_DDGI_ProbeClassificationShader = nullptr;

    std::vector<AssetRef> m_shaderAssets;

    std::shared_ptr<ComputePipeline> m_DDGI_ProbeTracePipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeIrradianceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeDistanceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeRelocationPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeClassificationPipeline;

    ProbeVolume m_ProbeVolume;

    std::vector<std::shared_ptr<UniformBuffer>> m_ProbeInfoBuffers;
    std::shared_ptr<DescriptorBindingUniformBuffer> m_probeInfoBinding;

    // is actually irradiance but iam retarted, will need to update this everywhere :(
    std::shared_ptr<Texture> m_RadianceTexture;
    std::shared_ptr<Texture> m_VisibilityTexture;

    std::shared_ptr<Texture> m_RayDataTexture;

    std::shared_ptr<Texture> m_ProbeClassificationTexture;
    std::shared_ptr<Texture> m_ProbeOffsetTexture; // stores an offset for each probe, enables the use of the relocation shader

    std::unique_ptr<FlattenTexture> m_IrradianceTextureFlattened;
    std::unique_ptr<FlattenTexture> m_DistanceTextureFlattened;

    std::unique_ptr<FlattenTexture> m_RayDataTextureFlattened;
    std::unique_ptr<FlattenTexture> m_ProbeClassificationTextureFlattened;
    std::unique_ptr<FlattenTexture> m_ProbeOffsetTextureFlattened;

    std::vector<glm::vec3> m_DebugProbePositions;

    VmaAllocator m_allocator;
    std::shared_ptr<VulkanQueue> m_computeQueue;

    VkDevice m_device = VK_NULL_HANDLE;

    CommandPoolHash m_commandPoolHash = 0;
    uint32_t m_framesInFlight;

    bool m_isFirstFrame;

    bool m_isVolumeDirty;

    uint32_t m_meshCount;
    uint32_t m_probesPerRow; // Number of probes along the X-axis of the atlas texture

    // Probe texture bindless indices for use in lighting pass
    uint32_t m_probeIrradianceBindlessIndex = 0;
    uint32_t m_probeVisibilityBindlessIndex = 0;
    uint32_t m_probeOffsetBindlessIndex = 0;
    uint32_t m_probeClassificationBindlessIndex = 0;

    Texture *m_skyboxTexture;
    float m_skyIntensity = 1.0f;

    std::unique_ptr<Texture> m_defaultSkyboxTexture;

    std::shared_ptr<DescriptorSet> m_probeTraceDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeIrradianceBlendingDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeDistanceBlendingDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeClassificationDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeRelocationDescriptorSet;
};

} // namespace Rapture
