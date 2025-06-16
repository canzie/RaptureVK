#pragma once

#include <memory>
#include <vector>
#include <cmath>

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

#include "DDGICommon.h"

namespace Rapture {


class DynamicDiffuseGI {
public:
    DynamicDiffuseGI();
    ~DynamicDiffuseGI();

    void populateProbes(std::shared_ptr<Scene> scene);
    void populateProbesCompute(std::shared_ptr<Scene> scene);

    std::shared_ptr<Texture> getRadianceTexture();
    std::shared_ptr<Texture> getVisibilityTexture();

    std::shared_ptr<Texture> getPrevRadianceTexture(); 
    std::shared_ptr<Texture> getPrevVisibilityTexture();


    std::shared_ptr<Texture> getRadianceTextureFlattened() { return m_IrradianceTextureFlattened; } 
    std::shared_ptr<Texture> getVisibilityTextureFlattened() { return m_DistanceTextureFlattened; } 

    std::vector<glm::vec3>& getDebugProbePositions() { return m_DebugProbePositions; }

    std::shared_ptr<UniformBuffer> getProbeVolumeUniformBuffer() { return m_ProbeInfoBuffer; }

    bool isFrameEven() { return m_isEvenFrame; }

    void updateSkybox(std::shared_ptr<Scene> scene);

private:
    void castRays(std::shared_ptr<Scene> scene);
    void blendTextures();
    void flattenTextures(std::shared_ptr<Texture> flatTexture, std::shared_ptr<Texture> texture, int descriptorSetIndex);

    void initTextures();
    void updateSunProperties(std::shared_ptr<Scene> scene);
    void initProbeInfoBuffer();
    void initializeSunProperties();

    void createPipelines();
    void createDescriptorSets(std::shared_ptr<Scene> scene);
    void createProbeTraceDescriptorSets(std::shared_ptr<Scene> scene);
    void createProbeBlendingDescriptorSets(std::shared_ptr<Scene> scene, bool isEvenFrame);


    void clearTextures();

private:
    std::shared_ptr<Shader> m_DDGI_ProbeTraceShader;
    std::shared_ptr<Shader> m_DDGI_ProbeIrradianceBlendingShader;
    std::shared_ptr<Shader> m_DDGI_ProbeDistanceBlendingShader;
    std::shared_ptr<Shader> m_Flatten2dArrayShader;

    std::shared_ptr<ComputePipeline> m_DDGI_ProbeTracePipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeIrradianceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_DDGI_ProbeDistanceBlendingPipeline;
    std::shared_ptr<ComputePipeline> m_Flatten2dArrayPipeline;

    ProbeVolume m_ProbeVolume;
    SunProperties m_SunShadowProps;

    
    std::shared_ptr<StorageBuffer> m_MeshInfoBuffer;


    std::shared_ptr<UniformBuffer> m_SunLightBuffer;
    std::shared_ptr<UniformBuffer> m_ProbeInfoBuffer;


    // is actually irradiance but iam retarted, will need to update this everywhere :(
    std::shared_ptr<Texture> m_RadianceTexture;
    std::shared_ptr<Texture> m_VisibilityTexture;

    std::shared_ptr<Texture> m_PrevRadianceTexture;
    std::shared_ptr<Texture> m_PrevVisibilityTexture;

    std::shared_ptr<Texture> m_RayDataTexture;

    std::shared_ptr<Texture> m_IrradianceTextureFlattened;
    std::shared_ptr<Texture> m_DistanceTextureFlattened;
    std::shared_ptr<Texture> m_RayDataTextureFlattened;


    std::vector<glm::vec3> m_DebugProbePositions;

    VmaAllocator m_allocator;
    std::shared_ptr<VulkanQueue> m_computeQueue;

    std::shared_ptr<CommandBuffer> m_CommandBuffer;


    // used to alternate between the textures each frame
    bool m_isEvenFrame;

    bool m_isPopulated;
    bool m_isFirstFrame;

    float m_Hysteresis;

    uint32_t m_meshCount;
    uint32_t m_probesPerRow; // Number of probes along the X-axis of the atlas texture

    std::vector<std::shared_ptr<DescriptorSet>> m_rayTraceDescriptorSets;
    std::shared_ptr<DescriptorSet> m_rayTracePrevTextureDescriptorSet[2]; // Set 2: Previous textures for probe trace
    std::shared_ptr<DescriptorSet> m_flattenDescriptorSet[3];
    std::shared_ptr<DescriptorSet> m_IrradianceBlendingDescriptorSet[2];
    std::shared_ptr<DescriptorSet> m_DistanceBlendingDescriptorSet[2];

    std::shared_ptr<DescriptorSet> m_probeVolumeDescriptorSet;

    std::shared_ptr<Texture> m_skyboxTexture;

    static std::shared_ptr<Texture> s_defaultSkyboxTexture;
};

}

