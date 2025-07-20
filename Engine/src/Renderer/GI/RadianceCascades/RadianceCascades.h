#pragma once

#include "RCCommon.h"
#include "Textures/Texture.h"
#include "Utils/TextureFlattener.h"

#include "Pipelines/ComputePipeline.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Scenes/Scene.h"

#include <memory>
#include <array>
#include <vector>

namespace Rapture {

#define MAX_CASCADES 6

/*

tkae probe a, and direction d
then go to cascade b and take the 8 probes, 
from the b probes find the rays with closest direction equal to d
then do some occlusion stuff
then merge based on ??? | b + wa * (a - b)

*/


class RadianceCascades {

public:

    RadianceCascades(uint32_t framesInFlight);
    ~RadianceCascades() = default;

    void build(const BuildParams& buildParams);
    void castRays(std::shared_ptr<Scene> scene, uint32_t frameIndex);

    // Getters for visualization
    const std::array<RadianceCascadeLevel, MAX_CASCADES>& getCascades() const { return m_radianceCascades; }
    const RadianceCascadeLevel& getCascade(uint32_t index) const { return m_radianceCascades[index]; }
    
    // Get probe positions for a specific cascade
    std::vector<glm::vec3> getCascadeProbePositions(uint32_t cascadeIndex) const;


private:
    void buildTextures();
    void buildDescriptorSet();
    void buildPipelines();
    void buildCommandBuffers(uint32_t framesInFlight);
    void buildUniformBuffers();

    void mergeCascades(std::shared_ptr<CommandBuffer> commandBuffer);


private:
    std::array<RadianceCascadeLevel, MAX_CASCADES> m_radianceCascades;
    std::array<std::shared_ptr<Texture>, MAX_CASCADES> m_cascadeTextures;
    // used for debugging to view the 2d texture arrays in 2d
    std::array<std::shared_ptr<FlattenTexture>, MAX_CASCADES> m_flatCascadeTextures;
    std::array<std::shared_ptr<FlattenTexture>, MAX_CASCADES> m_flatMergedCascadeTextures;

    std::vector<std::shared_ptr<UniformBuffer>> m_cascadeUniformBuffers;


    std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;

    std::shared_ptr<ComputePipeline> m_probeTracePipeline;
    std::shared_ptr<ComputePipeline> m_mergeCascadePipeline;
    std::vector<std::shared_ptr<DescriptorSet>> m_probeTraceDescriptorSets;


    };
}