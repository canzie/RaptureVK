#include "ShadowDataBuffer.h"
#include "Components/Components.h"
#include "Renderer/Shadows/ShadowCommon.h"
#include "Scenes/Scene.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Logging/Log.h"

namespace Rapture {

ShadowDataBuffer::ShadowDataBuffer() 
    : ObjectDataBuffer(DescriptorSetBindingLocation::SHADOW_DATA_UBO, sizeof(ShadowBufferData)) {
}

void ShadowDataBuffer::update(const LightComponent& light, const ShadowComponent& shadow, uint32_t entityID) {
    if (!shadow.shadowMap || !shadow.isActive) {
        return;
    }

    // Create shadow buffer data for this individual shadow map
    ShadowBufferData shadowData{};
    shadowData.type = static_cast<int>(light.type);
    shadowData.cascadeCount = 1;
    shadowData.lightIndex = entityID;
    shadowData.textureHandle = shadow.shadowMap->getTextureHandle();
    shadowData.cascadeMatrices[0] = shadow.shadowMap->getLightViewProjection();
    shadowData.cascadeSplitsViewSpace[0] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    updateBuffer(&shadowData, sizeof(ShadowBufferData));
}

void ShadowDataBuffer::update(const LightComponent& light, const CascadedShadowComponent& shadow, uint32_t entityID) {
    if (!shadow.cascadedShadowMap || !shadow.isActive) {
        return;
    }

    // Create shadow buffer data for this individual cascaded shadow map
    ShadowBufferData shadowData{};
    shadowData.type = static_cast<int>(light.type);
    shadowData.cascadeCount = shadow.cascadedShadowMap->getNumCascades();
    shadowData.lightIndex = entityID;
    shadowData.textureHandle = shadow.cascadedShadowMap->getTextureHandle();

    // # splits will always be numcascades + 1
    auto splits = shadow.cascadedShadowMap->getCascadeSplits();

    // Populate cascade data from the shadow map
    for (uint8_t i = 0; i < shadow.cascadedShadowMap->getNumCascades(); i++) {
        shadowData.cascadeMatrices[i] = shadow.cascadedShadowMap->getLightViewProjections()[i];
        shadowData.cascadeSplitsViewSpace[i] = glm::vec4(splits[i], splits[i+1], 0.0f, -1.0f);
    }

    updateBuffer(&shadowData, sizeof(ShadowBufferData));
}

} 