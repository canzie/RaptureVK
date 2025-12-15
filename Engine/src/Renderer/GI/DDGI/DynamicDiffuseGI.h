#pragma once

#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "Buffers/Buffers.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Scenes/Scene.h"
#include "Shaders/Shader.h"
#include "Textures/Texture.h"

#include "Pipelines/ComputePipeline.h"
#include "Utils/TextureFlattener.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"

#include "DDGICommon.h"

#include "Scenes/Entities/EntityCommon.h"

namespace Rapture {

class MaterialInstance;

enum class DDGIDescriptorSetBindingLocation {
    RAY_DATA = 400,
    IRRADIANCE = 401,
    PREV_IRRADIANCE = 402,
    VISIBILITY = 403,
    PREV_VISIBILITY = 404,
    PROBE_IRRADIANCE_ATLAS = 401,     // Alias for irradiance
    PROBE_IRRADIANCE_ATLAS_ALT = 402, // Alias for prev_irradiance
    PROBE_DISTANCE_ATLAS = 403,       // Alias for visibility
    PROBE_DISTANCE_ATLAS_ALT = 404,   // Alias for prev_visibility
    PROBE_CLASSIFICATION = 405,
    PROBE_OFFSET = 406,
    PROBE_RELOCATION = 406 // same as probe offset
};

class DynamicDiffuseGI {
  public:
    DynamicDiffuseGI(uint32_t framesInFlight);
    ~DynamicDiffuseGI();

    void populateProbes(std::shared_ptr<Scene> scene);
    void populateProbesCompute(std::shared_ptr<Scene> scene, uint32_t frameIndex);

    std::shared_ptr<Texture> getRadianceTexture() { return m_RadianceTexture; }
    std::shared_ptr<Texture> getVisibilityTexture() { return m_VisibilityTexture; }

    std::shared_ptr<Texture> getRadianceTextureFlattened()
    {
        return m_IrradianceTextureFlattened ? m_IrradianceTextureFlattened->getFlattenedTexture() : nullptr;
    }
    std::shared_ptr<Texture> getVisibilityTextureFlattened()
    {
        return m_DistanceTextureFlattened ? m_DistanceTextureFlattened->getFlattenedTexture() : nullptr;
    }

    std::vector<glm::vec3> &getDebugProbePositions() { return m_DebugProbePositions; }

    std::shared_ptr<UniformBuffer> getProbeVolumeUniformBuffer() { return m_ProbeInfoBuffer; }

    void updateSkybox(std::shared_ptr<Scene> scene);
    void updateProbeVolume();

    ProbeVolume &getProbeVolume() { return m_ProbeVolume; }
    void setProbeVolume(const ProbeVolume &probeVolume)
    {
        m_ProbeVolume = probeVolume;
        m_isVolumeDirty = true;
    }
    void setVolumeDirty(bool dirty) { m_isVolumeDirty = dirty; }

    void onResize(uint32_t framesInFlight);

    // Get bindless indices for probe textures
    uint32_t getProbeIrradianceBindlessIndex() const { return m_probeIrradianceBindlessIndex; }
    uint32_t getProbeVisibilityBindlessIndex() const { return m_probeVisibilityBindlessIndex; }

    // Mesh data SSBO index for compute shader access
    uint32_t getMeshDataSSBOIndex() const { return m_meshDataSSBOIndex; }

  private:
    void castRays(std::shared_ptr<Scene> scene, uint32_t frameIndex);
    void blendTextures(uint32_t frameIndex);
    void classifyProbes(uint32_t frameIndex);
    void relocateProbes(uint32_t frameIndex);

    void initTextures();
    void initProbeInfoBuffer();

    void createPipelines();
    void setupProbeTextures();
    void updateMeshInfoBuffer(std::shared_ptr<Scene> scene);

    uint32_t getSunLightDataIndex(std::shared_ptr<Scene> scene);

    void clearTextures();

  private:
    std::shared_ptr<Shader> m_DDGI_ProbeTraceShader;
    std::shared_ptr<Shader> m_DDGI_ProbeIrradianceBlendingShader;
    std::shared_ptr<Shader> m_DDGI_ProbeDistanceBlendingShader;
    std::shared_ptr<Shader> m_DDGI_ProbeRelocationShader;
    std::shared_ptr<Shader> m_DDGI_ProbeClassificationShader;

    std::shared_ptr<ComputePipeline> m_DDGI_ProbeTracePipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeIrradianceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeDistanceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeRelocationPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeClassificationPipeline;

    ProbeVolume m_ProbeVolume;

    // material to offsets
    // key: raw pointer to MaterialInstance
    // value: list of byte offsets in the MeshInfo SSBO where this material's parameters live
    std::unordered_map<MaterialInstance *, std::vector<uint32_t>> m_MaterialToOffsets;
    std::unordered_map<EntityID, uint32_t> m_MeshToOffsets;

    // Queue of materials that have changed this frame and need patching
    std::unordered_set<MaterialInstance *> m_dirtyMaterials;
    std::unordered_set<EntityID> m_dirtyMeshes;

    std::shared_ptr<StorageBuffer> m_MeshInfoBuffer;

    std::shared_ptr<UniformBuffer> m_ProbeInfoBuffer;

    // is actually irradiance but iam retarted, will need to update this everywhere :(
    std::shared_ptr<Texture> m_RadianceTexture;
    std::shared_ptr<Texture> m_VisibilityTexture;

    std::shared_ptr<Texture> m_RayDataTexture;

    std::shared_ptr<Texture> m_ProbeClassificationTexture;
    std::shared_ptr<Texture> m_ProbeOffsetTexture; // stores an offset for each probe, enables the use of the relocation shader

    std::shared_ptr<FlattenTexture> m_IrradianceTextureFlattened;
    std::shared_ptr<FlattenTexture> m_DistanceTextureFlattened;

    std::shared_ptr<FlattenTexture> m_RayDataTextureFlattened;
    std::shared_ptr<FlattenTexture> m_ProbeClassificationTextureFlattened;
    std::shared_ptr<FlattenTexture> m_ProbeOffsetTextureFlattened;

    std::vector<glm::vec3> m_DebugProbePositions;

    VmaAllocator m_allocator;
    std::shared_ptr<VulkanQueue> m_computeQueue;

    std::vector<std::shared_ptr<CommandBuffer>> m_CommandBuffers;
    uint32_t m_framesInFlight;

    bool m_isPopulated;
    bool m_isFirstFrame;

    bool m_isVolumeDirty;

    uint32_t m_meshCount;
    uint32_t m_probesPerRow; // Number of probes along the X-axis of the atlas texture

    // Probe texture bindless indices for use in lighting pass
    uint32_t m_probeIrradianceBindlessIndex = 0;
    uint32_t m_probeVisibilityBindlessIndex = 0;

    // Mesh data SSBO index for compute shader access
    uint32_t m_meshDataSSBOIndex = 0;

    std::shared_ptr<Texture> m_skyboxTexture;

    static std::shared_ptr<Texture> s_defaultSkyboxTexture;

    std::shared_ptr<DescriptorSet> m_probeTraceDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeIrradianceBlendingDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeDistanceBlendingDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeClassificationDescriptorSet;
    std::shared_ptr<DescriptorSet> m_probeRelocationDescriptorSet;
};

} // namespace Rapture
