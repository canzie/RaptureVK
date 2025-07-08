#pragma once

#include <memory>
#include <vector>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include <glm/glm.hpp>

#include "Buffers/Buffers.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"
#include "Shaders/Shader.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"

#include "Pipelines/ComputePipeline.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"
#include "Utils/TextureFlattener.h"

#include "DDGICommon.h"

#include "Scenes/Entities/EntityCommon.h"

namespace Rapture {

class MaterialInstance;

class DynamicDiffuseGI {
public:
    DynamicDiffuseGI(uint32_t framesInFlight);
    ~DynamicDiffuseGI();

    void populateProbes(std::shared_ptr<Scene> scene);
    void populateProbesCompute(std::shared_ptr<Scene> scene, uint32_t frameIndex);

    std::shared_ptr<Texture> getRadianceTexture();
    std::shared_ptr<Texture> getVisibilityTexture();

    std::shared_ptr<Texture> getPrevRadianceTexture(); 
    std::shared_ptr<Texture> getPrevVisibilityTexture();


    std::shared_ptr<Texture> getRadianceTextureFlattened() { return m_IrradianceTextureFlattened ? m_IrradianceTextureFlattened->getFlattenedTexture() : nullptr; } 
    std::shared_ptr<Texture> getVisibilityTextureFlattened() { return m_DistanceTextureFlattened ? m_DistanceTextureFlattened->getFlattenedTexture() : nullptr; } 

    std::vector<glm::vec3>& getDebugProbePositions() { return m_DebugProbePositions; }

    std::shared_ptr<UniformBuffer> getProbeVolumeUniformBuffer() { return m_ProbeInfoBuffer; }

    bool isFrameEven() { return m_isEvenFrame; }

    void updateSkybox(std::shared_ptr<Scene> scene);
    void updateProbeVolume();

    ProbeVolume& getProbeVolume() { return m_ProbeVolume; }
    void setProbeVolume(const ProbeVolume& probeVolume) { m_ProbeVolume = probeVolume; m_isVolumeDirty = true; }
    void setVolumeDirty(bool dirty) { m_isVolumeDirty = dirty; }

    void onResize(uint32_t framesInFlight);

    // Get bindless indices for probe textures
    uint32_t getProbeIrradianceBindlessIndex() const { return m_probeIrradianceBindlessIndex; }
    uint32_t getProbeVisibilityBindlessIndex() const { return m_probeVisibilityBindlessIndex; }

    // Get current texture indices based on frame parity (for lighting pass)
    uint32_t getCurrentRadianceBindlessIndex() const { 
        return m_isEvenFrame ? m_probeIrradianceBindlessIndex : m_prevProbeIrradianceBindlessIndex; 
    }
    uint32_t getCurrentVisibilityBindlessIndex() const { 
        return m_isEvenFrame ? m_probeVisibilityBindlessIndex : m_prevProbeVisibilityBindlessIndex; 
    }

    // Mesh data SSBO index for compute shader access
    uint32_t getMeshDataSSBOIndex() const { return m_meshDataSSBOIndex; }

private:
    void castRays(std::shared_ptr<Scene> scene, uint32_t frameIndex);
    void blendTextures(uint32_t frameIndex);

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

    std::shared_ptr<ComputePipeline> m_DDGI_ProbeTracePipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeIrradianceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeDistanceBlendingPipeline;

    ProbeVolume m_ProbeVolume;

    // material to offsets
    // key: raw pointer to MaterialInstance
    // value: list of byte offsets in the MeshInfo SSBO where this material's parameters live
    std::unordered_map<MaterialInstance*, std::vector<uint32_t>> m_MaterialToOffsets;
    std::unordered_map<EntityID, uint32_t> m_MeshToOffsets;

    // Queue of materials that have changed this frame and need patching
    std::unordered_set<MaterialInstance*> m_dirtyMaterials;
    std::unordered_set<EntityID> m_dirtyMeshes;

    std::shared_ptr<StorageBuffer> m_MeshInfoBuffer;


    std::shared_ptr<UniformBuffer> m_ProbeInfoBuffer;


    // is actually irradiance but iam retarted, will need to update this everywhere :(
    std::shared_ptr<Texture> m_RadianceTexture;
    std::shared_ptr<Texture> m_VisibilityTexture;

    std::shared_ptr<Texture> m_PrevRadianceTexture;
    std::shared_ptr<Texture> m_PrevVisibilityTexture;

    std::shared_ptr<Texture> m_RayDataTexture;

    std::shared_ptr<FlattenTexture> m_IrradianceTextureFlattened;
    std::shared_ptr<FlattenTexture> m_DistanceTextureFlattened;
    std::shared_ptr<FlattenTexture> m_RayDataTextureFlattened;


    std::vector<glm::vec3> m_DebugProbePositions;

    VmaAllocator m_allocator;
    std::shared_ptr<VulkanQueue> m_computeQueue;

    std::vector<std::shared_ptr<CommandBuffer>> m_CommandBuffers;
    uint32_t m_framesInFlight;

    // used to alternate between the textures each frame
    bool m_isEvenFrame;

    bool m_isPopulated;
    bool m_isFirstFrame;

    bool m_isVolumeDirty;

    float m_Hysteresis;



    uint32_t m_meshCount;
    uint32_t m_probesPerRow; // Number of probes along the X-axis of the atlas texture

    // Probe texture bindless indices for use in lighting pass
    uint32_t m_probeIrradianceBindlessIndex = 0;
    uint32_t m_probeVisibilityBindlessIndex = 0;
    uint32_t m_prevProbeIrradianceBindlessIndex = 0;
    uint32_t m_prevProbeVisibilityBindlessIndex = 0;

    // Mesh data SSBO index for compute shader access
    uint32_t m_meshDataSSBOIndex = 0;

    std::shared_ptr<Texture> m_skyboxTexture;


    static std::shared_ptr<Texture> s_defaultSkyboxTexture;
};

}

