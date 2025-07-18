#include "RadianceCascades.h"

#include "Logging/Log.h"

#include <format>

namespace Rapture {

RadianceCascades::RadianceCascades() {
    // load shaders and compute pipelines
    
}

void RadianceCascades::build(const BuildParams &buildParams) {


    float prevMinRange = 0.0f;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {
        RadianceCascadeLevel cascade = {};

        cascade.cascadeLevel = i;

        float maxRange = buildParams.baseRange * std::pow(buildParams.rangeScaleFactor, static_cast<float>(i));

        cascade.minProbeDistance = prevMinRange;
        cascade.maxProbeDistance = maxRange;

        prevMinRange = maxRange;


        glm::vec3 scaledDimsF = glm::vec3(buildParams.baseGridDimensions) * std::pow(buildParams.gridScaleFactor, static_cast<float>(i));
        cascade.probeGridDimensions = glm::max(glm::ivec3(glm::floor(scaledDimsF)), glm::ivec3(1)); // prevent zero dimensions

        glm::vec3 extent = glm::vec3(maxRange); // cubic region size
        cascade.probeSpacing = extent / glm::vec3(cascade.probeGridDimensions);

        cascade.angularResolution = static_cast<uint32_t>(
            buildParams.baseAngularResolution * std::pow(buildParams.angularScaleFactor, static_cast<float>(i))
        );

        // this is the center of the grid, the shader should shift the grid by half of the extent
        cascade.probeOrigin = glm::vec3(0.0f);

        cascade.cascadeTextureIndex = 0xFFFFFFFF;



        m_radianceCascades[i] = cascade;
        
    }

    try {
        buildTextures();
    } catch (const std::runtime_error& e) {
        RP_CORE_ERROR("Failed to build cascade textures: {}", e.what());
        throw;
    }

}

void RadianceCascades::buildTextures() {

    TextureSpecification defaultSpec = {};
    defaultSpec.filter = TextureFilter::Nearest;
    defaultSpec.srgb = false;
    defaultSpec.storageImage = true;
    defaultSpec.format = TextureFormat::RGBA32F;

    for (uint32_t i = 0; i < MAX_CASCADES; i++) {

        RadianceCascadeLevel& cascade = m_radianceCascades[i];

        if (cascade.cascadeLevel == UINT32_MAX) {
            RP_CORE_ERROR("Cascade not initializes, call build() first");
            throw std::runtime_error("Cascade not initialized, call build() first");
        }
        defaultSpec.width = cascade.probeGridDimensions.x * cascade.angularResolution;
        defaultSpec.height = cascade.probeGridDimensions.z * cascade.angularResolution;
        defaultSpec.depth = cascade.probeGridDimensions.y;

        m_cascadeTextures[i] = std::make_shared<Texture>(defaultSpec);

        uint32_t bindlessIndex = m_cascadeTextures[i]->getBindlessIndex();

        if (bindlessIndex == UINT32_MAX) {
            RP_CORE_ERROR("Failed to get bindless index for cascade({}) texture", cascade.cascadeLevel);
            throw std::runtime_error(std::format("Failed to get bindless index for cascade({}) texture", cascade.cascadeLevel).c_str());
        }

        cascade.cascadeTextureIndex = bindlessIndex;

    }

}


}
