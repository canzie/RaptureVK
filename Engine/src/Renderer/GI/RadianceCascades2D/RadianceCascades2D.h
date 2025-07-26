#pragma once

#include "RC2DCommon.h"
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

#define MAX_CASCADES 5

/*

tkae probe a, and direction d
then go to cascade b and take the 8 probes, 
from the b probes find the rays with closest direction equal to d
then do some occlusion stuff
then merge based on ??? | b + wa * (a - b)

*/


class RadianceCascades2D {

public:

    RadianceCascades2D(uint32_t framesInFlight);
    ~RadianceCascades2D();

    void build(const BuildParams2D& buildParams);
    void castRays(std::shared_ptr<Scene> scene, uint32_t frameIndex);

    // Getters for visualization
    const std::array<RadianceCascadeLevel2D, MAX_CASCADES>& getCascades() const { return m_radianceCascades; }
    const RadianceCascadeLevel2D& getCascade(uint32_t index) const { return m_radianceCascades[index]; }
    
    // Get probe positions for a specific cascade
    std::vector<glm::vec3> getCascadeProbePositions(uint32_t cascadeIndex) const;

    void updateBaseRange(float baseRange);
    void updateBaseSpacing(float baseSpacing);

    const BuildParams2D& getBuildParams() const { return m_buildParams; }



private:
    void buildTextures();
    void buildDescriptorSet();
    void buildPipelines();
    void buildCommandBuffers(uint32_t framesInFlight);
    void buildUniformBuffers();

    void mergeCascades(std::shared_ptr<CommandBuffer> commandBuffer);
    void integrateCascade(std::shared_ptr<CommandBuffer> commandBuffer);



private:
    std::array<RadianceCascadeLevel2D, MAX_CASCADES> m_radianceCascades;
    std::array<std::shared_ptr<Texture>, MAX_CASCADES> m_cascadeTextures;

    // stores the integrated irradiance from cascade 0
    std::shared_ptr<Texture> m_irradianceCascadeTexture;


    BuildParams2D m_buildParams;

    std::vector<std::shared_ptr<UniformBuffer>> m_cascadeUniformBuffers;
    std::vector<uint32_t> m_cascadeUniformBufferIndices; // keep these for cleaning


    std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;

    std::shared_ptr<ComputePipeline> m_probeTracePipeline;
    std::shared_ptr<ComputePipeline> m_mergeCascadePipeline;
    std::shared_ptr<ComputePipeline> m_integrateIrradiancePipeline;
    std::vector<std::shared_ptr<DescriptorSet>> m_probeTraceDescriptorSets;
    std::shared_ptr<DescriptorSet> m_integrateIrradianceDescriptorSet;


    };
}